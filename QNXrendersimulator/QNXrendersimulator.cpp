#include <Windows.h>
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

    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);
    // Main loop
    while (true)
    {
        frameCount++;
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

    return msg.wParam;
}
