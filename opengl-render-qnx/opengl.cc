// vnc_qnx_pipelined_multithreaded.cpp
//
// OPTIMIZATION SUMMARY:
// 1. MEMORY: Persistent frame buffer — eliminates malloc/realloc/free per frame
// 2. MEMORY: Persistent decompression buffer — eliminates malloc/free per rect
// 3. MEMORY: Decompress directly into final buffer — eliminates memcpy per rect
// 4. NETWORK: TCP_NODELAY — disables Nagle's algorithm for lower latency sends
// 5. NETWORK: Larger SO_RCVBUF — reduces recv() syscall count
// 6. NETWORK: Batched recv for small header reads — 4 bytes instead of 1+1+2
// 7. GPU: glTexSubImage2D — partial texture updates instead of full re-upload
// 8. GPU: Persistent VBO for text overlay — eliminates glGen/glDelete per frame
// 9. GPU: Cache attribute locations — eliminates glGetAttribLocation per frame
// 10. RENDER: Only upload overlay text buffer size, not full 20000-element array
// 11. PROTOCOL: Handshake fix — "\x01" was sending "\\x01" (4 bytes, not 1)
// 12. PROTOCOL: Incremental framebuffer update requests pipelined
// 13. ZLIB: inflateEnd() on clean session close (prevents memory leak)
// 15. THREADING: Dedicated pthread Network Thread for blocking socket recv()
// 16. MEMORY: Double-buffered framebuffers (Front/Back) to prevent tearing
// 17. SYNC: pthread_mutex_t for safe, low-latency cross-thread state transfer
// 18. PERF: C++98 compliant circular buffer for sliding-window VNC FPS tracking

#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <time.h>
#include "stb_easyfont.hh"
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "miniz.h"

#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>

#include <netinet/tcp.h>
#include <netinet/tcp_var.h> // Restored for TCPCTL_KEEPCNT
#include <fcntl.h>
#include <sys/sysctl.h>
#include <sys/param.h>

#include <pthread.h>

#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif

// ---------------- Timing ----------------
struct FrameTimings {
    double recv_ms;
    double inflate_ms;
    double parse_ms;
    double texture_upload_ms;
    double total_frame_ms;
    FrameTimings() : recv_ms(0.0), inflate_ms(0.0), parse_ms(0.0), texture_upload_ms(0.0), total_frame_ms(0.0) {}
};

static uint64_t now_us() {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
    }
#endif
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static double us_to_ms(uint64_t us) { return (double)us / 1000.0; }

static ssize_t recv_timed(int sockfd, void* buf, size_t len, int flags, FrameTimings* timings) {
    uint64_t t0 = now_us();
    ssize_t r = recv(sockfd, buf, len, flags);
    uint64_t t1 = now_us();
    if (timings) timings->recv_ms += us_to_ms(t1 - t0);
    return r;
}

static int recv_exact(int sockfd, void* buf, size_t len, FrameTimings* timings) {
    size_t off = 0;
    char* p = (char*)buf;
    while (off < len) {
        ssize_t r = recv_timed(sockfd, p + off, len - off, 0, timings);
        if (r == 0) { errno = ECONNRESET; return -1; }
        if (r < 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

// ============================================================================
// Persistent Double Buffers
// ============================================================================
struct PersistentBuffers {
    char* frontBuffer;
    char* backBuffer;
    size_t frameBufferCap;
    char* compressedBuf;
    size_t compressedCap;

    PersistentBuffers() : frontBuffer(NULL), backBuffer(NULL), frameBufferCap(0),
        compressedBuf(NULL), compressedCap(0) {}

    ~PersistentBuffers() {
        free(frontBuffer);
        free(backBuffer);
        free(compressedBuf);
    }

    bool ensureFrameBuffers(size_t needed) {
        if (needed > frameBufferCap) {
            size_t newCap = (needed > frameBufferCap * 2) ? needed : (frameBufferCap * 2);
            char* pFront = (char*)realloc(frontBuffer, newCap);
            char* pBack = (char*)realloc(backBuffer, newCap);
            if (!pFront || !pBack) return false;
            frontBuffer = pFront;
            backBuffer = pBack;
            frameBufferCap = newCap;
        }
        return true;
    }

    char* ensureCompressedBuf(size_t needed) {
        if (needed > compressedCap) {
            size_t newCap = (needed > compressedCap * 2) ? needed : (compressedCap * 2);
            char* p = (char*)realloc(compressedBuf, newCap);
            if (!p) return NULL;
            compressedBuf = p;
            compressedCap = newCap;
        }
        return compressedBuf;
    }
};

static PersistentBuffers g_bufs;

// --- Threading & Sync Globals ---
pthread_mutex_t g_frameMutex = PTHREAD_MUTEX_INITIALIZER;
volatile bool g_newFrameReady = false;
volatile bool g_running = true;
int g_currentSocket = -1;

// Shared state updated by Network Thread, read by Render Thread
int g_sharedFbW = 0;
int g_sharedFbH = 0;
int g_sharedFinalH = 0;
FrameTimings g_sharedTimings;
uint64_t g_sharedVncFramesDecoded = 0;

// ---------------- GLES setup ----------------
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

GLint g_posAttr = -1;
GLint g_texAttr = -1;
GLint g_textPosAttr = -1;
GLuint g_textVBO = 0;

// ---------------- VNC shaders ----------------
const char* vertexShaderSource =
    "attribute vec2 position;    \n"
    "attribute vec2 texCoord;     \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = vec4(position, 0.0, 1.0); \n"
    "   v_texCoord = texCoord;   \n"
    "   gl_PointSize = 4.0;      \n"
    "}                            \n";

const char* fragmentShaderSource =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D texture;\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, v_texCoord);\n"
    "}\n";

const char* vertexShaderSourceText =
    "attribute vec2 position;    \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = vec4(position, 0.0, 1.0); \n"
    "   gl_PointSize = 1.0;      \n"
    "}                            \n";

const char* fragmentShaderSourceText =
    "precision mediump float;\n"
    "void main()               \n"
    "{                         \n"
    "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); \n"
    "}                         \n";

// Geometry
GLfloat landscapeVertices[] = { -0.8f, 0.73f, 0.0f,  0.8f, 0.73f, 0.0f,  0.8f,-0.63f, 0.0f, -0.8f,-0.63f, 0.0f };
GLfloat portraitVertices[]  = { -0.8f, 1.0f,  0.0f,  0.8f, 1.0f,  0.0f,  0.8f,-0.67f, 0.0f, -0.8f,-0.67f, 0.0f };
GLfloat landscapeTexCoords[]= {  0.0f, 0.07f,        0.90f,0.07f,        0.90f,1.0f,         0.0f, 1.0f };
GLfloat portraitTexCoords[] = {  0.0f, 0.0f,         0.63f,0.0f,         0.63f,0.3f,         0.0f, 0.3f };
GLfloat backgroundColor[4]  = {  0.0f, 0.0f, 0.0f, 1.0f };

const char* PROTOCOL_VERSION = "RFB 003.003\n";
const char FRAMEBUFFER_UPDATE_REQUEST[] = { 3, 0, 0, 0, 0, 0, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF };
const char ZLIB_ENCODING[] = { 2, 0, 0, 2, 0, 0, 0, 6, 0, 0, 0, 0 };

int windowWidth  = 800;
int windowHeight = 480;
const char* VNC_SERVER_IP_ADDRESS = "10.173.189.62";
const int   VNC_SERVER_PORT       = 5900;

// ---------------- QNX Helpers ----------------
struct Command { const char* command; const char* error_message; };

void execute_initial_commands() {
    struct Command commands[] = {
        { "/eso/bin/apps/dmdt dc 70 3",  "Create new display table with context 3 failed with error" },
        { "/eso/bin/apps/dmdt sc 4 70",  "Set display 4 (VC) to display table 99 failed with error" }
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        int ret = system(commands[i].command);
        if (ret != 0) fprintf(stderr, "%s: %d\n", commands[i].error_message, ret);
    }
}

void execute_final_commands() {
    struct Command commands[] = {
        { "/eso/bin/apps/dmdt dc 70 33", "Create new display table with context 3 failed with error" },
        { "/eso/bin/apps/dmdt sc 4 70",  "Set display 4 (VC) to display table 99 failed with error" }
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        int ret = system(commands[i].command);
        if (ret != 0) fprintf(stderr, "%s: %d\n", commands[i].error_message, ret);
    }
}

int16_t byteArrayToInt16(const char* b) { return (int16_t)(((unsigned char)b[0] << 8) | (unsigned char)b[1]); }
int32_t byteArrayToInt32(const char* b) {
    return (int32_t)(((uint32_t)(unsigned char)b[0] << 24) | ((uint32_t)(unsigned char)b[1] << 16) |
                     ((uint32_t)(unsigned char)b[2] <<  8) | ((uint32_t)(unsigned char)b[3]      ));
}

// ---------------- GL Init ----------------
void Init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);

    GLuint vsT = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsT, 1, &vertexShaderSourceText, NULL);
    glCompileShader(vsT);

    GLuint fsT = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsT, 1, &fragmentShaderSourceText, NULL);
    glCompileShader(fsT);

    programObject = glCreateProgram();
    glAttachShader(programObject, vs);
    glAttachShader(programObject, fs);
    glLinkProgram(programObject);

    programObjectTextRender = glCreateProgram();
    glAttachShader(programObjectTextRender, vsT);
    glAttachShader(programObjectTextRender, fsT);
    glLinkProgram(programObjectTextRender);

    g_posAttr = glGetAttribLocation(programObject, "position");
    g_texAttr = glGetAttribLocation(programObject, "texCoord");
    g_textPosAttr = glGetAttribLocation(programObjectTextRender, "position");

    glGenBuffers(1, &g_textVBO);

    glClearColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3]);
}

void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    static char inputBuffer[20000];
    static GLfloat triangleBuffer[20000];

    memset(inputBuffer, 0, sizeof(inputBuffer));
    stb_easy_font_print(0, 0, (char*)text, NULL, inputBuffer, sizeof(inputBuffer));

    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;

    int triangleIndex = 0;
    for (int i = 0; i < (int)(sizeof(inputBuffer) / sizeof(GLfloat)); i += 8) {
        GLfloat* ptr = reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]);
        if (ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 0) break;

        triangleBuffer[triangleIndex++] = ptr[0] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[1] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = ptr[2] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[3] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = ptr[4] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[5] / size * -1 + ndcMovementY;

        triangleBuffer[triangleIndex++] = ptr[0] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[1] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = ptr[4] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[5] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = ptr[6] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = ptr[7] / size * -1 + ndcMovementY;
    }

    glUseProgram(programObjectTextRender);

    glBindBuffer(GL_ARRAY_BUFFER, g_textVBO);
    glBufferData(GL_ARRAY_BUFFER, triangleIndex * sizeof(GLfloat), triangleBuffer, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_textPosAttr);
    glVertexAttribPointer(g_textPosAttr, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex / 2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Config file parsing
void parseLineArray(char *line, const char *key, GLfloat *dest, int count) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *values = strchr(line, '=');
        if (values) {
            values++;
            for (int i = 0; i < count; i++) dest[i] = strtof(values, &values);
        }
    }
}
void parseLineInt(char *line, const char *key, int *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value) *dest = atoi(value + 1);
    }
}
void loadConfig(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        parseLineArray(line, "landscapeVertices", landscapeVertices, 12);
        parseLineArray(line, "portraitVertices", portraitVertices, 12);
        parseLineArray(line, "landscapeTexCoords", landscapeTexCoords, 8);
        parseLineArray(line, "portraitTexCoords", portraitTexCoords, 8);
        parseLineArray(line, "backgroundColor", backgroundColor, 4);
        parseLineInt(line, "windowWidth", &windowWidth);
        parseLineInt(line, "windowHeight", &windowHeight);
    }
    fclose(file);
}

// ============================================================================
// Network Parser - Writes to backBuffer directly
// ============================================================================
bool parseFramebufferUpdate(int socket_fd, int* frameBufferWidth, int* frameBufferHeight,
                            z_stream* strm, int* finalHeight, FrameTimings* timings)
{
    uint64_t parseStart = now_us();
    char msgHdr[4];
    if (recv_exact(socket_fd, msgHdr, 4, timings) != 0) return false;

    unsigned char messageType = (unsigned char)msgHdr[0];
    int rectCount = byteArrayToInt16(msgHdr + 2);

    if (messageType == 0) {
        (void)send(socket_fd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0);
    }

    int totalLoadedSize = 0;
    *finalHeight = 0;

    for (int i = 0; i < rectCount; i++) {
        char rectHdr[12];
        if (recv_exact(socket_fd, rectHdr, 12, timings) != 0) return false;

        int w = byteArrayToInt16(rectHdr + 4);
        int h = byteArrayToInt16(rectHdr + 6);
        int32_t encoding = byteArrayToInt32(rectHdr + 8);

        *frameBufferWidth = w;
        *frameBufferHeight = h;
        *finalHeight += h;

        if (encoding == 6) { // ZLIB
            char sizeBuf[4];
            if (recv_exact(socket_fd, sizeBuf, 4, timings) != 0) return false;

            int compressedSize = byteArrayToInt32(sizeBuf);
            if (compressedSize <= 0) return false;

            if (!g_bufs.ensureCompressedBuf(compressedSize)) return false;
            if (recv_exact(socket_fd, g_bufs.compressedBuf, (size_t)compressedSize, timings) != 0) return false;

            size_t outSize = (size_t)w * (size_t)h * 4u;
            size_t neededTotal = totalLoadedSize + outSize;

            if (!g_bufs.ensureFrameBuffers(neededTotal)) return false;

            strm->avail_in  = (uInt)compressedSize;
            strm->next_in   = (Bytef*)g_bufs.compressedBuf;
            strm->avail_out = (uInt)outSize;
            strm->next_out  = (Bytef*)(g_bufs.backBuffer + totalLoadedSize);

            uint64_t infStart = now_us();
            int ret = inflate(strm, Z_NO_FLUSH);
            if (timings) timings->inflate_ms += us_to_ms(now_us() - infStart);

            if (ret < 0 && ret != Z_BUF_ERROR) return false;

            totalLoadedSize = neededTotal;
        }
    }

    if (timings) timings->parse_ms = us_to_ms(now_us() - parseStart);
    return true;
}

// ============================================================================
// NETWORK THREAD
// ============================================================================
void* NetworkThreadFunc(void* arg) {
    (void)arg;

    while (g_running) {
        struct sockaddr_in serv_addr;
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) { usleep(200000); continue; }

        g_currentSocket = sockfd;

        int mib[4]; int ival = 0;
        mib[0] = CTL_NET; mib[1] = AF_INET; mib[2] = IPPROTO_TCP; mib[3] = TCPCTL_KEEPCNT;
        ival = 3; sysctl(mib, 4, NULL, NULL, &ival, sizeof(ival));

        mib[0] = CTL_NET; mib[1] = AF_INET; mib[2] = IPPROTO_TCP; mib[3] = TCPCTL_KEEPINTVL;
        ival = 2; sysctl(mib, 4, NULL, NULL, &ival, sizeof(ival));

        int keepalive = 1, keepidle = 2;
        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle, sizeof(keepidle));

        int nodelay = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        int rcvBufSize = 256 * 1024;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvBufSize, sizeof(rcvBufSize));

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        memset((char*)&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(VNC_SERVER_IP_ADDRESS);
        serv_addr.sin_port = htons(VNC_SERVER_PORT);

        struct timeval timeout;
        timeout.tv_sec = 10; timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 && errno != EINPROGRESS) {
            close(sockfd); usleep(200000);
            continue;
        }

        fd_set write_fds;
        FD_ZERO(&write_fds); FD_SET(sockfd, &write_fds);
        timeout.tv_sec = 5; timeout.tv_usec = 0;

        int result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
        if (result <= 0) { close(sockfd); usleep(200000); continue; }

        int so_error = 0; socklen_t len = sizeof(so_error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            close(sockfd); usleep(200000); continue;
        }
    	execute_initial_commands();  // View OFF before connect (like old)
        fcntl(sockfd, F_SETFL, flags); // Restore blocking

        // ============================================================
        // HANDSHAKE BLOCK - Replaced goto with strict scoping
        // ============================================================
        char buf[256];
        uint32_t nameLen = 0;
        z_stream strm;
        bool handshake_ok = false;

        do {
            if (recv_exact(sockfd, buf, 12, NULL) != 0) break;
            if (send(sockfd, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) < 0) break;
            if (recv(sockfd, buf, 4, 0) <= 0) break;

            if (send(sockfd, "\x01", 1, 0) < 0) break;
            if (recv_exact(sockfd, buf, 24, NULL) != 0) break;

            nameLen = ((uint32_t)(unsigned char)buf[20] << 24) | ((uint32_t)(unsigned char)buf[21] << 16) |
                      ((uint32_t)(unsigned char)buf[22] << 8)  | ((uint32_t)(unsigned char)buf[23]);

            if (nameLen > 0) {
                char* name = (char*)malloc(nameLen + 1);
                if (!name) break;
                if (recv_exact(sockfd, name, nameLen, NULL) != 0) { free(name); break; }
                free(name);
            }

            if (send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0) < 0) break;
            if (send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) break;

            memset(&strm, 0, sizeof(strm));
            if (inflateInit(&strm) != Z_OK) break;

            handshake_ok = true;
        } while(0);

        if (!handshake_ok) {
            close(sockfd);
            g_currentSocket = -1;
        	execute_final_commands();  // View OFF before connect (like old)
            continue;
        }

        // ============================================================
        // DECODING LOOP
        // ============================================================
        while (g_running) {
            int fbW = 0, fbH = 0, finalH = 0;
            FrameTimings localTimings;

            if (!parseFramebufferUpdate(sockfd, &fbW, &fbH, &strm, &finalH, &localTimings)) {
                inflateEnd(&strm);
                break;
            }

            // Sync with Render Thread
            pthread_mutex_lock(&g_frameMutex);

            char* tmp = g_bufs.frontBuffer;
            g_bufs.frontBuffer = g_bufs.backBuffer;
            g_bufs.backBuffer = tmp;

            g_sharedFbW = fbW;
            g_sharedFbH = fbH;
            g_sharedFinalH = finalH;
            g_sharedTimings = localTimings;
            g_sharedVncFramesDecoded++;
            g_newFrameReady = true;

            pthread_mutex_unlock(&g_frameMutex);
        }

        close(sockfd);
        execute_final_commands();  // View OFF before connect (like old)

        g_currentSocket = -1;
    }
    return NULL;
}

// ============================================================================
// MAIN
// ============================================================================
#define MAX_VNC_FRAMES 1024

int main(int argc, char* argv[]) {
    if (argc > 1) VNC_SERVER_IP_ADDRESS = argv[1];

    loadConfig("config.txt");

    void* func_handle = dlopen("libdisplayinit.so", RTLD_LAZY);
    if (!func_handle) { fprintf(stderr, "Error: %s\n", dlerror()); return 1; }

    void (*display_init)(int, int) = (void (*)(int, int))dlsym(func_handle, "display_init");
    if (display_init) display_init(0, 0);
    dlclose(func_handle);

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(eglDisplay, 0, 0);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 1, EGL_GREEN_SIZE, 1, EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };

    EGLConfig configs[5];
    EGLint num_configs = 0;
    EGLNativeWindowType windowEgl;
    int kdWindow = 0;

    eglChooseConfig(eglDisplay, config_attribs, configs, 1, &num_configs);
    eglConfig = configs[0];

    void* func_handle_d_c_w = dlopen("libdisplayinit.so", RTLD_LAZY);
    void (*display_create_window)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*) =
        (void (*)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*))dlsym(func_handle_d_c_w, "display_create_window");

    if (display_create_window) display_create_window(eglDisplay, configs[0], windowWidth, windowHeight, 3, &windowEgl, &kdWindow);
    dlclose(func_handle_d_c_w);

    eglSurface = eglCreateWindowSurface(eglDisplay, configs[0], windowEgl, 0);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, configs[0], EGL_NO_CONTEXT, context_attribs);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

    Init();

    g_bufs.ensureFrameBuffers(800 * 480 * 4);
    g_bufs.ensureCompressedBuf(256 * 1024);

    pthread_t networkThread;
    pthread_create(&networkThread, NULL, NetworkThreadFunc, NULL);

    execute_initial_commands();

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    int prevFbW = 0, prevFinalH = 0;
    bool firstFrame = true;

    int frameCount = 0;
    double renderFps = 0.0;
    uint64_t lastFpsUs = now_us();

    // Sliding Window State for VNC FPS
    uint64_t vncFrameTimes[MAX_VNC_FRAMES];
    int vncFrameHead = 0;
    int vncFrameTail = 0;
    int vncFrameCountWindow = 0;
    uint64_t lastVncDecodedCount = 0;

    FrameTimings displayTimings;

    while (g_running) {
        uint64_t frameStartUs = now_us();
        double textureUploadMs = 0.0;
        int renderFbW = 0, renderFinalH = 0;

        pthread_mutex_lock(&g_frameMutex);

        uint64_t currentDecoded = g_sharedVncFramesDecoded;

        if (g_newFrameReady) {
            renderFbW = g_sharedFbW;
            renderFinalH = g_sharedFinalH;
            displayTimings = g_sharedTimings;

            uint64_t texStart = now_us();
            if (firstFrame || renderFbW != prevFbW || renderFinalH != prevFinalH) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderFbW, renderFinalH, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
                prevFbW = renderFbW;
                prevFinalH = renderFinalH;
                firstFrame = false;
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, renderFbW, renderFinalH,
                                GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
            }
            textureUploadMs = us_to_ms(now_us() - texStart);
            g_newFrameReady = false;
        } else {
            renderFbW = prevFbW;
            renderFinalH = prevFinalH;
        }
        pthread_mutex_unlock(&g_frameMutex);

        // --- Calculate Sliding Window VNC FPS ---
        uint64_t newFrames = currentDecoded - lastVncDecodedCount;
        lastVncDecodedCount = currentDecoded;
        uint64_t currentLoopTimeUs = now_us();

        for (uint64_t i = 0; i < newFrames; i++) {
            vncFrameTimes[vncFrameHead] = currentLoopTimeUs;
            vncFrameHead = (vncFrameHead + 1) % MAX_VNC_FRAMES;
            if (vncFrameCountWindow < MAX_VNC_FRAMES) {
                vncFrameCountWindow++;
            } else {
                vncFrameTail = (vncFrameTail + 1) % MAX_VNC_FRAMES;
            }
        }

        while (vncFrameCountWindow > 0) {
            if ((currentLoopTimeUs - vncFrameTimes[vncFrameTail]) > 1000000ULL) {
                vncFrameTail = (vncFrameTail + 1) % MAX_VNC_FRAMES;
                vncFrameCountWindow--;
            } else {
                break;
            }
        }
        double vncFps = (double)vncFrameCountWindow;

        // --- Rendering ---
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(programObject);

        if (renderFbW > renderFinalH) {
            glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, landscapeVertices);
            glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, landscapeTexCoords);
        } else {
            glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, portraitVertices);
            glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, portraitTexCoords);
        }
        glEnableVertexAttribArray(g_posAttr);
        glEnableVertexAttribArray(g_texAttr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableVertexAttribArray(g_posAttr);
        glDisableVertexAttribArray(g_texAttr);

        // --- Stats ---
        //frameCount++;
        //uint64_t nowUs = now_us();
        //displayTimings.total_frame_ms = us_to_ms(nowUs - frameStartUs);

        //if ((nowUs - lastFpsUs) >= 1000000ULL) {
        //    renderFps = (double)frameCount * 1000000.0 / (double)(nowUs - lastFpsUs);
        //    frameCount = 0;
        //    lastFpsUs = nowUs;
        //}

        //char overlayText[512];
        //snprintf(overlayText, sizeof(overlayText),
        //    "Render FPS: %.1f\nVNC FPS: %.1f\nFrame: %.2f ms\nRecv: %.2f ms\nInflate: %.2f ms\nParse: %.2f ms\nGPU Up: %.2f ms",
        //    renderFps, vncFps, displayTimings.total_frame_ms, displayTimings.recv_ms,
        //    displayTimings.inflate_ms, displayTimings.parse_ms, textureUploadMs);

        //glUseProgram(programObjectTextRender);
        //print_string(-380, 220, overlayText, 1.0f, 1.0f, 0.0f, 80.0f);

        eglSwapBuffers(eglDisplay, eglSurface);
    }

    g_running = false;
    pthread_join(networkThread, NULL);

    glDeleteTextures(1, &textureID);
    return EXIT_SUCCESS;
}
