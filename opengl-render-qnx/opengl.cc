#include <cstdlib>
#include <iostream>


#include <EGL/egl.h>
#include <GLES2/gl2.h>

// Define vertex shader source
const char* vertexShaderSource =
    "attribute vec2 position;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

// Define fragment shader source
const char* fragmentShaderSource =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n" // Set color to white
    "}\n";


int main(int argc, char *argv[]) {

    std::cout << "QNX MOST render" << std::endl;

    // Get Display Type
    EGLDisplay eglDisplay = eglGetDisplay( EGL_DEFAULT_DISPLAY );
    eglInitialize( eglDisplay, NULL, NULL);

    // typical high-quality attrib list
    EGLint defaultAttribList[] = {
    // 32 bit color
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    // at least 24 bit depth
    EGL_DEPTH_SIZE, 24,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    // want opengl-es 2.x conformant CONTEXT
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
    };

    EGLint numConfigs;
    EGLConfig config;

    eglChooseConfig(eglDisplay, defaultAttribList,
                   &config, 1, &numConfigs);


    EGLSurface surface;
    EGLint attributes[] = {EGL_WIDTH, 800, EGL_HEIGHT, 480, EGL_NONE};
    surface = eglCreateWindowSurface(eglDisplay, config, 0, attributes);
    if (surface == EGL_NO_SURFACE) {
        std::cerr << "Failed to create EGL window surface" << std::endl;
        return EXIT_FAILURE;
    }

    EGLContext context = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return EXIT_FAILURE;
    }

    EGLBoolean madeCurrent = eglMakeCurrent(eglDisplay, surface, surface, context);
    if (madeCurrent == EGL_FALSE) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return EXIT_FAILURE;
    }

    // Load and compile shaders
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);

        // Create shader program
        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        glUseProgram(shaderProgram);

        // Set up vertex data and attributes
        GLfloat vertices[] = {
            0.0f, 0.0f  // Center of the screen
        };
        GLuint vbo;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
        glEnableVertexAttribArray(posAttrib);
        glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

        while(true)
        {
			// Clear the color buffer
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw the point
			glDrawArrays(GL_POINTS, 0, 1);
        }
        // Swap buffers (not necessary for pbuffer surface)
        // eglSwapBuffers(display, surface);

    // Swap buffers
    eglSwapBuffers(eglDisplay, surface);
    eglDestroySurface(eglDisplay, surface);
    eglDestroyContext(eglDisplay, context);
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
