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
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>

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

// --- Timing & Bandwidth ---
using Clock = std::chrono::high_resolution_clock;

struct FrameTimings {
    double recv_ms = 0.0;
    double inflate_ms = 0.0;
    double parse_ms = 0.0;
    double texture_upload_ms = 0.0;
    double total_frame_ms = 0.0;
};

// Tracks total bytes received for bandwidth calculations
std::atomic<uint64_t> g_totalBytesReceived{ 0 };

static double GetDurationMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int recv_timed(SOCKET s, char* buf, int len, int flags, FrameTimings& timings) {
    auto start = Clock::now();
    int result = recv(s, buf, len, flags);
    timings.recv_ms += GetDurationMs(start, Clock::now());
    if (result > 0) {
        g_totalBytesReceived += result;
    }
    return result;
}

// ============================================================================
// Double Buffered Persistent Memory
// ============================================================================
struct PersistentBuffers {
    char* frontBuffer;      // Render thread reads from here
    char* backBuffer;       // Network thread writes to here
    size_t frameBufferCap;
    char* compressedBuf;
    size_t compressedCap;

    PersistentBuffers() : frontBuffer(nullptr), backBuffer(nullptr), frameBufferCap(0),
        compressedBuf(nullptr), compressedCap(0) {
    }

    ~PersistentBuffers() {
        free(frontBuffer);
        free(backBuffer);
        free(compressedBuf);
    }

    bool ensureFrameBuffers(size_t needed) {
        if (needed > frameBufferCap) {
            size_t newCap = (std::max)(needed, frameBufferCap * 2);
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

// --- Threading & Sync Globals ---
std::mutex g_frameMutex;
std::atomic<bool> g_newFrameReady{ false };
std::atomic<bool> g_running{ true };
SOCKET g_currentSocket = INVALID_SOCKET;

// Shared state updated by Network Thread, read by Render Thread
int g_sharedFbW = 0;
int g_sharedFbH = 0;
int g_sharedFinalH = 0;
FrameTimings g_sharedTimings;

// Tracks total frames decoded over the lifetime of the app
std::atomic<uint64_t> g_vncFramesDecoded{ 0 };

// --- GLES globals ---
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
const char FRAMEBUFFER_UPDATE_REQUEST[] = { 3,0,0,0,0,0,255,255,255,255 };
const char ZLIB_ENCODING[] = { 2,0,0,2, 0,0,0,6, 0,0,0,0 };

// --- Setup ---
int windowWidth = 800;
int windowHeight = 480;

const char* VNC_SERVER_IP_ADDRESS = "192.168.1.112";
const int VNC_SERVER_PORT = 5900;

// --- Windows Helpers ---
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DESTROY:
        g_running = false; // Signal threads to exit
        if (g_currentSocket != INVALID_SOCKET) {
            closesocket(g_currentSocket); // Force recv() to unblock
        }
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

    g_posAttr = glGetAttribLocation(programObject, "position");
    g_texAttr = glGetAttribLocation(programObject, "texCoord");
    g_textPosAttr = glGetAttribLocation(programObjectTextRender, "position");

    glGenBuffers(1, &g_textVBO);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

// ============================================================================
// Network Parser - Writes explicitly to the BACK BUFFER
// ============================================================================
bool parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth,
    int* frameBufferHeight, z_stream* strm,
    int* finalHeight, FrameTimings& timings) {
    auto parseStart = Clock::now();

    char msgHeader[4];
    if (recv_timed(socket_fd, msgHeader, 4, MSG_WAITALL, timings) != 4) return false;

    char messageType = msgHeader[0];
    int rectCount = byteArrayToInt16(msgHeader + 2);

    if (messageType == 0) {
        send(socket_fd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0);
    }

    size_t totalDecompressedSize = 0;
    *finalHeight = 0;

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

            if (!g_bufs.ensureCompressedBuf(compressedSize)) return false;

            if (recv_timed(socket_fd, g_bufs.compressedBuf, compressedSize, MSG_WAITALL, timings) != compressedSize)
                return false;

            size_t decompSize = (size_t)rectW * rectH * 4;
            size_t neededTotal = totalDecompressedSize + decompSize;

            if (!g_bufs.ensureFrameBuffers(neededTotal)) return false;

            strm->avail_in = compressedSize;
            strm->next_in = (Bytef*)g_bufs.compressedBuf;
            strm->avail_out = (uInt)decompSize;

            // DECOMPRESS TO BACK BUFFER
            strm->next_out = (Bytef*)(g_bufs.backBuffer + totalDecompressedSize);

            auto infStart = Clock::now();
            int ret = inflate(strm, Z_NO_FLUSH);
            timings.inflate_ms += GetDurationMs(infStart, Clock::now());

            if (ret < 0 && ret != Z_BUF_ERROR) return false;

            totalDecompressedSize = neededTotal;
        }
    }

    timings.parse_ms = GetDurationMs(parseStart, Clock::now());
    return true;
}

// Helper wrapper for initial handshake recvs to also count bytes
int recv_and_count(SOCKET s, char* buf, int len, int flags) {
    int res = recv(s, buf, len, flags);
    if (res > 0) g_totalBytesReceived += res;
    return res;
}

// ============================================================================
// NETWORK THREAD: Handles all blocking socket I/O & Decompression
// ============================================================================
void NetworkThreadFunc() {
    while (g_running) {
        SOCKET sockfd = MySocketOpen(SOCK_STREAM, 0);
        g_currentSocket = sockfd;

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, VNC_SERVER_IP_ADDRESS, &serverAddr.sin_addr);
        serverAddr.sin_port = htons(VNC_SERVER_PORT);

        BOOL nodelay = TRUE;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        int rcvBufSize = 256 * 1024;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBufSize, sizeof(rcvBufSize));

        struct timeval timeout = { 5, 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        auto abort_connection = [&]() {
            closesocket(sockfd);
            g_currentSocket = INVALID_SOCKET;
            };

        if (connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            abort_connection();
            Sleep(500);
            continue;
        }

        // Handshake
        char buf[256];
        if (recv_and_count(sockfd, buf, 12, MSG_WAITALL) <= 0) { abort_connection(); continue; }
        send(sockfd, PROTOCOL_VERSION, (int)strlen(PROTOCOL_VERSION), 0);

        if (recv_and_count(sockfd, buf, 4, 0) <= 0) { abort_connection(); continue; }
        send(sockfd, "\x01", 1, 0);

        if (recv_and_count(sockfd, buf, 4, 0) <= 0) { abort_connection(); continue; }
        if (recv_and_count(sockfd, buf, 20, MSG_WAITALL) <= 0) { abort_connection(); continue; }

        uint32_t nameLen = (unsigned char)buf[16] << 24 | (unsigned char)buf[17] << 16 |
            (unsigned char)buf[18] << 8 | (unsigned char)buf[19];

        if (recv_and_count(sockfd, buf, nameLen, MSG_WAITALL) <= 0) { abort_connection(); continue; }

        send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0);
        send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0);

        z_stream strm = { 0 };
        inflateInit(&strm);

        while (g_running) {
            int fbW = 0, fbH = 0, finalH = 0;
            FrameTimings localTimings = {};

            if (!parseFramebufferUpdate(sockfd, &fbW, &fbH, &strm, &finalH, localTimings)) {
                inflateEnd(&strm);
                break; // Disconnected or error, break inner loop to reconnect
            }

            g_vncFramesDecoded++; // Increment total decoded frames

            // Sync with Render Thread: Swap Front/Back Buffers
            {
                std::lock_guard<std::mutex> lock(g_frameMutex);
                std::swap(g_bufs.frontBuffer, g_bufs.backBuffer);
                g_sharedFbW = fbW;
                g_sharedFbH = fbH;
                g_sharedFinalH = finalH;
                g_sharedTimings = localTimings;
                g_newFrameReady = true;
            }
        }

        abort_connection();
    }
}

// Text Rendering...
void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    static char inputBuffer[20000];
    static GLfloat triangleBuffer[20000];

    memset(inputBuffer, 0, sizeof(inputBuffer));
    int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;
    float invSize = 1.0f / size;

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

        triangleBuffer[triangleIndex++] = x0; triangleBuffer[triangleIndex++] = y0;
        triangleBuffer[triangleIndex++] = x1; triangleBuffer[triangleIndex++] = y1;
        triangleBuffer[triangleIndex++] = x2; triangleBuffer[triangleIndex++] = y2;
        triangleBuffer[triangleIndex++] = x0; triangleBuffer[triangleIndex++] = y0;
        triangleBuffer[triangleIndex++] = x2; triangleBuffer[triangleIndex++] = y2;
        triangleBuffer[triangleIndex++] = x3; triangleBuffer[triangleIndex++] = y3;
    }

    glBindBuffer(GL_ARRAY_BUFFER, g_textVBO);
    glBufferData(GL_ARRAY_BUFFER, triangleIndex * sizeof(GLfloat), triangleBuffer, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_textPosAttr);
    glVertexAttribPointer(g_textPosAttr, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex / 2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


// ============================================================================
// MAIN / RENDER THREAD: OpenGL context strictly remains on the UI thread
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OpenGL_VNC_Sim";
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(0, L"OpenGL_VNC_Sim", L"OpenGL VNC Render (Multithreaded)",
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

    g_bufs.ensureFrameBuffers(800 * 480 * 4);
    g_bufs.ensureCompressedBuf(256 * 1024);

    WSADATA WSAData;
    WSAStartup(0x202, &WSAData);

    // Launch Network Thread
    std::thread networkThread(NetworkThreadFunc);

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int prevFbW = 0, prevFinalH = 0;
    bool firstFrame = true;
    int frameCount = 0;
    double fps = 0.0;
    auto lastFpsTime = Clock::now();

    // Sliding Window State for VNC FPS Calculation
    std::deque<std::chrono::time_point<Clock>> vncFrameTimes;
    uint64_t lastVncDecodedCount = 0;

    FrameTimings displayTimings = {}; // Holds timings for current frame

    // Accumulators and 1-second Average Variables
    int vnc_frame_count = 0;
    double sum_total_frame_ms = 0.0, avg_total_frame_ms = 0.0;
    double sum_texture_up_ms = 0.0, avg_texture_up_ms = 0.0;
    double sum_recv_ms = 0.0, avg_recv_ms = 0.0;
    double sum_inflate_ms = 0.0, avg_inflate_ms = 0.0;
    double sum_parse_ms = 0.0, avg_parse_ms = 0.0;

    // Bandwidth State
    uint64_t lastBytesReceived = 0;
    double current_bandwidth_kbps = 0.0;
    double current_bandwidth_mbps = 0.0;

    MSG msg;

    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        auto frameStart = Clock::now();
        double textureUploadMs = 0.0;
        int renderFbW = 0, renderFinalH = 0;

        // --- Calculate Sliding Window VNC FPS ---
        uint64_t currentDecoded = g_vncFramesDecoded.load();
        uint64_t newFrames = currentDecoded - lastVncDecodedCount;
        lastVncDecodedCount = currentDecoded;

        auto currentLoopTime = Clock::now();

        for (uint64_t i = 0; i < newFrames; i++) {
            vncFrameTimes.push_back(currentLoopTime);
        }

        while (!vncFrameTimes.empty() && GetDurationMs(vncFrameTimes.front(), currentLoopTime) > 1000.0) {
            vncFrameTimes.pop_front();
        }

        double renderVncFps = static_cast<double>(vncFrameTimes.size());

        // --- Critical Section: Check for new frame and upload ---
        if (g_newFrameReady) {
            std::lock_guard<std::mutex> lock(g_frameMutex);

            renderFbW = g_sharedFbW;
            renderFinalH = g_sharedFinalH;
            displayTimings = g_sharedTimings;

            auto texStart = Clock::now();
            if (firstFrame || renderFbW != prevFbW || renderFinalH != prevFinalH) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderFbW, renderFinalH, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
                prevFbW = renderFbW;
                prevFinalH = renderFinalH;
                firstFrame = false;
            }
            else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, renderFbW, renderFinalH,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
            }
            textureUploadMs = GetDurationMs(texStart, Clock::now());

            // Accumulate VNC & Texture Upload stats for the 1-sec average
            sum_texture_up_ms += textureUploadMs;
            sum_recv_ms += displayTimings.recv_ms;
            sum_inflate_ms += displayTimings.inflate_ms;
            sum_parse_ms += displayTimings.parse_ms;
            vnc_frame_count++;

            g_newFrameReady = false;
        }
        else {
            renderFbW = prevFbW;
            renderFinalH = prevFinalH;
        }

        // --- Rendering ---
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(programObject);

        if (renderFbW > renderFinalH) {
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

        // --- Stats ---
        frameCount++;
        auto now = Clock::now();

        // Accumulate Render Loop stats
        double current_total_frame_ms = GetDurationMs(frameStart, now);
        sum_total_frame_ms += current_total_frame_ms;

        // --- 1-Second Update Block ---
        double timeSinceLastFps = GetDurationMs(lastFpsTime, now);
        if (timeSinceLastFps >= 1000.0) {
            fps = frameCount * 1000.0 / timeSinceLastFps;

            // Compute Bandwidth
            uint64_t currentTotalBytes = g_totalBytesReceived.load();
            uint64_t bytesThisSecond = currentTotalBytes - lastBytesReceived;
            lastBytesReceived = currentTotalBytes;

            double actualDurationSec = timeSinceLastFps / 1000.0;
            current_bandwidth_kbps = (bytesThisSecond / 1024.0) / actualDurationSec;
            current_bandwidth_mbps = ((bytesThisSecond * 8.0) / 1000000.0) / actualDurationSec; // Megabits

            // Compute Averages
            avg_total_frame_ms = frameCount > 0 ? (sum_total_frame_ms / frameCount) : 0.0;

            if (vnc_frame_count > 0) {
                avg_texture_up_ms = sum_texture_up_ms / vnc_frame_count;
                avg_recv_ms = sum_recv_ms / vnc_frame_count;
                avg_inflate_ms = sum_inflate_ms / vnc_frame_count;
                avg_parse_ms = sum_parse_ms / vnc_frame_count;
            }
            else {
                avg_texture_up_ms = 0.0;
                avg_recv_ms = 0.0;
                avg_inflate_ms = 0.0;
                avg_parse_ms = 0.0;
            }

            // Reset accumulators for the next second
            frameCount = 0;
            vnc_frame_count = 0;
            sum_total_frame_ms = 0.0;
            sum_texture_up_ms = 0.0;
            sum_recv_ms = 0.0;
            sum_inflate_ms = 0.0;
            sum_parse_ms = 0.0;

            lastFpsTime = now;
        }

        char overlayText[1024];
        snprintf(overlayText, sizeof(overlayText),
            "Render FPS: %.1f\n"
            "VNC FPS: %.1f\n"
            "Bandwidth: %.2f KB/s (%.2f Mbps)\n"
            "Frame (Avg): %.2f ms\n"
            "Recv (Avg): %.2f ms\n"
            "Inflate (Avg): %.2f ms\n"
            "Parse (Avg): %.2f ms\n"
            "GPU Up (Avg): %.2f ms",
            fps, renderVncFps, current_bandwidth_kbps, current_bandwidth_mbps,
            avg_total_frame_ms, avg_recv_ms, avg_inflate_ms, avg_parse_ms, avg_texture_up_ms);

        glUseProgram(programObjectTextRender);
        print_string(-380, 240, overlayText, 1.0f, 1.0f, 0.0f, 80.0f);

        eglSwapBuffers(eglDisplay, eglSurface);

        if (!g_newFrameReady) {
            Sleep(1);
        }
    }

    networkThread.join();

    WSACleanup();
    glDeleteTextures(1, &textureID);
    return 0;
}
