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
    //GLES setup
    #define NUM_ZLIB_STREAMS 4
    z_stream strm[NUM_ZLIB_STREAMS];

    #define MAX_FRAMEBUFFER_SIZE (8*1024*1024)  // 16 MB max framebuffer size
    #define MAX_RECTANGLE_SIZE (261120)     // max decompression buffer size (Full HD RGBA)
    #define MAX_COMPRESSED_SIZE (1024 * 1024)        // 1 MB max compressed data buffer

    GLuint programObject;
    GLuint programObjectTextRender;
    EGLDisplay eglDisplay;
    EGLConfig eglConfig;
    EGLSurface eglSurface;
    EGLContext eglContext;
    const char* vertexShaderSource =
    "#version 100\n"
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
    "#version 100\n"
    "precision highp float;\n"
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
    "   gl_PointSize = 4.0;      \n" // Point size
    "}                            \n";
    const char* fragmentShaderSourceText =
    "void main()               \n"
    "{                         \n"
    "  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); \n" // Color
    "}                         \n";
    GLfloat landscapeVertices[] = { -0.8f,0.73f,0.0f, 0.8f,0.73f,0.0f, 0.8f,-0.63f,0.0f, -0.8f,-0.63f,0.0f };
    GLfloat portraitVertices[] = { -0.8f,1.0f,0.0f,  0.8f,1.0f,0.0f,  0.8f,-0.67f,0.0f, -0.8f,-0.67f,0.0f };
    GLfloat landscapeTexCoords[] = { 0.0f,0.07f, 0.90f,0.07f, 0.90f,1.0f, 0.0f,1.0f };
    GLfloat portraitTexCoords[] = { 0.0f,0.0f,  0.63f,0.0f,  0.63f,0.2f, 0.0f,0.2f };
    // Constants for VNC protocol
    const char* PROTOCOL_VERSION = "RFB 003.003\n"; // Client initialization message
    const char FRAMEBUFFER_UPDATE_REQUEST[] = {3,0,0,0,0,0,255,255,255,255};
    const char CLIENT_INIT[] = {1};
    const char ZLIB_ENCODING[] = {2,0,0,2,0,0,0,6,0,0,0,0};
    const char TIGHT_ENCODING[] = {
    2,          // message-type: SetEncodings
    0,          // padding
    0, 1,       // number of encodings = 1
    0, 0, 0, 7  // encoding: 7 (Tight)
    };

    struct PixelFormat {
        uint8_t bitsPerPixel;
        uint8_t depth;
        uint8_t bigEndianFlag;
        uint8_t trueColorFlag;
        uint16_t redMax;
        uint16_t greenMax;
        uint16_t blueMax;
        uint8_t redShift;
        uint8_t greenShift;
        uint8_t blueShift;
        uint8_t padding[3];
    };

    typedef struct {
        long long totalMs;
        long long recvHeaderMs;
        long long recvRectsMs;
        long long decompressMs;
    } ParseTiming;

    // SETUP SECTION
    int windowWidth = 800;    int windowHeight = 480;
    const char* VNC_SERVER_IP_ADDRESS = "192.168.1.190";
    const int VNC_SERVER_PORT = 5900;
    const char* EXLAP_SERVER_IP_ADDRESS = "127.0.0.1";
    const int EXLAP_SERVER_PORT = 25010;
    // WINDOWS SPECIFIC CODE SECTION
    static void usleep(__int64 usec) {
        HANDLE timer;
        LARGE_INTEGER ft;
        // Convert microseconds to 100-nanosecond intervals
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
    static void BuildSockAddr(SOCKADDR* const SockAddr, char const* const IPAddress, WORD const Port)
    {
        SOCKADDR_IN* const SIn = (SOCKADDR_IN*)SockAddr;
        SIn->sin_family = AF_INET;
        inet_pton(AF_INET, IPAddress, &SIn->sin_addr);
        SIn->sin_port = htons(Port);
    }
    SOCKET MySocketOpen(int const Type, WORD const Port)
    {
        int Protocol = (Type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
        SOCKET Socket = WSASocket(AF_INET, Type, Protocol, NULL, 0, 0);
        if (Socket != INVALID_SOCKET)
        {
            SOCKADDR SockAddr = { 0 };
            BuildSockAddr(&SockAddr, NULL, Port);
            if (bind(Socket, &SockAddr, sizeof(SockAddr)) != 0)
            {
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
    void executeInitialCommands() {
        std::vector<std::pair<std::string, std::string>> commands = {
            {"on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc i:1304:210 1", "Cannot enable AA sensors data"},
            {"/eso/bin/apps/dmdt sc 4 -9", "Set context of display 4 failed with error"},
            {"/eso/bin/apps/dmdt sc 0 71", "Switch context to 71 on display 0 failed with error"}
        };
        for (size_t i = 0; i < commands.size(); ++i) {
            std::string command = commands[i].first;
            std::string error_message = commands[i].second;
            std::cout << "Executing '" << command << "'" << std::endl;
            // Execute the command
            int ret = system(command.c_str());
            if (ret != 0) {
                std::cerr << error_message << ": " << ret << std::endl;
            }
        }
    }
    void executeFinalCommands() {
        std::vector<std::pair<std::string, std::string>> commands = {
            {"/eso/bin/apps/dmdt sc 4 70", "Set display 4 (VC) to display table 70 failed with error"}
        };
        for (size_t i = 0; i < commands.size(); ++i) {
            std::string command = commands[i].first;
            std::string error_message = commands[i].second;
            std::cout << "Executing '" << command << "'" << std::endl;
            int ret = system(command.c_str());
            if (ret != 0) {
                std::cerr << error_message << ": " << ret << std::endl;
            }
        }
    }
    std::string readPersistanceData(const std::string& position) {
        std::string command = "";
    #ifdef _WIN32
        return ""; // not connected
    #else
        command = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc " + position;
    #endif
        FILE* pipe = _popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << "Error: Failed to execute command." << std::endl;
            return "0";
        }
        char buffer[128];
        std::string result = "";
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
        _pclose(pipe);
        std::cout << result;
        return result;
    }
    int16_t byteArrayToInt16(const char* byteArray) {
        return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
    }
    int32_t byteArrayToInt32(const char* byteArray) {
        return ((int32_t)(byteArray[0] & 0xFF) << 24) | ((int32_t)(byteArray[1] & 0xFF) << 16) | ((int32_t)(byteArray[2] & 0xFF) << 8) | (byteArray[3] & 0xFF);
    }
    void Init() {
        GLuint vertexShaderVncRender = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShaderVncRender, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShaderVncRender);
        GLint vertexShaderCompileStatus;
        glGetShaderiv(vertexShaderVncRender, GL_COMPILE_STATUS, &vertexShaderCompileStatus);
        if (vertexShaderCompileStatus != GL_TRUE) {
            char infoLog[512];
            glGetShaderInfoLog(vertexShaderVncRender, 512, NULL, infoLog);
            printf("Vertex shader compilation failed: %s\n", infoLog);
        }
        GLuint fragmentShaderVncRender = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShaderVncRender, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShaderVncRender);
        GLint fragmentShaderCompileStatus;
        glGetShaderiv(fragmentShaderVncRender, GL_COMPILE_STATUS, &fragmentShaderCompileStatus);
        if (fragmentShaderCompileStatus != GL_TRUE) {
            char infoLog[512];
            glGetShaderInfoLog(fragmentShaderVncRender, 512, NULL, infoLog);
            printf("Fragment shader compilation failed: %s\n", infoLog);
        }
        GLuint vertexShaderTextRender = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShaderTextRender, 1, &vertexShaderSourceText, NULL);
        glCompileShader(vertexShaderTextRender);
        GLint vertexShaderTextRenderCompileStatus;
        glGetShaderiv(vertexShaderTextRender, GL_COMPILE_STATUS, &vertexShaderTextRenderCompileStatus);
        if (vertexShaderTextRenderCompileStatus != GL_TRUE) {
            char infoLog[512];
            glGetShaderInfoLog(vertexShaderTextRender, 512, NULL, infoLog);
            printf("Vertex shader compilation failed: %s\n", infoLog);
        }
        GLuint fragmentShaderTextRender = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShaderTextRender, 1, &fragmentShaderSourceText, NULL);
        glCompileShader(fragmentShaderTextRender);
        GLint fragmentShaderCompileStatusTextRender;
        glGetShaderiv(fragmentShaderTextRender, GL_COMPILE_STATUS, &fragmentShaderCompileStatusTextRender);
        if (fragmentShaderCompileStatus != GL_TRUE) {
            char infoLog[512];
            glGetShaderInfoLog(fragmentShaderTextRender, 512, NULL, infoLog);
            printf("Fragment shader compilation failed: %s\n", infoLog);
        }
        programObject = glCreateProgram();
        glAttachShader(programObject, vertexShaderVncRender);
        glAttachShader(programObject, fragmentShaderVncRender);
        glLinkProgram(programObject);
        programObjectTextRender = glCreateProgram();
        glAttachShader(programObjectTextRender, vertexShaderTextRender);
        glAttachShader(programObjectTextRender, fragmentShaderTextRender);
        glLinkProgram(programObjectTextRender);
        GLint programLinkStatus;
        glGetProgramiv(programObject, GL_LINK_STATUS, &programLinkStatus);
        if (programLinkStatus != GL_TRUE) {
            char infoLog[512];
            glGetProgramInfoLog(programObject, 512, NULL, infoLog);
            printf("Program linking failed: %s\n", infoLog);
        }
        glGetProgramiv(programObjectTextRender, GL_LINK_STATUS, &programLinkStatus);
        if (programLinkStatus != GL_TRUE) {
            char infoLog[512];
            glGetProgramInfoLog(programObjectTextRender, 512, NULL, infoLog);
            printf("Program linking failed: %s\n", infoLog);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Helper to convert 2 bytes (big endian) to int16
    int byteArrayToInt16(char* bytes) {
        return ((unsigned char)bytes[0] << 8) | (unsigned char)bytes[1];
    }
    // Helper to convert 4 bytes (big endian) to int32
    int byteArrayToInt32(char* bytes) {
        return ((unsigned char)bytes[0] << 24) | ((unsigned char)bytes[1] << 16) | ((unsigned char)bytes[2] << 8) | (unsigned char)bytes[3];
    }

    // Helper to decode Tight's variable-length integer encoding
    int readTightLength(SOCKET sock) {
        int length = 0;
        int shift = 0;
        unsigned char b;
        do {
            if (recv(sock, (char*)&b, 1, MSG_WAITALL) != 1) {
                fprintf(stderr, "Error reading Tight length\n");
                return -1;
            }
            length |= (b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
        return length;
    }

    void debugPrintRect(int width, int height, int encoding) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Rect: width=%d height=%d encoding=0x%08X\n", width, height, encoding);
        OutputDebugStringA(buffer);
    }
    static bool recv_all(SOCKET sock, void* buffer, size_t length) {
        size_t total_received = 0;
        char* ptr = (char*)buffer;
        while (total_received < length) {
            int bytes = recv(sock, ptr + total_received, length - total_received, MSG_WAITALL);
            if (bytes <= 0) return false;
            total_received += bytes;
        }
        return true;
    }

    char* parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth, int* frameBufferHeight, z_stream strm[], int* finalHeight, ParseTiming* timingOut)
    {
        using Clock = std::chrono::high_resolution_clock;
        auto startTime = Clock::now();
        auto lastTime = startTime;
        static char* finalFrameBuffer = NULL;
        static size_t finalFrameBufferSize = 0;

        static char* decompressedData = NULL;
        static size_t decompressedDataSize = 0;

        static char* compressedData = NULL;
        static size_t compressedDataSize = 0;

        char header[4];

        if (!recv_all(socket_fd, header, sizeof(header))) {
            fprintf(stderr, "Error reading framebuffer update header\n");
            return NULL;
        }
        auto recvHeaderTime = Clock::now();
        timingOut->recvHeaderMs = std::chrono::duration_cast<std::chrono::milliseconds>(recvHeaderTime - lastTime).count();
        lastTime = recvHeaderTime;

        int numRectangles = byteArrayToInt16(&header[2]);
        int offset = 0;
        *finalHeight = 0;

        // Allocate or resize the final framebuffer
        if (!finalFrameBuffer) {
            finalFrameBuffer = (char*)malloc(MAX_FRAMEBUFFER_SIZE);
            if (!finalFrameBuffer) {
                perror("malloc finalFrameBuffer");
                return NULL;
            }
            finalFrameBufferSize = MAX_FRAMEBUFFER_SIZE;
        }

        // Rectangle parsing loop
        long long totalRecvRects = 0;
        long long totalDecompress = 0;

        for (int i = 0; i < numRectangles; i++) {
            auto rectStart = Clock::now();
            char rectHeader[12];
            if (!recv_all(socket_fd, rectHeader, sizeof(rectHeader))) {
                fprintf(stderr, "Error reading rectangle header\n");
                return NULL;
            }
            auto rectRecvDone = Clock::now();
            int rectWidth = byteArrayToInt16(&rectHeader[4]);
            int rectHeight = byteArrayToInt16(&rectHeader[6]);
            int encodingType = byteArrayToInt32(&rectHeader[8]);
            if (encodingType == 0x000000FF || encodingType > 0x08 || encodingType == 0x00000000) {
                continue;
            }

            if (rectWidth <= 0 || rectHeight <= 0 || rectWidth > 10000 || rectHeight > 10000) {
                continue;
            }



            int decompressedSize = rectWidth * rectHeight * 4;
            if (encodingType == 0x000000FF || encodingType > 0x08 || encodingType == 0x00000000) {
                // Solid color fill rectangle, no decompression needed
                decompressedSize = 4;
			}
            if ((size_t)(offset + decompressedSize) > finalFrameBufferSize) {
                fprintf(stderr, "Final framebuffer overflow at %d bytes\n", offset + decompressedSize);
                return NULL;
            }
            *frameBufferWidth = rectWidth;
            *frameBufferHeight = rectHeight;
            *finalHeight += rectHeight;

            // Grow decompression buffer if needed (exponential growth)
            while ((size_t)decompressedSize > decompressedDataSize) {
                size_t newSize = decompressedDataSize ? decompressedDataSize * 2 : MAX_RECTANGLE_SIZE;
                decompressedData = (char*)realloc(decompressedData, newSize);
                if (!decompressedData) {
                    perror("realloc decompressedData");
                    return NULL;
                }
                decompressedDataSize = newSize;
            }
            auto decompressDone = Clock::now();

            bool success = false;
            if (encodingType == 0x000000FF || encodingType > 0x08 || encodingType == 0x00000000)  {
                // Solid color fill rectangle
                unsigned char color[4];
                if (!recv_all(socket_fd, (char*)color, 4)) {
                    fprintf(stderr, "Error reading solid fill color\n");
                    return NULL;
                }

                // Fill the rectangle in the framebuffer
                // finalFrameBuffer is your framebuffer pointer
                // framebufferWidth is total width of framebuffer
                for (int row = 0; row < 800; row++) {
                    char* dest = finalFrameBuffer;
                    for (int col = 0; col < 480; col++) {
                        memcpy(dest + col * 4, color, 4);
                    }
                }

                // Advance offset or whatever your logic needs

            }
            else if (encodingType == 0x07) {  // Tight encoding
                unsigned char tightControl;

                if (recv(socket_fd, (char*)&tightControl, 1, MSG_WAITALL) != 1) {
                    fprintf(stderr, "Error reading Tight control byte\n");
                    return NULL;
                }

                unsigned int compressionControl = tightControl & 0x07;
                bool resetStream = (tightControl & 0x08) != 0;

                int compressedLength = readTightLength(socket_fd);
                if (compressedLength < 0) {
                    fprintf(stderr, "Invalid compressed length\n");
                    return NULL;
                }

                if ((size_t)compressedLength > compressedDataSize) {
                    char* tmp = (char*)realloc(compressedData, compressedLength);
                    if (!tmp) {
                        perror("realloc compressedData");
                        return NULL;
                    }
                    compressedData = tmp;
                    compressedDataSize = compressedLength;
                }

                if (!recv_all(socket_fd, compressedData, compressedLength)) {
                    fprintf(stderr, "Error reading compressed data\n");
                    return NULL;
                }

                z_stream* chosenStrm = &strm[compressionControl];

                if (resetStream) {
                    if (inflateReset(chosenStrm) != Z_OK) {
                        fprintf(stderr, "inflateReset failed\n");
                        return NULL;
                    }
                }

                chosenStrm->avail_in = compressedLength;
                chosenStrm->next_in = (Bytef*)compressedData;
                chosenStrm->avail_out = decompressedSize;
                chosenStrm->next_out = (Bytef*)decompressedData;

                int ret = inflate(chosenStrm, Z_SYNC_FLUSH);

                // Accept Z_OK or Z_STREAM_END, treat Z_BUF_ERROR as non-fatal if no output needed
                if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                    fprintf(stderr, "Tight inflate error: %s\n", chosenStrm->msg ? chosenStrm->msg : "unknown");
                    return NULL;
                }

                memcpy(finalFrameBuffer + offset, decompressedData, decompressedSize);
                offset += decompressedSize;
                success = true;
            }
            else if ((encodingType & 0xFF) == 0x08) {
                // Solid color fill     
                // Read 4 bytes of pixel color data
                unsigned char pixelColor[4];
                if (!recv_all(socket_fd, (char*)pixelColor, 4)) {
                    fprintf(stderr, "Error reading fill color\n");
                    return NULL;
                }

                // Fill decompressedData buffer with this pixel color
                for (int px = 0; px < decompressedSize; px += 4) {
                    memcpy(decompressedData + px, pixelColor, 4);
                }

                memcpy(finalFrameBuffer + offset, decompressedData, decompressedSize);
                offset += decompressedSize;
                success = true;
            }
            else if (encodingType > 0x08)
            {
                // Solid color fill rectangle
                unsigned char color[4];
                if (!recv_all(socket_fd, (char*)color, 4)) {
                    fprintf(stderr, "Error reading solid fill color\n");
                    return NULL;
                }

                // Fill the rectangle in the framebuffer
                // finalFrameBuffer is your framebuffer pointer
                // framebufferWidth is total width of framebuffer
                for (int row = 0; row < 800; row++) {
                    char* dest = finalFrameBuffer;
                    for (int col = 0; col < 480; col++) {
                        memcpy(dest + col * 4, color, 4);
                    }
                }
            }
            else {
                fprintf(stderr, "Unsupported encoding: 0x%08x\n", encodingType);
                return NULL;
            }

            if (!success) {
                fprintf(stderr, "Failed to process rectangle %d\n", i);
                return NULL;
            }
            totalRecvRects += std::chrono::duration_cast<std::chrono::milliseconds>(rectRecvDone - rectStart).count();
            totalDecompress += std::chrono::duration_cast<std::chrono::milliseconds>(decompressDone - rectRecvDone).count();
        }
        timingOut->recvRectsMs = totalRecvRects;
        timingOut->decompressMs = totalDecompress;
        timingOut->totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startTime).count();
        return finalFrameBuffer;
    }


    void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
        char inputBuffer[2000] = { 0 }; // ~500 chars
        GLfloat triangleBuffer[2000] = { 0 };
        int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));
        float ndcMovementX = (2.0f * x) / windowWidth;
        float ndcMovementY = (2.0f * y) / windowHeight;
        int triangleIndex = 0; // Index to keep track of the current position in the triangleBuffer
        // Convert each quad into two triangles and also apply size and offset to draw it to correct place
        for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); i += 8) {
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]) / size + ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 1) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 2) * sizeof(GLfloat)]) / size + +ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 3) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 4) * sizeof(GLfloat)]) / size + ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 5) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]) / size + ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 1) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 4) * sizeof(GLfloat)]) / size + ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 5) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 6) * sizeof(GLfloat)]) / size + ndcMovementX;
            triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 7) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        }
        GLuint vbo;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangleBuffer), triangleBuffer, GL_STATIC_DRAW);
        GLint positionAttribute = glGetAttribLocation(programObject, "position");
        glEnableVertexAttribArray(positionAttribute);
        glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glDrawArrays(GL_TRIANGLES, 0, triangleIndex);
        glDeleteBuffers(1, &vbo);
    }
    void print_string_center(float y, const char* text, float r, float g, float b, float size) {
        print_string(-stb_easy_font_width(text) * (size / 200), y, text, r, g, b, size);
    }
    void parseLineArray(char* line, const char* key, GLfloat* dest, int count) {
        if (strncmp(line, key, strlen(key)) == 0) {
            char* values = strchr(line, '=');
            if (values) {
                values++; // Skip '='
                for (int i = 0; i < count; i++) {
                    dest[i] = strtof(values, &values); // Parse floats
                }
            }
        }
    }


    void parseLineInt(char* line, const char* key, int* dest) {
        if (strncmp(line, key, strlen(key)) == 0) {
            char* value = strchr(line, '=');
            if (value) {
                *dest = atoi(value + 1); // Parse integer
            }
        }
    }
    void loadConfig(const char* filename) {
        FILE* file = NULL;
        errno_t err = fopen_s(&file, filename, "r");
        if (err != 0 || file == NULL) {
            printf("Config file not found or cannot be opened. Using defaults.\n");
            return;
        }

        char line[256];
        while (fgets(line, sizeof(line), file)) {
            parseLineArray(line, "landscapeVertices", landscapeVertices, 12);
            parseLineArray(line, "portraitVertices", portraitVertices, 12);
            parseLineArray(line, "landscapeTexCoords", landscapeTexCoords, 8);
            parseLineArray(line, "portraitTexCoords", portraitTexCoords, 8);
            parseLineInt(line, "windowWidth", &windowWidth);
            parseLineInt(line, "windowHeight", &windowHeight);
        }

        fclose(file);
    }
    void printArray(const char* label, GLfloat* array, int count, int elementsPerLine) {
        printf("%s:\n", label);
        for (int i = 0; i < count; i++) {
            printf("%f ", array[i]);
            if ((i + 1) % elementsPerLine == 0) printf("\n");
        }
        printf("\n");
    }

    int initializeZlibStreams() {
        for (int i = 0; i < NUM_ZLIB_STREAMS; i++) {
            strm[i].zalloc = Z_NULL;
            strm[i].zfree = Z_NULL;
            strm[i].opaque = Z_NULL;
            strm[i].avail_in = 0;
            strm[i].next_in = Z_NULL;
            int ret = inflateInit(&strm[i]);
            if (ret != Z_OK) {
                fprintf(stderr, "Error: Failed to initialize zlib stream %d\n", i);
                // Cleanup any streams initialized so far
                for (int j = 0; j < i; j++) {
                    inflateEnd(&strm[j]);
                }
                return ret;
            }
        }
        return Z_OK;
    }

    void cleanupZlibStreams() {
        for (int i = 0; i < NUM_ZLIB_STREAMS; i++) {
            inflateEnd(&strm[i]);
        }
    }

    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
        LPCWSTR className = L"OpenGL QNX render simulator";
        MSG msg;
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = className;
        wc.style = CS_OWNDC;
        RegisterClass(&wc);
        loadConfig("config.txt");
        printArray("Landscape vertices", landscapeVertices, 12, 3);
        printArray("Portrait vertices", portraitVertices, 12, 3);
        printArray("Landscape texture coordinates", landscapeTexCoords, 8, 2);
        printArray("Portrait texture coordinates", portraitTexCoords, 8, 2);
        HWND hWnd = CreateWindowEx(0, className, L"OpenGL QNX render simulator", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);
        if (hWnd == NULL) {
            MessageBox(nullptr, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
            return 0;
        }
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj, min;
        eglInitialize(eglDisplay, &maj, &min);
        EGLint cfg[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 1, EGL_GREEN_SIZE, 1,
            EGL_BLUE_SIZE, 1, EGL_ALPHA_SIZE, 1,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        EGLConfig config; EGLint n;
        eglChooseConfig(eglDisplay, cfg, &config, 1, &n);
        eglSurface = eglCreateWindowSurface(eglDisplay, config, hWnd, nullptr);
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint ctx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, ctx);
        eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
        Init();
        while (true)
        {
            WSADATA WSAData = { 0 };
            WSAStartup(0x202, &WSAData);
            SOCKET sockfd = MySocketOpen(SOCK_STREAM, 0);
            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            inet_pton(AF_INET, VNC_SERVER_IP_ADDRESS, &serverAddr.sin_addr);
            serverAddr.sin_port = htons(VNC_SERVER_PORT); // VNC default port
            struct timeval timeout;
            timeout.tv_sec = 10; // 10 seconds timeout
            timeout.tv_usec = 0;
            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
                perror("Set receive timeout failed");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
                perror("Set send timeout failed");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            if (connect(sockfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
                std::cerr << "Connection failed" << std::endl;
                closesocket(sockfd);
                WSACleanup();
                usleep(200000);
                continue;
            }
            char serverInitMsg[12];
            int bytesReceived = recv(sockfd, serverInitMsg, sizeof(serverInitMsg), MSG_WAITALL);
            if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                std::cerr << "Error receiving server initialization message" << std::endl;
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            if (send(sockfd, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) == SOCKET_ERROR) {
                std::cerr << "Error sending client initialization message" << std::endl;
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            char securityHandshake[4];
            int numOfTypes = recv(sockfd, securityHandshake, sizeof(securityHandshake), 0);
            printf("%s\n", securityHandshake);
            send(sockfd, "\x01", 1, 0); // ClientInit
            char framebufferWidth[2]; char framebufferHeight[2];
            if (!recv(sockfd, framebufferWidth, 2, 0) || !recv(sockfd, framebufferHeight, 2, 0)) {
                fprintf(stderr, "Error reading framebuffer dimensions\n");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            char pixelFormat[16]; char nameLength[4];
            if (!recv(sockfd, pixelFormat, sizeof(pixelFormat), MSG_WAITALL) ||
                !recv(sockfd, nameLength, sizeof(nameLength), MSG_WAITALL)) {
                fprintf(stderr, "Error reading pixel format or name length\n");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            uint32_t nameLengthInt = (nameLength[0] << 24) | (nameLength[1] << 16) | (nameLength[2] << 8) | nameLength[3];
            char name[32];
            if (!recv(sockfd, name, nameLengthInt, MSG_WAITALL)) {
                fprintf(stderr, "Error reading server name\n");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            if (send(sockfd, TIGHT_ENCODING, sizeof(TIGHT_ENCODING), 0) == SOCKET_ERROR ||
                send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) == SOCKET_ERROR) {
                std::cerr << "Error sending framebuffer update request" << std::endl;
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            int framebufferWidthInt = 0; int framebufferHeightInt = 0; int numberOfRectangles = 0; int finalHeight = 0;
            int frameCount = 0; int switchToMap = 0; double fps = 0.0; time_t startTime = time(NULL); GLuint textureID;
            glGenTextures(1, &textureID);
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            int ret = initializeZlibStreams();
            if (ret != Z_OK) {
                fprintf(stderr, "Error: Failed to initialize zlib decompression\n");
                closesocket(sockfd);
                WSACleanup();
                continue;
            }
            bool running = true;
            // Main loop
            while (running) {
                using Clock = std::chrono::high_resolution_clock;
                auto frameStartTime = Clock::now();

                frameCount++;
                ParseTiming timing;
                if (send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
                    std::cerr << "Error sending framebuffer update request\n";
                    cleanupZlibStreams();
                    closesocket(sockfd);
                    WSACleanup();
                    break;
                }
                char* framebufferUpdate = parseFramebufferUpdate(sockfd, &framebufferWidthInt, &framebufferHeightInt, strm, &finalHeight, &timing);

                if (framebufferUpdate == NULL) {
                    cleanupZlibStreams();
                    closesocket(sockfd);
                    WSACleanup();
                    break;
                }



                time_t currentTime = time(NULL);
                double elapsedTime = difftime(currentTime, startTime);
                if (elapsedTime >= 1.0) {
                    fps = frameCount / elapsedTime;
                    frameCount = 0;
                    startTime = currentTime;
                }

                // OpenGL draw pipeline
                glClear(GL_COLOR_BUFFER_BIT);
                glUseProgram(programObject);

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebufferWidthInt, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, framebufferUpdate);

                GLint positionAttribute = glGetAttribLocation(programObject, "position");
                GLint texCoordAttrib = glGetAttribLocation(programObject, "texCoord");

                const GLfloat* vertices = framebufferWidthInt > finalHeight ? landscapeVertices : portraitVertices;
                const GLfloat* texCoords = framebufferWidthInt > finalHeight ? landscapeTexCoords : portraitTexCoords;

                glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, vertices);
                glEnableVertexAttribArray(positionAttribute);

                glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texCoords);
                glEnableVertexAttribArray(texCoordAttrib);

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

                // Overlay text (FPS + parse duration)
                glUseProgram(programObjectTextRender);
                char overlayLine1[64], overlayLine2[64];

                snprintf(overlayLine1, sizeof(overlayLine1), "FPS: %.f  T: %lldms", fps, timing.totalMs);
                snprintf(overlayLine2, sizeof(overlayLine2), "RH: %lld  R: %lld  D: %lld",
                    timing.recvHeaderMs,
                    timing.recvRectsMs,
                    timing.decompressMs);

                int y = 230;
                print_string(-360, y, overlayLine1, 1, 1, 1, 150);
                print_string(-360, y - 25, overlayLine2, 1, 1, 1, 150);

                eglSwapBuffers(eglDisplay, eglSurface);

                switchToMap = (switchToMap + 1) % 26;

                glDisableVertexAttribArray(positionAttribute);
                glDisableVertexAttribArray(texCoordAttrib);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                // Windows event pump
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                finalHeight = 0;  // reset for next frame
            }
            glDeleteTextures(1, &textureID);
        }
        // Cleanup
        eglSwapBuffers(eglDisplay, eglSurface);
        eglDestroyContext(eglDisplay, eglContext);
        eglDestroySurface(eglDisplay, eglSurface);
        eglTerminate(eglDisplay);
        //execute_final_commands(); DO NOT RUN ON WINDOWS THERE IS NO NEED

        return msg.wParam;
    }
