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
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_easyfont.h"
#include <chrono>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <fcntl.h>
#include "miniz.h"
#include <time.h>

#pragma comment(lib,"Ws2_32.lib")

// GLES setup
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

// Vertex shader
const char* vertexShaderSource =
"#version 100\n"
"attribute vec2 position;\n"
"attribute vec2 texCoord;\n"
"varying vec2 v_texCoord;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(position, 0.0, 1.0);\n"
"   v_texCoord = texCoord;\n"
"   gl_PointSize = 4.0;\n"
"}\n";

// Fragment shader - GRAYSCALE
const char* fragmentShaderSource =
"#version 100\n"
"precision highp float;\n"
"varying vec2 v_texCoord;\n"
"uniform sampler2D texture;\n"
"void main()\n"
"{\n"
"    vec4 color = texture2D(texture, v_texCoord);\n"
"    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
"    gl_FragColor = vec4(vec3(gray), 1.0);\n"
"}\n";

// Text Rendering shaders
const char* vertexShaderSourceText =
"attribute vec2 position;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(position, 0.0, 1.0);\n"
"   gl_PointSize = 4.0;\n"
"}\n";

const char* fragmentShaderSourceText =
"void main()\n"
"{\n"
"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
"}\n";

// Vertex and texture coordinates
GLfloat landscapeVertices[] = { -0.8f, 0.73, 0.0f, 0.8f, 0.73f, 0.0f, 0.8f, -0.63f, 0.0f, -0.8f, -0.63f, 0.0f };
GLfloat portraitVertices[] = { -0.8f, 1.0f, 0.0f, 0.8f, 1.0f, 0.0f, 0.8f, -0.67f, 0.0f, -0.8f, -0.67f, 0.0f };
GLfloat landscapeTexCoords[] = { 0.0f, 0.07f, 0.9f, 0.07f, 0.9f, 1.0f, 0.0f, 1.0f };
GLfloat portraitTexCoords[] = { 0.0f, 0.0f, 0.63f, 0.0f, 0.63f, 0.2f, 0.0f, 0.2f };

// VNC protocol constants
const char* PROTOCOL_VERSION = "RFB 003.003\n";
const char FRAMEBUFFER_UPDATE_REQUEST[] = {3,0,0,0,0,0,255,255,255,255};
const char CLIENT_INIT[] = {1};
const char ZLIB_ENCODING[] = {2,0,0,2,0,0,0,6,0,0,0,0};

int windowWidth = 800;
int windowHeight = 480;
const char* VNC_SERVER_IP_ADDRESS = "192.168.1.190";
const int VNC_SERVER_PORT = 5900;

// Windows sleep
static void usleep(__int64 usec) {
    HANDLE timer;
    LARGE_INTEGER ft;
    ft.QuadPart = -(10 * usec);
    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

// Window callback
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) PostQuitMessage(0);
    else return DefWindowProc(hWnd, message, wParam, lParam);
    return 0;
}

// Socket helpers
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

// Shader compilation
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

// OpenGL ES initialization
void Init() {
    GLuint vShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint vShaderText = compileShader(GL_VERTEX_SHADER, vertexShaderSourceText);
    GLuint fShaderText = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSourceText);

    programObject = glCreateProgram();
    glAttachShader(programObject, vShader);
    glAttachShader(programObject, fShader);
    glLinkProgram(programObject);

    programObjectTextRender = glCreateProgram();
    glAttachShader(programObjectTextRender, vShaderText);
    glAttachShader(programObjectTextRender, fShaderText);
    glLinkProgram(programObjectTextRender);

    glClearColor(0, 0, 0, 1);
}

// Helper to convert bytes
int16_t byteArrayToInt16(const char* byteArray) { return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF); }
int32_t byteArrayToInt32(const char* byteArray) { return ((int32_t)(byteArray[0] & 0xFF) << 24) | ((int32_t)(byteArray[1] & 0xFF) << 16) | ((int32_t)(byteArray[2] & 0xFF) << 8) | (byteArray[3] & 0xFF); }

// Full framebuffer parser with zlib (keeps as RGBA, shader makes grayscale)
char* parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth, int* frameBufferHeight, z_stream& strm, int* finalHeight) {
    char messageType[1], padding[1], numberOfRectangles[2];
    if (!recv(socket_fd, messageType, 1, MSG_WAITALL)) return nullptr;
    if (!recv(socket_fd, padding, 1, MSG_WAITALL)) return nullptr;
    if (!recv(socket_fd, numberOfRectangles, 2, MSG_WAITALL)) return nullptr;

    int totalLoadedSize = 0;
    char* finalFrameBuffer = nullptr; // safer than malloc(1)
    int offset = 0;

    if (finalHeight) *finalHeight = 0; // initialize

    int numRects = byteArrayToInt16(numberOfRectangles);

    for (int i = 0; i < numRects; i++) {
        char xPos[2], yPos[2], width[2], height[2], encodingType[4], compressedDataSize[4];
        if (!recv(socket_fd, xPos, 2, MSG_WAITALL) ||
            !recv(socket_fd, yPos, 2, MSG_WAITALL) ||
            !recv(socket_fd, width, 2, MSG_WAITALL) ||
            !recv(socket_fd, height, 2, MSG_WAITALL) ||
            !recv(socket_fd, encodingType, 4, MSG_WAITALL)) {
            free(finalFrameBuffer);
            return nullptr;
        }

        *frameBufferWidth = byteArrayToInt16(width);
        *frameBufferHeight = byteArrayToInt16(height);
        if (finalHeight) *finalHeight += *frameBufferHeight;

        if (encodingType[3] == '\x6') { // Tight encoding
            if (!recv(socket_fd, compressedDataSize, 4, MSG_WAITALL)) {
                free(finalFrameBuffer);
                return nullptr;
            }

            int compressedSize = byteArrayToInt32(compressedDataSize);
            char* compressedData = (char*)malloc(compressedSize);
            if (!compressedData) {
                free(finalFrameBuffer);
                return nullptr;
            }

            int received = recv(socket_fd, compressedData, compressedSize, MSG_WAITALL);
            if (received != compressedSize) {
                free(compressedData);
                free(finalFrameBuffer);
                return nullptr;
            }

            int decompressedSize = (*frameBufferWidth) * (*frameBufferHeight) * 4;
            char* decompressedData = (char*)malloc(decompressedSize);
            if (!decompressedData) {
                free(compressedData);
                free(finalFrameBuffer);
                return nullptr;
            }

            totalLoadedSize += decompressedSize;
            char* newBuffer = (char*)realloc(finalFrameBuffer, totalLoadedSize);
            if (!newBuffer) { // realloc failed
                free(decompressedData);
                free(compressedData);
                free(finalFrameBuffer);
                return nullptr;
            }
            finalFrameBuffer = newBuffer;

            strm.avail_in = received;
            strm.next_in = (Bytef*)compressedData;
            strm.avail_out = decompressedSize;
            strm.next_out = (Bytef*)decompressedData;

            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                free(decompressedData);
                free(compressedData);
                free(finalFrameBuffer);
                return nullptr;
            }

            memcpy(finalFrameBuffer + offset, decompressedData, decompressedSize);
            offset += decompressedSize;

            free(decompressedData);
            free(compressedData);
        }
    }

    return finalFrameBuffer;
}


// Print string helpers
void print_string_center(float y, const char* text, float r, float g, float b, float size) {
    char inputBuffer[2000] = {0};
    GLfloat triangleBuffer[2000] = {0};
    int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));
    float ndcX = -stb_easy_font_width(text)*(size/200);
    float ndcY = (2.0f * y)/480; // Example conversion
    // Simple VBO draw
    GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(triangleBuffer),triangleBuffer,GL_STATIC_DRAW);
    GLint positionAttribute = glGetAttribLocation(programObjectTextRender,"position");
    glEnableVertexAttribArray(positionAttribute);
    glVertexAttribPointer(positionAttribute,2,GL_FLOAT,GL_FALSE,0,NULL);
    glDrawArrays(GL_TRIANGLES,0,number*6);
    glDeleteBuffers(1,&vbo);
}

// Full WinMain
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow) {
    LPCWSTR className = L"OpenGL Grayscale VNC";
    MSG msg = {0};
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(0,className,L"Grayscale VNC Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_OVERLAPPED,
        CW_USEDEFAULT,CW_USEDEFAULT,windowWidth,windowHeight,
        nullptr,nullptr,hInstance,nullptr);
    if (!hWnd) return 0;

    // EGL init
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    eglInitialize(eglDisplay,&major,&minor);
    EGLint config_attribs[] = {EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RED_SIZE,1,EGL_GREEN_SIZE,1,EGL_BLUE_SIZE,1,EGL_ALPHA_SIZE,1,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLint numConfigs;
    eglChooseConfig(eglDisplay,config_attribs,&eglConfig,1,&numConfigs);
    eglSurface = eglCreateWindowSurface(eglDisplay,eglConfig,hWnd,nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    eglContext = eglCreateContext(eglDisplay,eglConfig,EGL_NO_CONTEXT,contextAttribs);
    eglMakeCurrent(eglDisplay,eglSurface,eglSurface,eglContext);

    Init();

    WSADATA WSAData;
    WSAStartup(0x202,&WSAData);
    SOCKET sockfd = MySocketOpen(SOCK_STREAM,0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET,VNC_SERVER_IP_ADDRESS,&serverAddr.sin_addr);
    serverAddr.sin_port = htons(VNC_SERVER_PORT);
    connect(sockfd,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr));

    // VNC handshake
    char serverInit[12]; recv(sockfd,serverInit,sizeof(serverInit),MSG_WAITALL);
    send(sockfd,PROTOCOL_VERSION,strlen(PROTOCOL_VERSION),0);
    char security[4]; recv(sockfd,security,sizeof(security),0); send(sockfd,"\x01",1,0);
    char framebufferWidth[2], framebufferHeight[2];
    recv(sockfd,framebufferWidth,2,0); recv(sockfd,framebufferHeight,2,0);
    char pixelFormat[16], nameLength[4]; recv(sockfd,pixelFormat,sizeof(pixelFormat),MSG_WAITALL); recv(sockfd,nameLength,sizeof(nameLength),MSG_WAITALL);
    uint32_t nameLen = (nameLength[0]<<24)|(nameLength[1]<<16)|(nameLength[2]<<8)|nameLength[3];
    char name[32]; recv(sockfd,name,nameLen,MSG_WAITALL);
    send(sockfd,ZLIB_ENCODING,sizeof(ZLIB_ENCODING),0);
    send(sockfd,FRAMEBUFFER_UPDATE_REQUEST,sizeof(FRAMEBUFFER_UPDATE_REQUEST),0);

    int framebufferWidthInt=0, framebufferHeightInt=0, finalHeight=0, frameCount=0;
    double fps=0.0;
    time_t startTime = time(NULL);

    GLuint textureID; glGenTextures(1,&textureID); glBindTexture(GL_TEXTURE_2D,textureID);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    z_stream strm = {0}; strm.zalloc=Z_NULL; strm.zfree=Z_NULL; strm.opaque=Z_NULL; inflateInit(&strm);

    bool running = true;
    while (running) {
        frameCount++;
        char* framebufferUpdate = parseFramebufferUpdate(sockfd,&framebufferWidthInt,&framebufferHeightInt,strm,&finalHeight);
        if(!framebufferUpdate){running=false; break;}
        send(sockfd,FRAMEBUFFER_UPDATE_REQUEST,sizeof(FRAMEBUFFER_UPDATE_REQUEST),0);

        time_t currentTime = time(NULL);
        double elapsed = difftime(currentTime,startTime);
        if(elapsed>=1.0){fps=frameCount/elapsed; frameCount=0; startTime=currentTime;}

        glClear(GL_COLOR_BUFFER_BIT);

        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,framebufferWidthInt,finalHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,framebufferUpdate);

        glUseProgram(programObject);
        GLint posAttr = glGetAttribLocation(programObject,"position");
        glEnableVertexAttribArray(posAttr);
        if(framebufferWidthInt>finalHeight) glVertexAttribPointer(posAttr,3,GL_FLOAT,GL_FALSE,0,landscapeVertices);
        else glVertexAttribPointer(posAttr,3,GL_FLOAT,GL_FALSE,0,portraitVertices);

        GLint texAttr = glGetAttribLocation(programObject,"texCoord");
        glEnableVertexAttribArray(texAttr);
        if(framebufferWidthInt>finalHeight) glVertexAttribPointer(texAttr,2,GL_FLOAT,GL_FALSE,0,landscapeTexCoords);
        else glVertexAttribPointer(texAttr,2,GL_FLOAT,GL_FALSE,0,portraitTexCoords);

        glDrawArrays(GL_TRIANGLE_FAN,0,4);

        // Overlay
        glUseProgram(programObjectTextRender);
        char overlay[128];
        sprintf_s(overlay,"FPS: %.1f",fps); print_string_center(20,overlay,1,1,0,120);

        eglSwapBuffers(eglDisplay,eglSurface);
        free(framebufferUpdate);

        while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT) running=false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
    }

    glDeleteTextures(1,&textureID);
    eglSwapBuffers(eglDisplay,eglSurface);
    eglDestroyContext(eglDisplay,eglContext);
    eglDestroySurface(eglDisplay,eglSurface);
    eglTerminate(eglDisplay);
    WSACleanup();
    return static_cast<int>(msg.wParam);
}
