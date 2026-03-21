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
    static float inBuf[5000]; // 20000 bytes, naturally aligned for floats
    static float triBuf[30000];

    // stb_easy_font_print returns the exact number of quads generated
    int quads = stb_easy_font_print(0, 0, (char*)text, nullptr, inBuf, sizeof(inBuf));

    float ndcX = (2.0f * x) / windowWidth, ndcY = (2.0f * y) / windowHeight;
    float invS = 1.0f / size, negInvS = -invS; // Precompute negation
    int tIdx = 0;

    for (int i = 0; i < quads; i++) {
        float* p = &inBuf[i * 8]; // 8 floats per quad
        float x0 = p[0] * invS + ndcX, y0 = p[1] * negInvS + ndcY;
        float x1 = p[2] * invS + ndcX, y1 = p[3] * negInvS + ndcY;
        float x2 = p[4] * invS + ndcX, y2 = p[5] * negInvS + ndcY;
        float x3 = p[6] * invS + ndcX, y3 = p[7] * negInvS + ndcY;

        // Push 2 triangles (6 vertices = 12 floats) directly
        float verts[] = { x0, y0, x1, y1, x2, y2, x0, y0, x2, y2, x3, y3 };
        memcpy(&triBuf[tIdx], verts, sizeof(verts));
        tIdx += 12;
    }

    glBindBuffer(GL_ARRAY_BUFFER, g_textVBO);
    glBufferData(GL_ARRAY_BUFFER, tIdx * sizeof(float), triBuf, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_textPosAttr);
    glVertexAttribPointer(g_textPosAttr, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLES, 0, tIdx / 2);
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
    EGLint maj, min, numConfigs;
    eglInitialize(eglDisplay, &maj, &min);

    EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig eglCfg;
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

    std::thread networkThread(NetworkThreadFunc);

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int prevFbW = 0, prevFinalH = 0;
    int frameCount = 0, vnc_frame_count = 0;
    double sum_total_ms = 0, sum_tex_ms = 0, sum_recv_ms = 0, sum_inflate_ms = 0, sum_parse_ms = 0;
    char overlayText[512] = { 0 };

    auto lastFpsTime = Clock::now();
    uint64_t lastVncDecoded = 0, lastBytesReceived = 0;

    MSG msg;
    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        auto frameStart = Clock::now();
        int renderFbW = prevFbW, renderFinalH = prevFinalH;

        // --- Frame Update & Upload ---
        if (g_newFrameReady) {
            std::lock_guard<std::mutex> lock(g_frameMutex);
            renderFbW = g_sharedFbW;
            renderFinalH = g_sharedFinalH;

            auto texStart = Clock::now();
            if (!prevFbW || renderFbW != prevFbW || renderFinalH != prevFinalH) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderFbW, renderFinalH, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
                prevFbW = renderFbW;
                prevFinalH = renderFinalH;
            }
            else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, renderFbW, renderFinalH, GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
            }

            sum_tex_ms += GetDurationMs(texStart, Clock::now());
            sum_recv_ms += g_sharedTimings.recv_ms;
            sum_inflate_ms += g_sharedTimings.inflate_ms;
            sum_parse_ms += g_sharedTimings.parse_ms;
            vnc_frame_count++;
            g_newFrameReady = false;
        }

        // --- Rendering ---
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(programObject);

        const float* verts = (renderFbW > renderFinalH) ? landscapeVertices : portraitVertices;
        const float* texs = (renderFbW > renderFinalH) ? landscapeTexCoords : portraitTexCoords;

        glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, verts);
        glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, texs);
        glEnableVertexAttribArray(g_posAttr);
        glEnableVertexAttribArray(g_texAttr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableVertexAttribArray(g_posAttr);
        glDisableVertexAttribArray(g_texAttr);

        // --- Stats & 1-Second Updates ---
        frameCount++;
        auto now = Clock::now();
        sum_total_ms += GetDurationMs(frameStart, now);
        double elapsedMs = GetDurationMs(lastFpsTime, now);

        if (elapsedMs >= 1000.0) {
            double elapsedSec = elapsedMs / 1000.0;
            uint64_t curDecoded = g_vncFramesDecoded.load();
            uint64_t curBytes = g_totalBytesReceived.load();

            double fps = frameCount / elapsedSec;
            double vncFps = (curDecoded - lastVncDecoded) / elapsedSec;
            double bwKbps = ((curBytes - lastBytesReceived) / 1024.0) / elapsedSec;
            double bwMbps = ((curBytes - lastBytesReceived) * 8.0 / 1000000.0) / elapsedSec;

            double avg_frame = frameCount ? sum_total_ms / frameCount : 0.0;
            double avg_tex = vnc_frame_count ? sum_tex_ms / vnc_frame_count : 0.0;
            double avg_recv = vnc_frame_count ? sum_recv_ms / vnc_frame_count : 0.0;
            double avg_inf = vnc_frame_count ? sum_inflate_ms / vnc_frame_count : 0.0;
            double avg_pars = vnc_frame_count ? sum_parse_ms / vnc_frame_count : 0.0;

            snprintf(overlayText, sizeof(overlayText),
                "Render FPS: %.1f\nVNC FPS: %.1f\nBandwidth: %.2f KB/s (%.2f Mbps)\n"
                "Frame (Avg): %.2f ms\nRecv (Avg): %.2f ms\nInflate (Avg): %.2f ms\n"
                "Parse (Avg): %.2f ms\nGPU Up (Avg): %.2f ms",
                fps, vncFps, bwKbps, bwMbps, avg_frame, avg_recv, avg_inf, avg_pars, avg_tex);

            lastFpsTime = now;
            lastVncDecoded = curDecoded;
            lastBytesReceived = curBytes;
            frameCount = vnc_frame_count = 0;
            sum_total_ms = sum_tex_ms = sum_recv_ms = sum_inflate_ms = sum_parse_ms = 0.0;
        }

        glUseProgram(programObjectTextRender);
        if (overlayText[0]) print_string(-380, 240, overlayText, 1.0f, 1.0f, 0.0f, 80.0f);

        eglSwapBuffers(eglDisplay, eglSurface);
        if (!g_newFrameReady) Sleep(1);
    }

    networkThread.join();
    WSACleanup();
    glDeleteTextures(1, &textureID);
    return 0;
}
