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

// Constants for VNC protocol
const char* PROTOCOL_VERSION = "RFB 003.003\n"; // Client initialization message
const char FRAMEBUFFER_UPDATE_REQUEST[] = {
    3,     // Message Type: FramebufferUpdateRequest
    0,
    0,0,
    0,0,
    15,0,
    8,112
};
const char CLIENT_INIT[] = {
    1,     // Message Type: FramebufferUpdateRequest
};

const char ENCODING[] = {
    2,0,0,1,0,0,0,0
};


std::string find_newest_file(const std::string& directory, const std::string& extension = ".esotrace") {
    std::string newest_file;
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((directory + "/*").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening directory" << std::endl;
        return "";
    }

    time_t newest_mtime = 0;

    do {
        std::string filename = findFileData.cFileName;
        std::string filepath = directory + "/" + filename;

        if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && filename.find(extension) != std::string::npos) {
            FILETIME ft = findFileData.ftLastWriteTime;
            ULARGE_INTEGER ull;
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;
            time_t modified_time = ull.QuadPart / 10000000ULL - 11644473600ULL; // Convert to UNIX time
            if (modified_time > newest_mtime) {
                newest_mtime = modified_time;
                newest_file = filepath;
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
    return newest_file;
}

void parse_log_line_next_turn(const std::smatch& match) {
    if (!match.empty()) {
        last_road = match[1].str();
        last_turn_side = match[2].str();
        last_event = match[3].str();
        last_turn_angle = (match[4].str());
        last_turn_number = (match[5].str());
        last_valid = (match[6].str());
    }
    else {
        // Set default values or handle the case where the pattern is not found
        last_road = "";
        last_turn_side = "";
        last_event = "";
        last_turn_angle = "0";
        last_turn_number = "0";
        last_valid = "0";
    }
}


void parse_log_line_next_turn_distance(const std::smatch& match) {
    if (!match.empty()) {
        last_distance_meters = (match[1].str());
        last_distance_seconds = (match[2].str());
        last_distance_valid = (match[3].str());
    }
    else {
        // Set default values or handle the case where the pattern is not found
        last_distance_meters = "0";
        last_distance_seconds = "0";
        last_distance_valid = "0";
    }
}

void search_and_parse_last_occurrence(const std::string& file_path, const std::regex& regex_pattern, int parseoption) {
    FILE* file;
    if (fopen_s(&file, file_path.c_str(), "rb") != 0) {
        std::cerr << "File '" << file_path << "' does not exist." << std::endl;
        return;
    }

    const size_t chunk_size = 8192;
    std::smatch last_match;
    std::string chunk;

    // Start searching from the end of the file
    _fseeki64(file, 0, SEEK_END);
    long long file_size = _ftelli64(file);
    while (_ftelli64(file) > 0) {
        long long current_position = _ftelli64(file);
        size_t offset = (current_position >= chunk_size) ? chunk_size : static_cast<size_t>(current_position);
        _fseeki64(file, -static_cast<long>(offset), SEEK_CUR);

        char* data = new char[offset]; // Allocate memory for chunk
        if (!data) {
            fclose(file);
            return;
        }

        fread(data, 1, offset, file);
        chunk.insert(0, data, offset); // Insert chunk into string
        delete[] data;

        std::smatch match;
        if (std::regex_search(chunk, match, regex_pattern)) {
            if(parseoption == 0)
            { 
                parse_log_line_next_turn(match);
            }
            else if (parseoption == 1)
            {
                parse_log_line_next_turn_distance(match);
            }
            fclose(file);
            break;
        }
        else if (current_position == 0) {
            break; // Reached the beginning of the file
        }
    }
    fclose(file);
}

void execute_initial_commands() {
    std::vector<std::pair<std::string, std::string>> commands = {
        {"/scripts/activateSDCardEsotrace.sh", "Cannot activate SD card trace log"},
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

std::string convert_to_km(int meters) {
    if (meters >= 1000) {
        // Convert meters to kilometers
        double km = static_cast<double>(meters) / 1000.0;
        int km_int = static_cast<int>(km); // Integer part of kilometers
        int decimal_part = static_cast<int>((km - km_int) * 10); // Extract one decimal place
        std::stringstream ss;
        ss << km_int << '.' << decimal_part << " Km";
        return ss.str();
    }
    else {
        // Return meters if less than 1000 meters
        return std::to_string(meters) + " m";
    }
}




// Vertex shader source
const char* vertexShaderSource =
"attribute vec2 position;    \n"
"void main()                  \n"
"{                            \n"
"   gl_Position = vec4(position, 0.0, 1.0); \n"
"   gl_PointSize = 4.0;      \n" // Point size
"}                            \n";

// Fragment shader source
const char* fragmentShaderSource =
"void main()               \n"
"{                         \n"
"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); \n" // Color
"}                         \n";

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

void drawArrow() {
    // Define the vertices of the arrowhead
    GLfloat arrowheadVertices[] = {
        0.0f, 0.5f,    // Top
        -0.1f, 0.2f,   // Top left
        0.1f, 0.2f,    // Top right
        0.0f, 0.0f     // Middle
    };

    // Define the vertices of the shaft
    GLfloat shaftVertices[] = {
        0.0f, 0.0f,    // Middle
        0.0f, -0.5f,   // Bottom
        -0.05f, -0.5f, // Bottom left
        0.05f, -0.5f   // Bottom right
    };

    GLint positionAttribute = glGetAttribLocation(programObject, "position");
    // Set up vertex attribute pointer for the arrowhead
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, arrowheadVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the arrowhead
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Set up vertex attribute pointer for the shaft
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, shaftVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the shaft
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int16_t byteArrayToInt16(const char* byteArray) {
    return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
}

const int NUM_SEGMENTS = 30;
// Function to render the ring
void drawRing() {
    // Set up radius and center of the ring
    float outerRadius = 0.5f;
    float innerRadius = 0.4f;
    float centerX = 0.0f;
    float centerY = 0.0f;

    // Define vertices array for the outer circle
    GLfloat outerCircleVertices[(NUM_SEGMENTS + 1) * 2];
    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        float angle = ((float)i / NUM_SEGMENTS) * 2.0f * 3.1421;
        outerCircleVertices[i * 2] = centerX + outerRadius * cos(angle);
        outerCircleVertices[i * 2 + 1] = centerY + outerRadius * sin(angle);
    }
    outerCircleVertices[NUM_SEGMENTS * 2] = outerCircleVertices[0];
    outerCircleVertices[NUM_SEGMENTS * 2 + 1] = outerCircleVertices[1];

    // Define vertices array for the inner circle
    GLfloat innerCircleVertices[(NUM_SEGMENTS + 1) * 2];
    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        float angle = ((float)i / NUM_SEGMENTS) * 2.0f * 3.1421;
        innerCircleVertices[i * 2] = centerX + innerRadius * cos(angle);
        innerCircleVertices[i * 2 + 1] = centerY + innerRadius * sin(angle);
    }
    innerCircleVertices[NUM_SEGMENTS * 2] = innerCircleVertices[0];
    innerCircleVertices[NUM_SEGMENTS * 2 + 1] = innerCircleVertices[1];

    float aspectRatio = (float)windowHeight / windowWidth;

    // Adjust vertices for aspect ratio
    for (int i = 0; i <= NUM_SEGMENTS; ++i) {
        outerCircleVertices[i * 2] *= aspectRatio;
        innerCircleVertices[i * 2] *= aspectRatio;
    }
    GLint positionAttribute = glGetAttribLocation(programObject, "position");
    // Set up vertex attribute pointer for the outer circle
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, outerCircleVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the outer circle
    glDrawArrays(GL_LINE_STRIP, 0, NUM_SEGMENTS + 1);

    // Set up vertex attribute pointer for the inner circle
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, innerCircleVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the inner circle
    glDrawArrays(GL_LINE_STRIP, 0, NUM_SEGMENTS + 1);
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

int parseFramebufferUpdate(SOCKET socket_fd) {
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

        // Calculate size of pixel data based on encoding type (assuming 4 bytes per pixel)
        int pixelDataSize = byteArrayToInt16(width) * byteArrayToInt16(height) * 4;

        // Add the size of each rectangle header and pixel data to the total message size
        pixelSizeToRead += pixelDataSize;
    }

    printf("FramebufferUpdate message size: %d bytes\n", pixelSizeToRead);
    return pixelSizeToRead;
}

void writeBMP(char * imageData, int width, int height, const char* fileName) {
    FILE* file;
    if (fopen_s(&file, fileName, "wb") != 0) {
        perror("Error opening file");
        return;
    }

    // BMP header
    uint8_t bmpHeader[] = { 0x42, 0x4D }; // BM
    fwrite(bmpHeader, sizeof(uint8_t), 2, file);
    int fileSize = 14 + 40 + width * height * 4;
    fwrite(&fileSize, sizeof(int), 1, file); // File size
    int reserved = 0;
    fwrite(&reserved, sizeof(int), 1, file); // Reserved
    int dataOffset = 14 + 40;
    fwrite(&dataOffset, sizeof(int), 1, file); // Data offset

    // DIB header
    int headerSize = 40;
    fwrite(&headerSize, sizeof(int), 1, file); // Header size
    fwrite(&width, sizeof(int), 1, file); // Image width
    fwrite(&height, sizeof(int), 1, file); // Image height
    short colorPlanes = 1;
    fwrite(&colorPlanes, sizeof(short), 1, file); // Color planes
    short bitsPerPixel = 32;
    fwrite(&bitsPerPixel, sizeof(short), 1, file); // Bits per pixel
    int compressionMethod = 0;
    fwrite(&compressionMethod, sizeof(int), 1, file); // Compression method
    int imageSize = width * height * 4;
    fwrite(&imageSize, sizeof(int), 1, file); // Image size (bytes)
    int horizontalResolution = 0;
    fwrite(&horizontalResolution, sizeof(int), 1, file); // Horizontal resolution
    int verticalResolution = 0;
    fwrite(&verticalResolution, sizeof(int), 1, file); // Vertical resolution
    int numColorsInPalette = 0;
    fwrite(&numColorsInPalette, sizeof(int), 1, file); // Number of colors in the palette
    int importantColors = 0;
    fwrite(&importantColors, sizeof(int), 1, file); // Important colors

    // Write pixel data (BGR format) in reverse row order
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            int pixelIndex = (y * width + x) * 4; // Calculate pixel index in ARGB format
            fwrite(&imageData[pixelIndex + 2], sizeof(uint8_t), 1, file); // Blue
            fwrite(&imageData[pixelIndex + 1], sizeof(uint8_t), 1, file); // Green
            fwrite(&imageData[pixelIndex], sizeof(uint8_t), 1, file); // Red
            fwrite(&imageData[pixelIndex + 3], sizeof(uint8_t), 1, file); // Alpha
        }
    }

    fclose(file);
    printf("BMP image saved successfully.\n");
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
        std::cerr << "connection failed" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }
    // Receive server initialization message
    char serverInitMsg[12];
    int bytesReceived = recv(MySocket, serverInitMsg, sizeof(serverInitMsg), MSG_WAITALL);
    if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
        std::cerr << "error receiving server initialization message" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Send client protocol version message
    if (send(MySocket, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) == SOCKET_ERROR) {
        std::cerr << "error sending client initialization message" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Security handshake
    char securityHandshake[4];
    int numOfTypes = recv(MySocket, securityHandshake, sizeof(securityHandshake), 0);
    printf("%s\n", securityHandshake);
    send(MySocket, "\x01", 1,0); // ClientInit

    // Read framebuffer width (2 bytes) ServerInit
    char framebufferWidth[2];
    if (!recv(MySocket, framebufferWidth, 2,0)) {
        fprintf(stderr, "Error reading framebuffer width\n");
        return 1;
    }
    int16_t framebufferWidthInt = ((framebufferWidth[0] & 0xFF) << 8) | (framebufferWidth[1] & 0xFF);


    // Read framebuffer width (2 bytes) ServerInit
    char framebufferHeight[2];
    if (!recv(MySocket, framebufferHeight, 2, 0)) {
        fprintf(stderr, "Error reading framebuffer height\n");
        return 1;
    }
    int16_t framebufferHeightInt = ((framebufferHeight[0] & 0xFF) << 8) | (framebufferHeight[1] & 0xFF);


    // Read pixel format (16 bytes) ServerInit
    char pixelFormat[16];
    if (!recv(MySocket, pixelFormat, 16, MSG_WAITALL)) {
        fprintf(stderr, "Error reading pixel format\n");
        return 1;
    }


    // Read pixel format (4 bytes) ServerInit
    char nameLenght[4];
    if (!recv(MySocket, nameLenght, 4, MSG_WAITALL)) {
        fprintf(stderr, "Error reading pixel format\n");
        return 1;
    }
    uint32_t nameLengthInt = ((nameLenght[0] & 0xFF) << 24) |
        ((nameLenght[1] & 0xFF) << 16) |
        ((nameLenght[2] & 0xFF) << 8) |
        (nameLenght[3] & 0xFF);

    char name[32];
    if (!recv(MySocket, name, nameLengthInt, MSG_WAITALL)) {
        fprintf(stderr, "Error reading pixel format\n");
        return 1;
    }

    // Send encoding update request
    if (send(MySocket, ENCODING, 8, 0) == SOCKET_ERROR) {
        std::cerr << "error sending framebuffer update request" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }

    // Send encoding update request
    if (send(MySocket, FRAMEBUFFER_UPDATE_REQUEST, 10, 0) == SOCKET_ERROR) {
        std::cerr << "error sending framebuffer update request" << std::endl;
        closesocket(MySocket);
        WSACleanup();
        return 1;
    }
        
    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);
    // Main loop
    while (true)
    {
        int leftSizeForRead = parseFramebufferUpdate(MySocket);
        std::vector<char> framebufferUpdate(leftSizeForRead); // Allocate memory for receiving framebuffer update
        bytesReceived = recv(MySocket, framebufferUpdate.data(), leftSizeForRead, MSG_WAITALL);
        if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
            std::cerr << "error receiving framebuffer update" << std::endl;
           // break; // Exit loop if there's an error or the connection is closed
        }


        // Write received data to a file
        char filename[256];
        sprintf_s(filename, sizeof(filename), "framebuffer_update%d.bin", frameCount);
        writeBMP(framebufferUpdate.data(), framebufferWidthInt, framebufferHeightInt, filename);

        frameCount++;

        // Send encoding update request
        if (send(MySocket, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) == SOCKET_ERROR) {
            std::cerr << "error sending framebuffer update request" << std::endl;
            closesocket(MySocket);
            WSACleanup();
            return 1;
        }

        if (frameCount % 20 == 0) // 3 times per second
        {
            std::string log_file_path = find_newest_file(log_directory_path);

            // Start timing
            auto start = std::chrono::steady_clock::now();

            // Call the parsing function
            // Replace "file_path" and "regex_pattern" with appropriate values
            search_and_parse_last_occurrence(log_file_path, next_turn_pattern, 0);
            search_and_parse_last_occurrence(log_file_path, next_turn_distance_pattern, 1);
            
            // End timing
            auto end = std::chrono::steady_clock::now();

            // Calculate the duration
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Execution time: " << duration.count() << " milliseconds" << std::endl;

           // search_last_occurrence(log_file_path, next_turn_distance_pattern);

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
        char test[10]; // Adjust size accordingly
        snprintf(test, sizeof(test), "%.2f FPS\n", fps);
        print_string(-300, 200, test, 1, 1, 1, 200);

        //drawArrow();
        //drawRing();

        print_string_center(100, last_road.c_str(), 1, 1, 1, 100);
        print_string_center(50, last_turn_side.c_str(), 1, 1, 1, 100);
        print_string_center(0, last_event.c_str(), 1, 1, 1, 100);
        print_string_center(-50, last_turn_angle.c_str(), 1, 1, 1, 100);
        print_string_center(-100, last_turn_number.c_str(), 1, 1, 1, 100);

        print_string_center(150, last_distance_meters.c_str(), 1, 1, 1, 100);
        print_string_center(200, last_distance_seconds.c_str(), 1, 1, 1, 100);

        eglSwapBuffers(eglDisplay, eglSurface);
    }

    // Cleanup
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}
