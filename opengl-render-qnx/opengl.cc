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
GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLfloat dotX = 0.0f;
GLfloat dotY = 0.0f;

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

    while(true)
    {
    	// Draw
    	Draw();

    }

    // Swap buffers
    eglSwapBuffers(eglDisplay, eglSurface);
    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
