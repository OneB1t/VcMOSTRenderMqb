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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "miniz.h"

// Vertex shader source
const char* vertexShaderSource =
"attribute vec2 position;    \n"
"attribute vec2 texCoord;     \n" // Add texture coordinate attribute
"varying vec2 v_texCoord;     \n" // Declare varying variable for texture coordinate
"void main()                  \n"
"{                            \n"
"   gl_Position = vec4(position, 0.0, 1.0); \n"
"   v_texCoord = texCoord;   \n"
"   gl_PointSize = 4.0;      \n" // Point size
"}                            \n";

const char* fragmentShaderSource =
"precision mediump float;\n"
"varying vec2 v_texCoord;\n"
"uniform sampler2D texture;\n"
"void main()\n"
"{\n"
"    gl_FragColor = texture2D(texture, v_texCoord);\n"
"}\n";

GLfloat vertices[] = {
   -0.8f,  0.7, 0.0f,  // Top Left
    0.8f,  0.7f, 0.0f,  // Top Right
    0.8f, -0.6f, 0.0f,  // Bottom Right
   -0.8f, -0.6f, 0.0f   // Bottom Left
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

const char ZLIB_ENCODING[] = {
    2,0,0,2,0,0,0,6,0,0,0,0
};

GLuint programObject;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;
GLfloat dotX = 0.0f;
GLfloat dotY = 0.0f;
int windowWidth = 800;
int windowHeight = 480;

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

int16_t byteArrayToInt16(const char* byteArray) {
    return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
}

int32_t byteArrayToInt32(const char* byteArray) {
    return ((int32_t)(byteArray[0] & 0xFF) << 24) | ((int32_t)(byteArray[1] & 0xFF) << 16) | ((int32_t)(byteArray[2] & 0xFF) << 8) | (byteArray[3] & 0xFF);
}

struct Command {
    const char *command;
    const char *error_message;
};

void execute_initial_commands() {
    struct Command commands[] = {
        {"/eso/bin/apps/dmdt dc 99 3", "Create new display table with context 3 failed with error"},
        {"/eso/bin/apps/dmdt sc 4 99", "Set display 4 (VC) to display table 99 failed with error"}
    };
    size_t num_commands = sizeof(commands) / sizeof(commands[0]);

    for (size_t i = 0; i < num_commands; ++i) {
        const char *command = commands[i].command;
        const char *error_message = commands[i].error_message;
        printf("Executing '%s'\n", command);

        // Execute the command
        int ret = system(command);
        if (ret != 0) {
            fprintf(stderr, "%s: %d\n", error_message, ret);
        }
    }
}


char* parseFramebufferUpdate(int socket_fd, int* frameBufferWidth, int* frameBufferHeight, z_stream strm, int* finalHeight)
{
    // Read message-type (1 byte) - not used, assuming it's always 0
    char messageType[1];
    if (!recv(socket_fd, messageType, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading message type\n");
        return NULL;
    }

    // Read padding (1 byte) - unused
    char padding[1];
    if (!recv(socket_fd, padding, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading padding\n");
        return NULL;
    }

    // Read number-of-rectangles (2 bytes)
    char numberOfRectangles[2];
    if (!recv(socket_fd, numberOfRectangles, 2, MSG_WAITALL)) {
        fprintf(stderr, "Error reading number of rectangles\n");
        return NULL;
    }

    // Calculate the total size of the message
    int totalLoadedSize = 0; // message-type + padding + number-of-rectangles
    char* finalFrameBuffer = (char*)malloc(1);
    int offset = 0;
    int ret = 0;
    // Now parse each rectangle
    for (int i = 0; i < byteArrayToInt16(numberOfRectangles); i++) {
        // Read rectangle header
        char xPosition[2];
        char yPosition[2];
        char width[2];
        char height[2];
        char encodingType[4]; // S32
        char compressedDataSize[4]; // S32

        if (!recv(socket_fd, xPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, yPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, width, 2, MSG_WAITALL) ||
            !recv(socket_fd, height, 2, MSG_WAITALL) ||
            !recv(socket_fd, encodingType, 4, MSG_WAITALL)) {
            fprintf(stderr, "Error reading rectangle header\n");
            return NULL;
        }

        *frameBufferWidth = byteArrayToInt16(width);
        *frameBufferHeight = byteArrayToInt16(height);
        *finalHeight = *finalHeight + *frameBufferHeight;
        if (encodingType[3] == '\x6') // ZLIB encoding
        {
            if (!recv(socket_fd, compressedDataSize, 4, MSG_WAITALL)) {
                fprintf(stderr, "Zlib compressedDataSize not found\n");
            }
            char* compressedData = (char*)malloc(byteArrayToInt32(compressedDataSize));
            int compresedDataReceivedSize = recv(socket_fd, compressedData, byteArrayToInt32(compressedDataSize), MSG_WAITALL);
            if (compresedDataReceivedSize < 0) {
                perror("error receiving framebuffer update rectangle");
                free(compressedData);
                return NULL;
            }

            // Allocate memory for decompressed data (assuming it's at most the same size as compressed)
            char* decompressedData = (char*)malloc(*frameBufferWidth * *frameBufferHeight * 4);
            if (!decompressedData) {
                perror("Error allocating memory for decompressed data");
                free(decompressedData);
                return NULL;
            }

            // Resize finalFrameBuffer to accommodate the appended data

            totalLoadedSize = totalLoadedSize + (*frameBufferWidth * *frameBufferHeight * 4);
            finalFrameBuffer = (char*)realloc(finalFrameBuffer, totalLoadedSize);

            // Decompress the data
            strm.avail_in = compresedDataReceivedSize;
            strm.next_in = (Bytef*)compressedData;
            strm.avail_out = *frameBufferWidth * *frameBufferHeight * 4; // Use the actual size of the decompressed data
            strm.next_out = (Bytef*)decompressedData;

            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret < 0 && ret != Z_BUF_ERROR) {
                fprintf(stderr, "Error: Failed to decompress zlib data: %s\n", strm.msg);
                inflateEnd(&strm);
                free(decompressedData);
                free(compressedData);
                return NULL;
            }


            memcpy(finalFrameBuffer + offset, decompressedData, *frameBufferWidth * *frameBufferHeight * 4);
            offset = offset + (*frameBufferWidth * *frameBufferHeight * 4);
            // Free memory allocated for framebufferUpdateRectangle
            free(compressedData);
            free(decompressedData);
        }
    }
    return finalFrameBuffer;
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

void print_string_center(float y, const char* text, float r, float g, float b, float size) {
    print_string(-stb_easy_font_width(text) * (size / 200), y, text, r, g, b, size);
}

// Initialize OpenGL ES
void Init() {
    // Load and compile shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Check for compile errors
    GLint vertexShaderCompileStatus;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vertexShaderCompileStatus);
    if (vertexShaderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("Vertex shader compilation failed: %s\n", infoLog);
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Check for compile errors
    GLint fragmentShaderCompileStatus;
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fragmentShaderCompileStatus);
    if (fragmentShaderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf("Fragment shader compilation failed: %s\n", infoLog);
    }

    // Create program object
    programObject = glCreateProgram();
    glAttachShader(programObject, vertexShader);
    glAttachShader(programObject, fragmentShader);
    glLinkProgram(programObject);

    // Check for linking errors
    GLint programLinkStatus;
    glGetProgramiv(programObject, GL_LINK_STATUS, &programLinkStatus);
    if (programLinkStatus != GL_TRUE) {
        char infoLog[512];
        glGetProgramInfoLog(programObject, 512, NULL, infoLog);
        printf("Program linking failed: %s\n", infoLog);
    }

    // Set clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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

    GLint maxSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);

    // Print the maximum texture size
    printf("Maximum texture size supported: %d\n", maxSize);

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
       eglBindAPI(EGL_OPENGL_ES_API);

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

    int sockfd;
    struct sockaddr_in serv_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    // Initialize socket structure
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    if (argc > 1) {
         // Use IP address from command line argument
    	 serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
     } else {
         // Fallback to default IP address if no argument is provided
    	 serv_addr.sin_addr.s_addr = inet_addr("10.173.189.62");
     }
    serv_addr.sin_port = htons(5900);

    // Connect to the VNC server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    // Receive server initialization message
    char serverInitMsg[12];
    int bytesReceived = recv(sockfd, serverInitMsg, sizeof(serverInitMsg), MSG_WAITALL);

    if (bytesReceived < 0) {
        std::cerr << "Error receiving server initialization message" << std::endl;
        return 1;
    }

    // Send client protocol version message
    if (send(sockfd, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) < 0) {
        std::cerr << "Error sending client initialization message" << std::endl;
        return 1;
    }

    // Security handshake
    char securityHandshake[4];
    recv(sockfd, securityHandshake, sizeof(securityHandshake), 0);
    printf("%s\n", securityHandshake);
    send(sockfd, "\x01", 1, 0); // ClientInit

    // Read framebuffer width and height
    char framebufferWidth[2];
    char framebufferHeight[2];

    if (!recv(sockfd, framebufferWidth, 2, 0) || !recv(sockfd, framebufferHeight, 2, 0)) {
        fprintf(stderr, "Error reading framebuffer dimensions\n");
        return 1;
    }



    // Read pixel format and name length
    char pixelFormat[16];
    char nameLength[4];

    if (!recv(sockfd, pixelFormat, sizeof(pixelFormat), MSG_WAITALL) ||
        !recv(sockfd, nameLength, sizeof(nameLength), MSG_WAITALL)) {
        fprintf(stderr, "Error reading pixel format or name length\n");
        return 1;
    }

    uint32_t nameLengthInt = (nameLength[0] << 24) | (nameLength[1] << 16) | (nameLength[2] << 8) | nameLength[3];

    // Read server name
    char name[32];
    if (!recv(sockfd, name, nameLengthInt, MSG_WAITALL)) {
        fprintf(stderr, "Error reading server name\n");
        return 1;
    }

    // Send encoding update requests
    if (send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0) < 0 ||
        send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
        std::cerr << "Error sending framebuffer update request" << std::endl;
        return 1;
    }
    int framebufferWidthInt = 0;
    int framebufferHeightInt = 0;
    int finalHeight = 0;


    int frameCount = 0;
    double fps = 0.0;
    time_t startTime = time(NULL);

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set texture parameters (optional)
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

     // Set texture wrapping mode (optional)
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

     z_stream strm;
     strm.zalloc = Z_NULL;
     strm.zfree = Z_NULL;
     strm.opaque = Z_NULL;


     int ret = inflateInit(&strm);
     if (ret != Z_OK) {
         fprintf(stderr, "Error: Failed to initialize zlib decompression\n");
         return NULL;
     }

     // Main loop
     while (true)
     {
         frameCount++;
         char * framebufferUpdate = parseFramebufferUpdate(sockfd, &framebufferWidthInt, &framebufferHeightInt, strm, &finalHeight);

         // Send encoding update request
         if (send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
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
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebufferWidthInt, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, framebufferUpdate);

         // Set vertex positions
         GLint positionAttribute = glGetAttribLocation(programObject, "position");
         glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, vertices);
         glEnableVertexAttribArray(positionAttribute);

         // Set texture coordinates
         GLint texCoordAttrib = glGetAttribLocation(programObject, "texCoord");
         glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texCoords);
         glEnableVertexAttribArray(texCoordAttrib);
         finalHeight = 0;
         // Draw quad
         glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
         eglSwapBuffers(eglDisplay, eglSurface);
         free(framebufferUpdate); // Free the dynamically allocated memory
     }

    // Cleanup
    glDeleteTextures(1, &textureID);
    eglSwapBuffers(eglDisplay, eglSurface);
    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
