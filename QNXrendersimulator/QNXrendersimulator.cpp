#include <Windows.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <iostream>
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_easyfont.h"

GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLuint shaderProgram;

static char inputBuffer[9999]; // ~500 chars
GLfloat floatBuffer[sizeof(inputBuffer) / sizeof(GLfloat)];
GLfloat triangleBuffer[9999];
GLuint vertexBuffer; // Define vertex buffer object ID

int windowWidth = 800;
int windowHeight = 480;


// Shader source code for vertex shader
const char* vertexShaderSource = R"(
        attribute vec2 position;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";

// Shader source code for fragment shader
const char* fragmentShaderSource = R"(
        void main() {
            gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red color
        }
    )";

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

// Function to generate random float between min and max
float randomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}

void print_string(float x, float y, char* text, float r, float g, float b, float size) {
    int num_quads;
    num_quads = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

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

float M_PI = 3.1421;
// Function to render the ring
void drawRing() {
    // Set up radius and center of the ring
    float outerRadius = 0.5f;
    float innerRadius = 0.4f;
    float centerX = 0.0f;
    float centerY = 0.0f;

    const int NUM_SEGMENTS = 30;
    // Define vertices array for the outer circle
    GLfloat outerCircleVertices[(NUM_SEGMENTS + 1) * 2];
    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        float angle = ((float)i / NUM_SEGMENTS) * 2.0f * M_PI;
        outerCircleVertices[i * 2] = centerX + outerRadius * cos(angle);
        outerCircleVertices[i * 2 + 1] = centerY + outerRadius * sin(angle);
    }
    outerCircleVertices[NUM_SEGMENTS * 2] = outerCircleVertices[0];
    outerCircleVertices[NUM_SEGMENTS * 2 + 1] = outerCircleVertices[1];

    // Define vertices array for the inner circle
    GLfloat innerCircleVertices[(NUM_SEGMENTS + 1) * 2];
    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        float angle = ((float)i / NUM_SEGMENTS) * 2.0f * M_PI;
        innerCircleVertices[i * 2] = centerX + innerRadius * cos(angle);
        innerCircleVertices[i * 2 + 1] = centerY + innerRadius * sin(angle);
    }
    innerCircleVertices[NUM_SEGMENTS * 2] = innerCircleVertices[0];
    
    innerCircleVertices[NUM_SEGMENTS * 2 + 1] = innerCircleVertices[1];
    GLint positionAttribute = glGetAttribLocation(shaderProgram, "position");
    // Set up vertex attribute pointer for the outer circle
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, outerCircleVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the outer circle
    glDrawArrays(GL_LINE_STRIP, 0, NUM_SEGMENTS + 1);

    // Set up vertex attribute pointer for the inner circle
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, innerCircleVertices);
    glEnableVertexAttribArray(positionAttribute);

    // Draw the ring
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


    srand(time(nullptr)); // Seed the random number generator
    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);
    // Main loop
    MSG msg;
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

        char test[10]; // Adjust size accordingly
        snprintf(test, sizeof(test), "%.2f FPS\n", fps);
        glClear(GL_COLOR_BUFFER_BIT); // clear all
        //drawArrow();
        drawRing();
        print_string(-330, 200, test, 1, 1, 1,50);
        //print_string(-100, 100, test, 1, 1, 1, 50);
        eglSwapBuffers(eglDisplay, eglSurface);      

    }

    // Cleanup
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    return msg.wParam;
}
