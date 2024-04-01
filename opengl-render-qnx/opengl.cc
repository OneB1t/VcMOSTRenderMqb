#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <time.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>   // for dynamic loading functions such as dlopen, dlsym, and dlclose

#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_easyfont.h"

int windowWidth = 800;
int windowHeight = 480;

// Vertex shader source
const char* vertexShaderSource =
    "attribute vec4 vPosition;                  \n"
    "void main()                                \n"
    "{                                          \n"
    "   gl_Position = vec4(position, 0.0, 1.0); \n"
    "}                                          \n";

// Fragment shader source
const char* fragmentShaderSource =
    "precision mediump float;  \n"
    "void main()               \n"
    "{                         \n"
    "  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);  \n"
    "}                         \n";

GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLuint shaderProgram;

// Compile shader function
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    // Check for compilation errors
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Shader compilation error: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void print_string(float x, float y, char* text, float r, float g, float b, float size) {

    char inputBuffer[9999]; // ~500 chars
    GLfloat floatBuffer[sizeof(inputBuffer) / sizeof(GLfloat)];
    GLfloat triangleBuffer[9999];
    stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

    // Copying data from inputBuffer to floatBuffer
    for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); ++i) {
        floatBuffer[i] = *((GLfloat*)(inputBuffer + i * sizeof(GLfloat)));
    }
    // calculate movement inside viewport
    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;

    int triangleIndex = 0; // Index to keep track of the current position in the triangleBuffer
    // Convert each quad into two triangles and also apply size and offset to draw it to correct place
    for (int i = 0; i < sizeof(floatBuffer) / sizeof(GLfloat); i += 8) {
        // Triangle 1
        triangleBuffer[triangleIndex++] = floatBuffer[i] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 1] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 2] / size + +ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 3] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 4] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 5] / size * -1 + ndcMovementY;

        //// Triangle 2
        triangleBuffer[triangleIndex++] = floatBuffer[i] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 1] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 4] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 5] / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 6] / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = floatBuffer[i + 7] / size * -1 + ndcMovementY;

    }

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangleBuffer), triangleBuffer, GL_STATIC_DRAW);

    // Specify the layout of the vertex data
    GLint positionAttribute = glGetAttribLocation(shaderProgram, "position");
    glEnableVertexAttribArray(positionAttribute);
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    // Render the triangle
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex);

    glDeleteBuffers(1, &vbo);
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

    GLint positionAttribute = glGetAttribLocation(shaderProgram, "position");
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
    GLint positionAttribute = glGetAttribLocation(shaderProgram, "position");
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

// Initialize OpenGL ES
void Init() {
    // Load and compile shaders
	// OpenGL ES initialization
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Compile vertex shader
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);

	// Compile fragment shader
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);

	// Create shader program and link shaders
	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	glUseProgram(shaderProgram);

}

int main(int argc, char *argv[]) {

    std::cout << "QNX MOST render v2" << std::endl;

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

    	   display_create_window(eglDisplay,configs[0],windowWidth,windowHeight,3,&windowEgl,&kdWindow);


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

       EGLint contextAttribs[] = {
       EGL_CONTEXT_CLIENT_VERSION, 2, // Request OpenGL ES 2.0
       EGL_NONE // Indicates the end of the attribute list
       };
    eglContext = eglCreateContext(eglDisplay, configs[0], EGL_NO_CONTEXT, contextAttribs);
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
    srand(time(NULL)); // Seed the random number generator
    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);
    while (true)
    {
        frameCount++;
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

        //drawArrow();
        drawRing();
        print_string(-330, 200, test, 1, 1, 1,50);
        char speed[15] = ""; // Adjust size accordingly
         snprintf(speed, sizeof(speed), "%i Frame", frameCount);

         print_string(-150, -200, speed, 1, 1, 1,50);
        //print_string(-100, 100, test, 1, 1, 1, 50);
        eglSwapBuffers(eglDisplay, eglSurface);

    }

    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
