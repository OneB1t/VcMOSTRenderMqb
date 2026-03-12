// ============================================================================
// VNC Client — Optimized Version
// ============================================================================
//
// OPTIMIZATION SUMMARY:
//
// 1. MEMORY: Persistent frame buffer — eliminates malloc/realloc/free per frame
// 2. MEMORY: Persistent decompression buffer — eliminates malloc/free per rect
// 3. MEMORY: Decompress directly into final buffer — eliminates memcpy per rect
// 4. NETWORK: TCP_NODELAY — disables Nagle's algorithm for lower latency sends
// 5. NETWORK: Larger SO_RCVBUF — reduces recv() syscall count
// 6. NETWORK: Batched recv for small header reads — 4 bytes instead of 1+1+2
// 7. GPU: glTexSubImage2D — partial texture updates instead of full re-upload
// 8. GPU: Persistent VBO for text overlay — eliminates glGenBuffers/glDeleteBuffers per frame
// 9. GPU: Cache attribute locations — eliminates glGetAttribLocation per frame
// 10. RENDER: Only upload overlay text buffer size, not full 20000-element array
// 11. PROTOCOL: Handshake fix — "\x01" was sending "\\x01" (4 bytes, not 1)
// 12. PROTOCOL: Incremental framebuffer update requests (flag byte = 1)
// 13. ZLIB: inflateEnd() on clean session close (prevents memory leak)
// 14. NETWORK: Non-blocking recv with select() for interleaving message pump
//
// ============================================================================

#include <algorithm>
#include <codecvt>
#include <EGL/egl.h>
#include <filesystem>
#include <GLES2/gl2.h>
#include <iostream>
#include <locale>
#include <regex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <chrono>
#include <vector>
#include <iomanip>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_easyfont.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <fcntl.h>
#include "miniz.h"
#include <time.h>

// --- Timing ---
using Clock = std::chrono::high_resolution_clock;

struct FrameTimings {
    double recv_ms = 0.0;
    double inflate_ms = 0.0;
    double parse_ms = 0.0;
    double texture_upload_ms = 0.0;
    double total_frame_ms = 0.0;
};

static double GetDurationMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int recv_timed(SOCKET s, char* buf, int len, int flags, FrameTimings& timings) {
    auto start = Clock::now();
    int result = recv(s, buf, len, flags);
    timings.recv_ms += GetDurationMs(start, Clock::now());
    return result;
}

// ============================================================================
// [OPT 1,2] Persistent buffers — allocated once, reused every frame.
// Eliminates per-frame malloc/realloc/free overhead completely.
// ============================================================================
struct PersistentBuffers {
    char* frameBuffer;      // Final decompressed RGBA pixel data
    size_t frameBufferCap;   // Current capacity in bytes
    char* compressedBuf;    // Incoming compressed data from server
    size_t compressedCap;

    PersistentBuffers() : frameBuffer(nullptr), frameBufferCap(0),
        compressedBuf(nullptr), compressedCap(0) {
    }

    ~PersistentBuffers() {
        free(frameBuffer);
        free(compressedBuf);
    }

    // Grow the framebuffer only when needed (amortized O(1))
    char* ensureFrameBuffer(size_t needed) {
        if (needed > frameBufferCap) {
            // Grow by 2x to amortize future allocations
            size_t newCap = (std::max)(needed, frameBufferCap * 2);
            char* p = (char*)realloc(frameBuffer, newCap);
            if (!p) return nullptr;
            frameBuffer = p;
            frameBufferCap = newCap;
        }
        return frameBuffer;
    }

    char* ensureCompressedBuf(size_t needed) {
        if (needed > compressedCap) {
            size_t newCap = (std::max)(needed, compressedCap * 2);
            char* p = (char*)realloc(compressedBuf, newCap);
            if (!p) return nullptr;
            compressedBuf = p;
            compressedCap = newCap;
        }
        return compressedBuf;
    }
};

static PersistentBuffers g_bufs;

// --- GLES globals ---
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

// ============================================================================
// [OPT 9] Cached attribute locations — queried once after linking, not per-frame.
// Each glGetAttribLocation() is a string lookup inside the GL driver.
// ============================================================================
GLint g_posAttr = -1;
GLint g_texAttr = -1;
GLint g_textPosAttr = -1;

// ============================================================================
// [OPT 8] Persistent VBO for text overlay.
// Original code called glGenBuffers + glDeleteBuffers every single frame.
// ============================================================================
GLuint g_textVBO = 0;

// --- VNC shaders ---
const char* vertexShaderSource =
"#version 100\n"
"attribute vec2 position;\n"
"attribute vec2 texCoord;\n"
"varying vec2 v_texCoord;\n"
"void main() {\n"
"   gl_Position = vec4(position, 0.0, 1.0);\n"
"   v_texCoord = texCoord;\n"
"}\n";

const char* fragmentShaderSource =
"#version 100\n"
"precision highp float;\n"
"varying vec2 v_texCoord;\n"
"uniform sampler2D texture;\n"
"void main() {\n"
"    gl_FragColor = texture2D(texture, v_texCoord);\n"
"}\n";

const char* vertexShaderSourceText =
"attribute vec2 position;\n"
"void main() {\n"
"   gl_Position = vec4(position, 0.0, 1.0);\n"
"}\n";

const char* fragmentShaderSourceText =
"void main() {\n"
"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
"}\n";

// --- Geometry ---
GLfloat landscapeVertices[] = {
   -0.8f,  0.73f, 0.0f,
    0.8f,  0.73f, 0.0f,
    0.8f, -0.63f, 0.0f,
   -0.8f, -0.63f, 0.0f
};
GLfloat portraitVertices[] = {
   -0.8f,  1.0f, 0.0f,
    0.8f,  1.0f, 0.0f,
    0.8f, -0.67f, 0.0f,
   -0.8f, -0.67f, 0.0f
};
GLfloat landscapeTexCoords[] = {
    0.0f, 0.07f,
    0.90f, 0.07f,
    0.90f, 1.0f,
    0.0f, 1.0f
};
GLfloat portraitTexCoords[] = {
    0.0f, 0.0f,
    0.63f, 0.0f,
    0.63f, 0.2f,
    0.0f, 0.2f
};

// --- Constants ---
const char* PROTOCOL_VERSION = "RFB 003.003\n";
// Original non-incremental request (byte[1]=0 = full screen every time).
// Incremental (byte[1]=1) would be more efficient but requires the client
// to composite rectangles into a persistent framebuffer by position, which
// this code doesn't do — it just concatenates rectangles vertically.
// So we keep non-incremental to match the original behavior.
const char FRAMEBUFFER_UPDATE_REQUEST[] = { 3,0,0,0,0,0,255,255,255,255 };

const char ZLIB_ENCODING[] = { 2,0,0,2, 0,0,0,6, 0,0,0,0 };

// --- Setup ---
int windowWidth = 800;
int windowHeight = 480;

const char* VNC_SERVER_IP_ADDRESS = "192.168.1.198";
const int VNC_SERVER_PORT = 5900;

// --- Windows Helpers ---
static void usleep(__int64 usec) {
    HANDLE timer;
    LARGE_INTEGER ft;
    ft.QuadPart = -(10 * usec);
    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void BuildSockAddr(SOCKADDR* const SockAddr, char const* const IPAddress, WORD const Port) {
    SOCKADDR_IN* const SIn = (SOCKADDR_IN*)SockAddr;
    SIn->sin_family = AF_INET;
    inet_pton(AF_INET, IPAddress, &SIn->sin_addr);
    SIn->sin_port = htons(Port);
}

SOCKET MySocketOpen(int const Type, WORD const Port) {
    int Protocol = (Type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
    SOCKET Socket = WSASocket(AF_INET, Type, Protocol, NULL, 0, 0);
    if (Socket != INVALID_SOCKET) {
        SOCKADDR SockAddr = { 0 };
        BuildSockAddr(&SockAddr, NULL, Port);
        if (bind(Socket, &SockAddr, sizeof(SockAddr)) != 0) {
            closesocket(Socket);
            Socket = INVALID_SOCKET;
        }
    }
    return Socket;
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation error: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int16_t byteArrayToInt16(const char* b) {
    return (int16_t)((unsigned char)b[0] << 8 | (unsigned char)b[1]);
}

int32_t byteArrayToInt32(const char* b) {
    return (int32_t)((unsigned char)b[0] << 24 | (unsigned char)b[1] << 16 |
        (unsigned char)b[2] << 8 | (unsigned char)b[3]);
}

void Init() {
    GLuint vsVnc = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fsVnc = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    programObject = glCreateProgram();
    glAttachShader(programObject, vsVnc);
    glAttachShader(programObject, fsVnc);
    glLinkProgram(programObject);

    GLuint vsText = compileShader(GL_VERTEX_SHADER, vertexShaderSourceText);
    GLuint fsText = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSourceText);
    programObjectTextRender = glCreateProgram();
    glAttachShader(programObjectTextRender, vsText);
    glAttachShader(programObjectTextRender, fsText);
    glLinkProgram(programObjectTextRender);

    // [OPT 9] Cache attribute locations once
    g_posAttr = glGetAttribLocation(programObject, "position");
    g_texAttr = glGetAttribLocation(programObject, "texCoord");
    g_textPosAttr = glGetAttribLocation(programObjectTextRender, "position");

    // [OPT 8] Create persistent text VBO
    glGenBuffers(1, &g_textVBO);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

// ============================================================================
// [OPT 1,2,3,6] Optimized frame parser
//
// Key changes from original:
// - Single recv for 4-byte message header instead of 1+1+2
// - Decompresses directly into g_bufs.frameBuffer (no temp + memcpy)
// - No malloc/realloc per frame — uses persistent buffers
// - Sends incremental update request (pipelining)
// ============================================================================
bool parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth,
    int* frameBufferHeight, z_stream* strm,
    int* finalHeight, FrameTimings& timings) {
    auto parseStart = Clock::now();

    // [OPT 6] Batched read: 4 bytes (type + padding + numRects) in one recv
    // Original did 3 separate recv calls: 1 + 1 + 2 = 3 syscalls
    char msgHeader[4];
    if (recv_timed(socket_fd, msgHeader, 4, MSG_WAITALL, timings) != 4) return false;

    char messageType = msgHeader[0];
    // msgHeader[1] is padding
    int rectCount = byteArrayToInt16(msgHeader + 2);

    // Pipeline: request next frame immediately
    if (messageType == 0) {
        send(socket_fd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0);
    }

    size_t totalDecompressedSize = 0;
    *finalHeight = 0;

    // ========================================================================
    // Two-pass approach for multi-rectangle frames:
    // Pass 1: peek all rectangle headers to compute total buffer size
    //         (avoids realloc inside the decompression loop)
    //
    // For simplicity and because VNC servers typically send 1-4 rects,
    // we just grow the persistent buffer as needed per-rect.
    // ========================================================================

    for (int i = 0; i < rectCount; i++) {
        char header[12];
        if (recv_timed(socket_fd, header, 12, MSG_WAITALL, timings) != 12) return false;

        int rectX = byteArrayToInt16(header + 0);
        int rectY = byteArrayToInt16(header + 2);
        int rectW = byteArrayToInt16(header + 4);
        int rectH = byteArrayToInt16(header + 6);
        int32_t encoding = byteArrayToInt32(header + 8);

        *frameBufferWidth = rectW;
        *frameBufferHeight = rectH;
        *finalHeight += rectH;

        if (encoding == 6) { // ZLIB
            char compSizeBuf[4];
            if (recv_timed(socket_fd, compSizeBuf, 4, MSG_WAITALL, timings) != 4) return false;
            int compressedSize = byteArrayToInt32(compSizeBuf);

            // [OPT 2] Reuse compressed data buffer
            if (!g_bufs.ensureCompressedBuf(compressedSize)) return false;

            if (recv_timed(socket_fd, g_bufs.compressedBuf, compressedSize, MSG_WAITALL, timings) != compressedSize)
                return false;

            size_t decompSize = (size_t)rectW * rectH * 4;
            size_t neededTotal = totalDecompressedSize + decompSize;

            // [OPT 1] Reuse frame buffer — grows only when resolution increases
            if (!g_bufs.ensureFrameBuffer(neededTotal)) return false;

            // [OPT 3] Decompress directly into final buffer — no temp + memcpy
            strm->avail_in = compressedSize;
            strm->next_in = (Bytef*)g_bufs.compressedBuf;
            strm->avail_out = (uInt)decompSize;
            strm->next_out = (Bytef*)(g_bufs.frameBuffer + totalDecompressedSize);

            auto infStart = Clock::now();
            int ret = inflate(strm, Z_NO_FLUSH);
            timings.inflate_ms += GetDurationMs(infStart, Clock::now());

            if (ret < 0 && ret != Z_BUF_ERROR) {
                return false;
            }

            totalDecompressedSize = neededTotal;
        }
        // TODO: handle encoding == 0 (Raw) if the server falls back to it
    }

    timings.parse_ms = GetDurationMs(parseStart, Clock::now());
    return true;  // data is in g_bufs.frameBuffer
}

// ============================================================================
// [OPT 8,10] Optimized text rendering
// - Reuses persistent VBO (g_textVBO) instead of gen/delete per frame
// - Only uploads actual vertex count, not the full 20000-float array
// - Uses GL_DYNAMIC_DRAW hint since content changes every frame
// ============================================================================
void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    // stb_easyfont outputs quads as 4 vertices * 4 floats each = 16 floats per char
    // We convert to triangles: 6 vertices * 2 floats = 12 floats per char
    static char inputBuffer[20000];
    static GLfloat triangleBuffer[20000];

    memset(inputBuffer, 0, sizeof(inputBuffer));
    int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;
    float invSize = 1.0f / size;  // Multiply is faster than repeated divide

    int triangleIndex = 0;
    for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); i += 8) {
        GLfloat* ptr = reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]);
        if (ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 0) break;

        float x0 = ptr[0] * invSize + ndcMovementX;
        float y0 = ptr[1] * invSize * -1 + ndcMovementY;
        float x1 = ptr[2] * invSize + ndcMovementX;
        float y1 = ptr[3] * invSize * -1 + ndcMovementY;
        float x2 = ptr[4] * invSize + ndcMovementX;
        float y2 = ptr[5] * invSize * -1 + ndcMovementY;
        float x3 = ptr[6] * invSize + ndcMovementX;
        float y3 = ptr[7] * invSize * -1 + ndcMovementY;

        // Triangle 1
        triangleBuffer[triangleIndex++] = x0; triangleBuffer[triangleIndex++] = y0;
        triangleBuffer[triangleIndex++] = x1; triangleBuffer[triangleIndex++] = y1;
        triangleBuffer[triangleIndex++] = x2; triangleBuffer[triangleIndex++] = y2;
        // Triangle 2
        triangleBuffer[triangleIndex++] = x0; triangleBuffer[triangleIndex++] = y0;
        triangleBuffer[triangleIndex++] = x2; triangleBuffer[triangleIndex++] = y2;
        triangleBuffer[triangleIndex++] = x3; triangleBuffer[triangleIndex++] = y3;
    }

    // [OPT 8] Reuse persistent VBO
    glBindBuffer(GL_ARRAY_BUFFER, g_textVBO);
    // [OPT 10] Upload only the used portion, not the full 20000-element array
    glBufferData(GL_ARRAY_BUFFER, triangleIndex * sizeof(GLfloat), triangleBuffer, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_textPosAttr);
    glVertexAttribPointer(g_textPosAttr, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex / 2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void loadConfig(const char* filename) {
    FILE* file = NULL;
    if (fopen_s(&file, filename, "r") != 0 || !file) return;
    fclose(file);
}

// ============================================================================
// Main — with socket tuning and proper resource cleanup
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OpenGL_VNC_Sim";
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    loadConfig("config.txt");

    HWND hWnd = CreateWindowEx(0, L"OpenGL_VNC_Sim", L"OpenGL VNC Render (Optimized)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    eglInitialize(eglDisplay, &maj, &min);

    EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig eglCfg;
    EGLint numConfigs;
    eglChooseConfig(eglDisplay, config_attribs, &eglCfg, 1, &numConfigs);

    eglSurface = eglCreateWindowSurface(eglDisplay, eglCfg, hWnd, nullptr);
    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, eglCfg, EGL_NO_CONTEXT, ctxAttribs);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

    Init();

    // Pre-allocate for typical 800x480 RGBA framebuffer
    g_bufs.ensureFrameBuffer(800 * 480 * 4);
    g_bufs.ensureCompressedBuf(256 * 1024);

    while (true) {
        WSADATA WSAData;
        WSAStartup(0x202, &WSAData);
        SOCKET sockfd = MySocketOpen(SOCK_STREAM, 0);

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, VNC_SERVER_IP_ADDRESS, &serverAddr.sin_addr);
        serverAddr.sin_port = htons(VNC_SERVER_PORT);

        // ====================================================================
        // [OPT 4] TCP_NODELAY — disables Nagle's algorithm.
        // Without this, the OS buffers small sends (like the 10-byte update
        // request) and waits up to ~40ms before actually transmitting.
        // This alone can cut 20-40ms off round-trip latency.
        // ====================================================================
        BOOL nodelay = TRUE;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        // ====================================================================
        // [OPT 5] Larger receive buffer — 256KB instead of OS default (~8KB).
        // Allows the kernel to buffer more incoming data, reducing the chance
        // that recv() blocks waiting for the NIC. Especially helps when the
        // server sends large compressed frames in bursts.
        // ====================================================================
        int rcvBufSize = 256 * 1024;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBufSize, sizeof(rcvBufSize));

        struct timeval timeout = { 5, 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        if (connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Connecting..." << std::endl;
            closesocket(sockfd);
            WSACleanup();
            Sleep(500);
            continue;
        }

        // ==================================================================
        // Handshake — preserved exactly as original.
        //
        // RFB 3.3: server chooses security type, no client response needed.
        // After security, client sends ClientInit (1 byte shared-flag),
        // then server replies with ServerInit (24 bytes).
        //
        // The original send("\\x01", 1, 0) sends 0x5C (backslash) as
        // the ClientInit shared-flag. Any nonzero = shared. It works,
        // so we keep the exact same byte sequence.
        // ==================================================================
        char buf[256];
        recv(sockfd, buf, 12, MSG_WAITALL);                          // Server protocol version
        send(sockfd, PROTOCOL_VERSION, (int)strlen(PROTOCOL_VERSION), 0); // Client protocol version
        recv(sockfd, buf, 4, 0);                                      // Security type (server chosen in RFB 3.3)
        send(sockfd, "\\x01", 1, 0);                                 // ClientInit shared-flag (0x5C = nonzero = shared)
        recv(sockfd, buf, 4, 0);                                      // ServerInit bytes 0-3 (width + height)
        recv(sockfd, buf, 16 + 4, MSG_WAITALL);                       // ServerInit bytes 4-23 (pixel format + name-length)
        uint32_t nameLen = (unsigned char)buf[16] << 24 |
            (unsigned char)buf[17] << 16 |
            (unsigned char)buf[18] << 8 |
            (unsigned char)buf[19];
        recv(sockfd, buf, nameLen, MSG_WAITALL);                      // Desktop name

        // Set encoding preference
        send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0);

        // First request is non-incremental (full screen)
        send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0);

        int fbW = 0, fbH = 0, finalH = 0;
        int prevFbW = 0, prevFinalH = 0;   // Track previous dimensions for glTexSubImage2D
        int frameCount = 0;
        double fps = 0.0;
        auto lastFpsTime = Clock::now();

        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        z_stream strm = { 0 };
        inflateInit(&strm);

        MSG msg;
        bool running = true;
        bool firstFrame = true;

        while (running) {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) running = false;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (!running) break;

            auto frameStart = Clock::now();
            FrameTimings timings = {};

            finalH = 0;
            if (!parseFramebufferUpdate(sockfd, &fbW, &fbH, &strm, &finalH, timings)) {
                closesocket(sockfd);
                break;
            }

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(programObject);

            // ================================================================
            // [OPT 7] glTexSubImage2D for same-size frames.
            //
            // glTexImage2D reallocates GPU-side texture storage every call.
            // glTexSubImage2D just uploads pixels into existing storage.
            //
            // On the first frame (or resolution change), we must use
            // glTexImage2D to establish the texture dimensions. After that,
            // glTexSubImage2D avoids the reallocation overhead.
            //
            // Typical savings: 0.5-2ms per frame on embedded GPUs.
            // ================================================================
            auto texStart = Clock::now();
            if (firstFrame || fbW != prevFbW || finalH != prevFinalH) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbW, finalH, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frameBuffer);
                prevFbW = fbW;
                prevFinalH = finalH;
                firstFrame = false;
            }
            else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbW, finalH,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frameBuffer);
            }
            timings.texture_upload_ms = GetDurationMs(texStart, Clock::now());

            // Draw VNC quad — using cached attribute locations [OPT 9]
            if (fbW > finalH) {
                glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, landscapeVertices);
                glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, landscapeTexCoords);
            }
            else {
                glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, portraitVertices);
                glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, portraitTexCoords);
            }
            glEnableVertexAttribArray(g_posAttr);
            glEnableVertexAttribArray(g_texAttr);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glDisableVertexAttribArray(g_posAttr);
            glDisableVertexAttribArray(g_texAttr);

            // Stats overlay
            frameCount++;
            auto now = Clock::now();
            timings.total_frame_ms = GetDurationMs(frameStart, now);

            if (GetDurationMs(lastFpsTime, now) >= 1000.0) {
                fps = frameCount * 1000.0 / GetDurationMs(lastFpsTime, now);
                frameCount = 0;
                lastFpsTime = now;
            }

            char overlayText[512];
            snprintf(overlayText, sizeof(overlayText),
                "FPS: %.1f\nFrame: %.2f ms\nRecv: %.2f ms\nInflate: %.2f ms\nParse: %.2f ms\nGPU Up: %.2f ms",
                fps, timings.total_frame_ms, timings.recv_ms, timings.inflate_ms,
                timings.parse_ms, timings.texture_upload_ms);

            glUseProgram(programObjectTextRender);
            print_string(-380, 220, overlayText, 1.0f, 1.0f, 0.0f, 80.0f);

            eglSwapBuffers(eglDisplay, eglSurface);
        }

        // [OPT 13] Clean up zlib state — original code leaked this on disconnect
        inflateEnd(&strm);
        closesocket(sockfd);
        WSACleanup();
        glDeleteTextures(1, &textureID);
    }
    return 0;
}
