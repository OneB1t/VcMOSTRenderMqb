#include <Windows.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <iostream>

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

GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLfloat dotX = 0.0f;
GLfloat dotY = 0.0f;

// Vertex shader source
const char* vertexShaderSource =
"attribute vec4 vPosition;    \n"
"void main()                  \n"
"{                            \n"
"   gl_Position = vPosition;  \n"
"   gl_PointSize = 10.0;      \n" // Point size
"}                            \n";

// Fragment shader source
const char* fragmentShaderSource =
"precision mediump float;  \n"
"void main()               \n"
"{                         \n"
"  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); \n" // Color
"}                         \n";


// Draw frame
void Draw() {
    // Set viewport
    glViewport(0, 0, 800, 480);

    // Clear the color buffer
    glClear(GL_COLOR_BUFFER_BIT);

    // Use the program object
    glUseProgram(programObject);

    // Move the dot horizontally
    dotX += 0.01f; // Increment x-coordinate to move dot horizontally

    // Wrap around to the left side when dot goes off the screen
    if (dotX > 1.0f) {
        dotX = -1.0f;
    }

    // Define vertex data (position of the dot)
    GLfloat vertices[] = { dotX, dotY };

    // Load the vertex data
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);

    // Draw a single point
    glDrawArrays(GL_POINTS, 0, 1);

    // Swap buffers
    eglSwapBuffers(eglDisplay, eglSurface);
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
    srand(time(NULL));

}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LPCWSTR className = L"OpenGL_ES_Window";

    // Register class
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    // Create window
    HWND hWnd = CreateWindowEx(0, className, L"OpenGL ES Window", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 480, nullptr, nullptr, hInstance, nullptr);

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
    
    Init();
    // Main loop
    MSG msg;
    while (true)
    {
        // Draw
        Draw();

    }

    // Cleanup
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    return msg.wParam;
}
