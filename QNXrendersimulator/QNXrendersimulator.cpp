#include <EGL/egl.h>
#include <regex>
#include <iostream>
#include <string>
#include <codecvt>
#include <filesystem>
#include <sstream>
#include <locale>
#include <GLES2/gl2.h>
#include <iostream>
#include <algorithm> // For std::min
#include <sys/stat.h>
#include <filesystem>
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_easyfont.h"
#include <chrono>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <fcntl.h>




//GLES setup
GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
int windowWidth = 800;
int windowHeight = 480;

// AA variables
std::string last_road = "UNKNOWN";
std::string last_turn_side = "UNKNOWN";
std::string last_event = "UNKNOWN";
std::string last_turn_angle = "0";
std::string last_turn_number = "0";
std::string last_valid = "1";
std::string last_distance_meters = "---";
std::string last_distance_seconds = "---";
std::string last_distance_valid = "1";

// esotrace AA data parsing
std::string log_directory_path = "C:\\Users\\OneB1t\\IdeaProjects\\VcMOSTRenderMqb\\QNXrendersimulator\\x64\\Debug\\logs\\somerandomfolder";  // point it to place where .esotrace files are stored
std::regex next_turn_pattern("onJob_updateNavigationNextTurnEvent : road='(.*?)', turnSide=(.*?), event=(.*?), turnAngle=(.*?), turnNumber=(.*?), valid=(.*?)");
std::regex next_turn_distance_pattern("onJob_updateNavigationNextTurnDistance : distanceMeters=(.*?), timeSeconds=(.*?), valid=(.*?)");

std::string icons_folder_path = "icons";

// AA sensors data location
std::string     sd = "i:1304:216";

GLfloat vertices[] = {
   -0.5f,  0.8, 0.0f,  // Top Left
    0.5f,  0.8f, 0.0f,  // Top Right
    0.5f, -1.80f, 0.0f,  // Bottom Right
   -0.5f, -1.80f, 0.0f   // Bottom Left
};

// Texture coordinates
GLfloat texCoords[] = {
    0.0f, 0.0f,  // Bottom Left
    1.0f, 0.0f,  // Bottom Right
    1.0f, 1.0f,  // Top Right
    0.0f, 1.0f   // Top Left
};

// Constants for VNC protocol
const char* PROTOCOL_VERSION = "RFB 003.003\n"; // Client initialization message
const char FRAMEBUFFER_UPDATE_REQUEST[] = {
    3,     // Message Type: FramebufferUpdateRequest
    0,
    0,0,
    0,0,
    255,255,
    255,255
};
const char CLIENT_INIT[] = {
    1,     // Message Type: FramebufferUpdateRequest
};

const char ENCODING[] = {
    2,0,0,1,0,0,0,0
};

void execute_initial_commands() {
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

std::string read_data(const std::string& position) {
    std::string command = "";
#ifdef _WIN32
    return "0";
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

// Vertex shader source
const char* vertexShaderSource =
"#version 100\n" // Specify ES 2.0 version
"attribute vec2 position;    \n"
"attribute vec2 texCoord;     \n" // Add texture coordinate attribute
"varying vec2 v_texCoord;     \n" // Declare varying variable for texture coordinate
"void main()                  \n"
"{                            \n"
"   gl_Position = vec4(position, 0.0, 1.0); \n"
"   v_texCoord = texCoord;   \n"
"   gl_PointSize = 4.0;      \n" // Point size
"}                            \n";

// Fragment shader source
//const char* fragmentShaderSource =
//"void main()               \n"
//"{                         \n"
//"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); \n" // Color
//"}                         \n";

const char* fragmentShaderSource =
"#version 100\n" // Specify ES 2.0 version
"precision highp float;\n"
"varying vec2 v_texCoord;\n"
"uniform sampler2D texture;\n"
"void main()\n"
"{\n"
"    gl_FragColor = texture2D(texture, v_texCoord);\n"
"}\n";

// Compile shader function
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // Check for compilation errors
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

void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    char inputBuffer[2000] = { 0 }; // ~500 chars
    GLfloat triangleBuffer[2000] = { 0 };
    int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

    // calculate movement inside viewport
    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;

    int triangleIndex = 0; // Index to keep track of the current position in the triangleBuffer
    // Convert each quad into two triangles and also apply size and offset to draw it to correct place
    for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); i += 8) {
        // Triangle 1
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 1) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 2) * sizeof(GLfloat)]) / size + +ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 3) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 4) * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 5) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;

        //// Triangle 2
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

    // Specify the layout of the vertex data
    GLint positionAttribute = glGetAttribLocation(programObject, "position");
    glEnableVertexAttribArray(positionAttribute);
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    // glEnableVertexAttribArray(0);

     // Render the triangle
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex);

    glDeleteBuffers(1, &vbo);
}

void print_string_center(float y, const char* text, float r, float g, float b, float size) {
    print_string(-stb_easy_font_width(text) * (size / 200), y, text, r, g, b, size);
}


// Initialize OpenGL ES
void Init() {
    // Load and compile shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Create program object
    programObject = glCreateProgram();
    glAttachShader(programObject, vertexShader);
    glAttachShader(programObject, fragmentShader);
    glLinkProgram(programObject);

    // Set clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

}
int16_t byteArrayToInt16(const char* byteArray) {
    return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
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

int parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth, int* frameBufferHeight) {
    // Read message-type (1 byte) - not used, assuming it's always 0
    char messageType[1];
    if (!recv(socket_fd, messageType, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading message type\n");
        return -1;
    }

    // Read padding (1 byte) - unused
    char padding[1];
    if (!recv(socket_fd, padding, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading padding\n");
        return -1;
    }

    // Read number-of-rectangles (2 bytes)
    char numberOfRectangles[2];
    if (!recv(socket_fd, numberOfRectangles, 2, MSG_WAITALL)) {
        fprintf(stderr, "Error reading number of rectangles\n");
        return -1;
    }

    // Calculate the total size of the message
    int pixelSizeToRead = 0; // message-type + padding + number-of-rectangles

    // Now parse each rectangle
    for (int i = 0; i < byteArrayToInt16(numberOfRectangles); i++) {
        // Read rectangle header
        char xPosition[2];
        char yPosition[2];
        char width[2];
        char height[2];
        char encodingType[4]; // S32

        if (!recv(socket_fd, xPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, yPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, width, 2, MSG_WAITALL) ||
            !recv(socket_fd, height, 2, MSG_WAITALL) ||
            !recv(socket_fd, encodingType, 4, MSG_WAITALL)) {
            fprintf(stderr, "Error reading rectangle header\n");
            return -1;
        }
        *frameBufferWidth = byteArrayToInt16(width);
        *frameBufferHeight = byteArrayToInt16(height);
        // Calculate size of pixel data based on encoding type (assuming 4 bytes per pixel)
        int pixelDataSize = byteArrayToInt16(width) * byteArrayToInt16(height) * 4;

        // Add the size of each rectangle header and pixel data to the total message size
        pixelSizeToRead += pixelDataSize;
    }

    printf("FramebufferUpdate message size: %d bytes\n", pixelSizeToRead);
    return pixelSizeToRead;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LPCWSTR className = L"OpenGL_ES_Window-QNXrender";
    MSG msg;
    // Register class
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    // Create window
    HWND hWnd = CreateWindowEx(0, className, L"OpenGL_ES_Window-QNXrender", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);

    if (hWnd == NULL) {
        MessageBox(nullptr, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Initialize EGL
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint majorVersion, minorVersion;
    eglInitialize(eglDisplay, &majorVersion, &minorVersion);

    // Setup EGL Configuration
    // Specify EGL configurations
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig eglConfig;
    EGLint numConfigs;
    eglChooseConfig(eglDisplay, config_attribs, &eglConfig, 1, &numConfigs);


    // Create EGL Surface
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, hWnd, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2, // Request OpenGL ES 2.0
    EGL_NONE // Indicates the end of the attribute list
    };

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

    // Initialize OpenGL ES
    Init();

    WSADATA WSAData = { 0 };
    WSAStartup(0x202, &WSAData);
    SOCKET MySocket = MySocketOpen(SOCK_STREAM, 0);


    // Connect to VNC server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.189", &serverAddr.sin_addr);
    serverAddr.sin_port = htons(5900); // VNC default port

    if (connect(MySocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Receive server initialization message
    char serverInitMsg[12];
    int bytesReceived = recv(MySocket, serverInitMsg, sizeof(serverInitMsg), MSG_WAITALL);

    if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
        std::cerr << "Error receiving server initialization message" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Send client protocol version message
    if (send(MySocket, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) == SOCKET_ERROR) {
        std::cerr << "Error sending client initialization message" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Security handshake
    char securityHandshake[4];
    int numOfTypes = recv(MySocket, securityHandshake, sizeof(securityHandshake), 0);
    printf("%s\n", securityHandshake);
    send(MySocket, "\x01", 1, 0); // ClientInit

    // Read framebuffer width and height
    char framebufferWidth[2];
    char framebufferHeight[2];

    if (!recv(MySocket, framebufferWidth, 2, 0) || !recv(MySocket, framebufferHeight, 2, 0)) {
        fprintf(stderr, "Error reading framebuffer dimensions\n");
        return 1;
    }



    // Read pixel format and name length
    char pixelFormat[16];
    char nameLength[4];

    if (!recv(MySocket, pixelFormat, sizeof(pixelFormat), MSG_WAITALL) ||
        !recv(MySocket, nameLength, sizeof(nameLength), MSG_WAITALL)) {
        fprintf(stderr, "Error reading pixel format or name length\n");
        return 1;
    }

    uint32_t nameLengthInt = (nameLength[0] << 24) | (nameLength[1] << 16) | (nameLength[2] << 8) | nameLength[3];

    // Read server name
    char name[32];
    if (!recv(MySocket, name, nameLengthInt, MSG_WAITALL)) {
        fprintf(stderr, "Error reading server name\n");
        return 1;
    }

    // Send encoding update requests
    if (send(MySocket, ENCODING, sizeof(ENCODING), 0) == SOCKET_ERROR ||
        send(MySocket, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) == SOCKET_ERROR) {
        std::cerr << "Error sending framebuffer update request" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }
    int framebufferWidthInt = 0;
    int framebufferHeightInt = 0;
        
    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Main loop
    while (true)
    {
        frameCount++;
        int leftSizeForRead = parseFramebufferUpdate(MySocket, &framebufferWidthInt, &framebufferHeightInt);
        char* framebufferUpdate = (char*)malloc(leftSizeForRead * sizeof(char));
        if (framebufferUpdate == NULL) {
            perror("malloc failed");
            return 1;
        }
        bytesReceived = recv(MySocket, framebufferUpdate, leftSizeForRead, MSG_WAITALL);
        if (bytesReceived < 0) {
            perror("error receiving framebuffer update");
            free(framebufferUpdate);
            return 1;
        }

        // Send encoding update request
        if (send(MySocket, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
            std::cerr << "error sending framebuffer update request" << std::endl;
            return 1;
        }

        // Calculate elapsed time
        time_t currentTime = time(NULL);
        double elapsedTime = difftime(currentTime, startTime);

        // Calculate FPS every second
        if (elapsedTime >= 1.0) {
            // Calculate FPS
            fps = frameCount / elapsedTime;

            // Reset frame count and start time
            frameCount = 0;
            startTime = currentTime;
        }
        glClear(GL_COLOR_BUFFER_BIT); // clear all
        glUseProgram(programObject);
        //char test[10]; // Adjust size accordingly
        //snprintf(test, sizeof(test), "%.2f FPS\n", fps);
        //print_string(-300, 200, test, 1, 1, 1, 200); // print FPS

        // Load image data into texture
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebufferWidthInt, framebufferHeightInt, 0, GL_RGBA, GL_UNSIGNED_BYTE, framebufferUpdate);

        // Set up shader program and attributes
        // (assuming you have a shader program with attribute vec3 position and attribute vec2 texCoord)

        // Set vertex positions
        GLint positionAttribute = glGetAttribLocation(programObject, "position");
        glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, vertices);
        glEnableVertexAttribArray(positionAttribute);

        // Set texture coordinates
        GLint texCoordAttrib = glGetAttribLocation(programObject, "texCoord");
        glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texCoords);
        glEnableVertexAttribArray(texCoordAttrib);

        // Draw quad
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        eglSwapBuffers(eglDisplay, eglSurface);
        free(framebufferUpdate); // Free the dynamically allocated memory
    }

    // Cleanup
    glDeleteTextures(1, &textureID);
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}
