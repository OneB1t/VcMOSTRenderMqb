#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <time.h>
#include "stb_easyfont.hh"
#include <regex.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>   // for dynamic loading functions such as dlopen, dlsym, and dlclose

#include <iostream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>

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

GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLfloat dotX = 0.0f;
GLfloat dotY = 0.0f;
int windowWidth = 800;
int windowHeight = 480;

// AA variables
std::string last_road = "UNKNOWN";
std::string last_turn_side = "UNKNOWN";
std::string last_event = "UNKNOWN";
std::string last_turn_angle = "0";
std::string last_turn_number = "0";
std::string last_valid = "1";
std::string last_distance_meters = "0";
std::string last_distance_seconds = "0";
std::string last_distance_valid = "1";

// esotrace AA data parsing
const char* log_directory_path = "/fs/sda0/esotrace_SD";
regex_t next_turn_pattern, next_turn_distance_pattern;
const char* next_turn_pattern_str = "onJob_updateNavigationNextTurnEvent : road='(.*?)', turnSide=(.*?), event=(.*?), turnAngle=(.*?), turnNumber=(.*?), valid=(.*?)";
const char* next_turn_distance_pattern_str = "onJob_updateNavigationNextTurnDistance : distanceMeters=(.*?), timeSeconds=(.*?), valid=(.*?)";

std::string icons_folder_path = "icons";

// AA sensors data location
std::string speedPos = "i:1304:216";


const char * find_newest_file(const std::string& directory, const std::string& extension = ".esotrace") {
    std::string newest_file;
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        std::cerr << "Error opening directory" << std::endl;
        return "";
    }

    time_t newest_mtime = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string filename = entry->d_name;
        std::string filepath = directory + "/" + filename;

        struct stat st;
        if (stat(filepath.c_str(), &st) == 0 && S_ISREG(st.st_mode) && filename.find(extension) != std::string::npos) {
            time_t modified_time = st.st_mtime;
            if (modified_time > newest_mtime) {
                newest_mtime = modified_time;
                newest_file = filepath;
            }
        }
    }

    closedir(dir);
    return newest_file.empty() ? NULL : newest_file.c_str();
}

void parse_log_line_next_turn(const char* match[]) {
    if (match != NULL) {
        last_road = std::string(match[1]);
        last_turn_side = std::string(match[2]);
        last_event = std::string(match[3]);
        last_turn_angle = std::string(match[4]);
        last_turn_number = std::string(match[5]);
        last_valid = std::string(match[6]);
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

void parse_log_line_next_turn_distance(const char* match[]) {
    if (match != NULL) {
        last_distance_meters = std::string(match[1]);
        last_distance_seconds = std::string(match[2]);
        last_distance_valid = std::string(match[3]);
    }
    else {
        // Set default values or handle the case where the pattern is not found
        last_distance_meters = "0";
        last_distance_seconds = "0";
        last_distance_valid = "0";
    }
}

void search_and_parse_last_occurrence(const char* file_path, regex_t regex_pattern, int parseoption) {
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        fprintf(stderr, "File '%s' does not exist.\n", file_path);
        return;
    }

    const size_t chunk_size = 8192;
    char chunk[chunk_size + 1];

    // Get file size
    fseek(file, 0, SEEK_END);
    long long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    while (ftell(file) > 0) {
        long long current_position = ftell(file);
        size_t offset = (current_position >= chunk_size) ? chunk_size : (size_t)current_position;
        fseek(file, -offset, SEEK_CUR);

        fread(chunk, 1, offset, file);
        chunk[offset] = '\0'; // Null-terminate the chunk

        regmatch_t match[10];
        if (regexec(&regex_pattern, chunk, 10, match, 0) == 0) {
            if (parseoption == 0) {
                parse_log_line_next_turn((const char**)&chunk[match[0].rm_so]);
            } else if (parseoption == 1) {
                parse_log_line_next_turn_distance((const char**)&chunk[match[0].rm_so]);
            }
            fclose(file);
            return;
        }

        regfree(&regex_pattern);

        if (current_position == 0) {
            break; // Reached the beginning of the file
        }
    }
    fclose(file);
}


static EGLenum checkErrorEGL(const char* msg)
{
    static const char* errmsg[] =
    {
        "EGL function succeeded",
        "EGL is not initialized, or could not be initialized, for the specified display",
        "EGL cannot access a requested resource",
        "EGL failed to allocate resources for the requested operation",
        "EGL fail to access an unrecognized attribute or attribute value was passed in an attribute list",
        "EGLConfig argument does not name a valid EGLConfig",
        "EGLContext argument does not name a valid EGLContext",
        "EGL current surface of the calling thread is no longer valid",
        "EGLDisplay argument does not name a valid EGLDisplay",
        "EGL arguments are inconsistent",
        "EGLNativePixmapType argument does not refer to a valid native pixmap",
        "EGLNativeWindowType argument does not refer to a valid native window",
        "EGL one or more argument values are invalid",
        "EGLSurface argument does not name a valid surface configured for rendering",
        "EGL power management event has occurred",
    };
    EGLenum error = eglGetError();
    fprintf(stderr, "%s: %s\n", msg, errmsg[error - EGL_SUCCESS]);
    return error;
}

const int NUM_SEGMENTS = 30;
// Function to render the ring
void drawRing() {
    // Clear the color buffer
    glClear(GL_COLOR_BUFFER_BIT);

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

    glUseProgram(programObject);
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

// Draw frame
void Draw() {
    // Set viewport
    //glViewport(0, 0, 800, 480);
    //glClear(GL_COLOR_BUFFER_BIT);
    //glUseProgram(programObject);
    dotX += 0.01f; // Increment x-coordinate to move dot horizontally
    if (dotX > 1.0f) {
        dotX = -1.0f;
    }
    GLfloat vertices[] = { dotX, dotY };
    //glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    //glEnableVertexAttribArray(0);
    //glDrawArrays(GL_POINTS, 0, 1);
}

int main(int argc, char *argv[]) {

    std::cout << "QNX MOST render v2" << std::endl;

    // Compile regular expressions
    if (regcomp(&next_turn_pattern, next_turn_pattern_str, REG_EXTENDED) != 0 ||
        regcomp(&next_turn_distance_pattern, next_turn_distance_pattern_str, REG_EXTENDED) != 0) {
        std::cerr << "Failed to compile regular expression" << std::endl;
        return 1;
    }

    void* func_handle = dlopen("libdisplayinit.so", RTLD_LAZY);
     if (!func_handle) {
         fprintf(stderr, "Error: %s\n", dlerror());
         return 1; // Exit with error
     }

    // Load the function pointer
        void (*display_init)(int, int) = (void (*)(int, int))dlsym(func_handle, "display_init");
       if (!display_init) {
           fprintf(stderr, "Error: %s\n", dlerror());
           dlclose(func_handle); // Close the handle before returning
           return 1; // Exit with error
       }

       // Call the function
       display_init(0,0); // DONE

       // Close the handle
       if (dlclose(func_handle) != 0) {
           fprintf(stderr, "Error: %s\n", dlerror());
           return 1; // Exit with error
       }


    eglDisplay = eglGetDisplay( EGL_DEFAULT_DISPLAY ); // DONE
    eglInitialize( eglDisplay, 0, 0); // DONE

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

    EGLConfig* configs = new EGLConfig[5];
    EGLint num_configs;
    EGLNativeWindowType windowEgl;
	int kdWindow;
    if (!eglChooseConfig(eglDisplay, config_attribs, configs, 1, &num_configs)) { // DONE
        fprintf(stderr, "Error: Failed to choose EGL configuration\n");
        return 1; // Exit with error
    }
    eglConfig = configs[0];

    void* func_handle2 = dlopen("libdisplayinit.so", RTLD_LAZY);
     if (!func_handle2) {
         fprintf(stderr, "Error: %s\n", dlerror());
         return 1; // Exit with error
     }

		void (*display_create_window)(
			EGLDisplay,  // DAT_00102f68: void* parameter
			EGLConfig,  // local_24[0]: void* parameter
			int,  // 800: void* parameter
			int,  // 0x1e0: void* parameter
			int,  // DAT_00102f0c: void* parameter
			EGLNativeWindowType*,  // &local_2c: void* parameter
			int*   // &DAT_00102f78: void* parameter
		) = (void (*)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*))dlsym(func_handle2, "display_create_window");
		if (!display_create_window) {
			   fprintf(stderr, "Error: %s\n", dlerror());
			   dlclose(func_handle2); // Close the handle before returning
			   return 1; // Exit with error
		   }

    	   display_create_window(eglDisplay,configs[0],800,480,3,&windowEgl,&kdWindow);


       // Close the handle
       if (dlclose(func_handle2) != 0) {
           fprintf(stderr, "Error: %s\n", dlerror());
           return 1; // Exit with error
       }

       eglSurface = eglCreateWindowSurface(eglDisplay, configs[0], windowEgl, 0);
       if (eglSurface == EGL_NO_SURFACE) {
       	checkErrorEGL("eglCreateWindowSurface");
           fprintf(stderr, "Create surface failed: 0x%x\n", eglSurface);
           exit(EXIT_FAILURE);
       }

       const EGLint context_attribs[] = {
               EGL_CONTEXT_CLIENT_VERSION, 2,
               EGL_NONE
       };
    eglContext = eglCreateContext(eglDisplay, configs[0], EGL_NO_CONTEXT, context_attribs);
    checkErrorEGL("eglCreateContext");
    if (eglContext == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return EXIT_FAILURE;
    }

    EGLBoolean madeCurrent = eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    if (madeCurrent == EGL_FALSE) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return EXIT_FAILURE;
    }


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
            const char * log_file_path = find_newest_file(log_directory_path);

            // Call the parsing function
            // Replace "file_path" and "regex_pattern" with appropriate values
            search_and_parse_last_occurrence(log_file_path, next_turn_pattern, 0);
            search_and_parse_last_occurrence(log_file_path, next_turn_distance_pattern, 1);

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
        char test[10]; // Adjust size accordingly
        snprintf(test, sizeof(test), "%.2f FPS\n", fps);
        print_string(-330, 200, test, 1, 1, 1,50);
        //drawArrow();
        drawRing();
        char speed[12]; // Adjust size accordingly
         snprintf(speed, sizeof(speed), "%i Frame", frameCount);

    	Draw();
    	//drawRing();
    	print_string(-150, 0, speed, 1, 1, 1,50);

    	print_string(0, -50, last_road.c_str(), 1, 1, 1, 200);
    	print_string(0, -100, last_turn_side.c_str(), 1, 1, 1, 200);
    	print_string(0, -150, last_event.c_str(), 1, 1, 1, 200);
    	print_string(0, -200, last_turn_angle.c_str(), 1, 1, 1, 200);
    	print_string(0, -250, last_turn_number.c_str(), 1, 1, 1, 200);

        eglSwapBuffers(eglDisplay, eglSurface);

    }

    // Swap buffers
    eglSwapBuffers(eglDisplay, eglSurface);
    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
