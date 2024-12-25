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
#include <dlfcn.h>   // for dynamic loading functions such as dlopen, 	lsym, and dlclose
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
#include <unistd.h>
#include <sys/time.h>

#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <netinet/tcp_var.h>

#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18  // how long for loss retry before timeout [ms]
#endif

//GLES setup
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

// VNC shaders
// Vertex shader source
const char* vertexShaderSource = "attribute vec2 position;    \n"
	"attribute vec2 texCoord;     \n" // Add texture coordinate attribute
		"varying vec2 v_texCoord;     \n" // Declare varying variable for texture coordinate
		"void main()                  \n"
		"{                            \n"
		"   gl_Position = vec4(position, 0.0, 1.0); \n"
		"   v_texCoord = texCoord;   \n"
		"   gl_PointSize = 4.0;      \n" // Point size
		"}                            \n";

const char* fragmentShaderSource = "precision mediump float;\n"
	"varying vec2 v_texCoord;\n"
	"uniform sampler2D texture;\n"
	"void main()\n"
	"{\n"
	"    gl_FragColor = texture2D(texture, v_texCoord);\n"
	"}\n";

// Text Rendering shaders
const char* vertexShaderSourceText = "attribute vec2 position;    \n"
	"void main()                  \n"
	"{                            \n"
	"   gl_Position = vec4(position, 0.0, 1.0); \n"
	"   gl_PointSize = 1.0;      \n" // Point size
		"}                            \n";

// Fragment shader source
const char* fragmentShaderSourceText = "precision mediump float;\n"
	"void main()               \n"
	"{                         \n"
	"  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); \n" // Color
		"}                         \n";

GLfloat landscapeVertices[] = { -0.8f, 0.73, 0.0f, // Top Left
		0.8f, 0.73f, 0.0f, // Top Right
		0.8f, -0.63f, 0.0f, // Bottom Right
		-0.8f, -0.63f, 0.0f // Bottom Left
		};
GLfloat portraitVertices[] = { -0.8f, 1.0f, 0.0f, // Top Left
		0.8f, 1.0f, 0.0f, // Top Right
		0.8f, -0.67f, 0.0f, // Bottom Right
		-0.8f, -0.67f, 0.0f // Bottom Left
		};
// Texture coordinates
GLfloat landscapeTexCoords[] = { 0.0f, 0.07f, // Bottom Left
		0.90f, 0.07f, // Bottom Right
		0.90f, 1.0f, // Top Right
		0.0f, 1.0f // Top Left
		};
// Texture coordinates
GLfloat portraitTexCoords[] = { 0.0f, 0.0f, // Bottom Left
		0.63f, 0.0f, // Bottom Right
		0.63f, 0.3f, // Top Right
		0.0f, 0.3f // Top Left
		};

// Constants for VNC protocol
const char* PROTOCOL_VERSION = "RFB 003.003\n"; // Client initialization message
const char FRAMEBUFFER_UPDATE_REQUEST[] = { 3, 0, 0, 0, 0, 0, 255, 255, 255,
		255 };
const char CLIENT_INIT[] = { 1 };
const char ZLIB_ENCODING[] = { 2, 0, 0, 2, 0, 0, 0, 6, 0, 0, 0, 0 };

// SETUP SECTION
int windowWidth = 800;
int windowHeight = 480;

const char* VNC_SERVER_IP_ADDRESS = "10.173.189.62";
const int VNC_SERVER_PORT = 5900;

const char* EXLAP_SERVER_IP_ADDRESS = "127.0.0.1";
const int EXLAP_SERVER_PORT = 25010;

// QNX SPECIFIC SECTION
static EGLenum checkErrorEGL(const char* msg) {
	static const char
			* errmsg[] = {
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
					"EGL power management event has occurred", };
	EGLenum error = eglGetError();
	fprintf(stderr, "%s: %s\n", msg, errmsg[error - EGL_SUCCESS]);
	return error;
}

struct Command {
	const char* command;
	const char* error_message;
};

// CODE FROM HERE IS THE SAME FOR WINDOWS OR QNX
void execute_initial_commands() {
	struct Command commands[] = { { "/eso/bin/apps/dmdt dc 99 3",
			"Create new display table with context 3 failed with error" }, {
			"/eso/bin/apps/dmdt sc 4 99",
			"Set display 4 (VC) to display table 99 failed with error" } };
	size_t num_commands = sizeof(commands) / sizeof(commands[0]);

	for (size_t i = 0; i < num_commands; ++i) {
		const char* command = commands[i].command;
		const char* error_message = commands[i].error_message;
		printf("Executing '%s'\n", command);

		// Execute the command
		int ret = system(command);
		if (ret != 0) {
			fprintf(stderr, "%s: %d\n", error_message, ret);
		}
	}
}

void execute_final_commands() {
	struct Command commands[] = { { "/eso/bin/apps/dmdt sc 4 70",
			"Set display 4 (VC) to display table 70 failed with error" } };
	size_t num_commands = sizeof(commands) / sizeof(commands[0]);

	for (size_t i = 0; i < num_commands; ++i) {
		const char* command = commands[i].command;
		const char* error_message = commands[i].error_message;
		printf("Executing '%s'\n", command);

		// Execute the command
		int ret = system(command);
		if (ret != 0) {
			fprintf(stderr, "%s: %d\n", error_message, ret);
		}
	}
}

std::string readPersistanceData(const std::string& position) {
	std::string command = "";
#ifdef _WIN32
	return "NC"; // not connected
#else
	command = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc " + position;
#endif

	FILE* pipe = popen(command.c_str(), "r");
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
	pclose(pipe);

	std::cout << result;
	return result;
}

int16_t byteArrayToInt16(const char* byteArray) {
	return ((int16_t) (byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
}

int32_t byteArrayToInt32(const char* byteArray) {
	return ((int32_t) (byteArray[0] & 0xFF) << 24) | ((int32_t) (byteArray[1]
			& 0xFF) << 16) | ((int32_t) (byteArray[2] & 0xFF) << 8)
			| (byteArray[3] & 0xFF);
}

// Initialize OpenGL ES
void Init() {
	// Load and compile VNC shaders
	GLuint vertexShaderVncRender = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShaderVncRender, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShaderVncRender);

	// Check for compile errors
	GLint vertexShaderCompileStatus;
	glGetShaderiv(vertexShaderVncRender, GL_COMPILE_STATUS,
			&vertexShaderCompileStatus);
	if (vertexShaderCompileStatus != GL_TRUE) {
		char infoLog[512];
		glGetShaderInfoLog(vertexShaderVncRender, 512, NULL, infoLog);
		printf("Vertex shader compilation failed: %s\n", infoLog);
	}

	GLuint fragmentShaderVncRender = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShaderVncRender, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShaderVncRender);

	// Check for compile errors
	GLint fragmentShaderCompileStatus;
	glGetShaderiv(fragmentShaderVncRender, GL_COMPILE_STATUS,
			&fragmentShaderCompileStatus);
	if (fragmentShaderCompileStatus != GL_TRUE) {
		char infoLog[512];
		glGetShaderInfoLog(fragmentShaderVncRender, 512, NULL, infoLog);
		printf("Fragment shader compilation failed: %s\n", infoLog);
	}

	// Load and compile Text Render shaders
	GLuint vertexShaderTextRender = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShaderTextRender, 1, &vertexShaderSourceText, NULL);
	glCompileShader(vertexShaderTextRender);

	// Check for compile errors
	GLint vertexShaderTextRenderCompileStatus;
	glGetShaderiv(vertexShaderTextRender, GL_COMPILE_STATUS,
			&vertexShaderTextRenderCompileStatus);
	if (vertexShaderTextRenderCompileStatus != GL_TRUE) {
		char infoLog[512];
		glGetShaderInfoLog(vertexShaderTextRender, 512, NULL, infoLog);
		printf("Vertex shader compilation failed: %s\n", infoLog);
	}

	GLuint fragmentShaderTextRender = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShaderTextRender, 1, &fragmentShaderSourceText, NULL);
	glCompileShader(fragmentShaderTextRender);

	// Check for compile errors
	GLint fragmentShaderCompileStatusTextRender;
	glGetShaderiv(fragmentShaderTextRender, GL_COMPILE_STATUS,
			&fragmentShaderCompileStatusTextRender);
	if (fragmentShaderCompileStatus != GL_TRUE) {
		char infoLog[512];
		glGetShaderInfoLog(fragmentShaderTextRender, 512, NULL, infoLog);
		printf("Fragment shader compilation failed: %s\n", infoLog);
	}

	// Create program object
	programObject = glCreateProgram();

	glAttachShader(programObject, vertexShaderVncRender);
	glAttachShader(programObject, fragmentShaderVncRender);
	glLinkProgram(programObject);

	programObjectTextRender = glCreateProgram();
	glAttachShader(programObjectTextRender, vertexShaderTextRender);
	glAttachShader(programObjectTextRender, fragmentShaderTextRender);
	glLinkProgram(programObjectTextRender);

	// Check for linking errors
	GLint programLinkStatus;
	glGetProgramiv(programObject, GL_LINK_STATUS, &programLinkStatus);
	if (programLinkStatus != GL_TRUE) {
		char infoLog[512];
		glGetProgramInfoLog(programObject, 512, NULL, infoLog);
		printf("Program linking failed: %s\n", infoLog);
	}

	glGetProgramiv(programObjectTextRender, GL_LINK_STATUS, &programLinkStatus);
	if (programLinkStatus != GL_TRUE) {
		char infoLog[512];
		glGetProgramInfoLog(programObjectTextRender, 512, NULL, infoLog);
		printf("Program linking failed: %s\n", infoLog);
	}

	// Set clear color to black
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

}

char* parseFramebufferUpdate(int socket_fd, int* frameBufferWidth,
		int* frameBufferHeight, z_stream strm, int* finalHeight) {
	// Read message-type (1 byte) - not used, assuming it's always 0
	// Message type
	char messageType[1];
	if (!recv(socket_fd, messageType, 1, MSG_WAITALL)) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			std::cerr << "Receive timeout occurred while reading message type"
					<< std::endl;
		} else {
			std::cerr << "Error reading message type: " << strerror(errno)
					<< std::endl;
		}
		close(socket_fd);
		return NULL;
	}

	// Read padding (1 byte) - unused
	char padding[1];
	if (!recv(socket_fd, padding, 1, MSG_WAITALL)) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			std::cerr << "Receive timeout occurred while reading padding"
					<< std::endl;
		} else {
			std::cerr << "Error reading padding: " << strerror(errno)
					<< std::endl;
		}
		close(socket_fd);
		return NULL;
	}

	// Read number-of-rectangles (2 bytes)
	char numberOfRectangles[2];
	if (!recv(socket_fd, numberOfRectangles, 2, MSG_WAITALL)) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			std::cerr
					<< "Receive timeout occurred while reading number of rectangles"
					<< std::endl;
		} else {
			std::cerr << "Error reading number of rectangles: " << strerror(
					errno) << std::endl;
		}
		close(socket_fd);
		return NULL;
	}

	// Calculate the total size of the message
	int totalLoadedSize = 0; // message-type + padding + number-of-rectangles
	char* finalFrameBuffer = (char*) malloc(1);
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

		if (!recv(socket_fd, xPosition, 2, MSG_WAITALL) || !recv(socket_fd,
				yPosition, 2, MSG_WAITALL) || !recv(socket_fd, width, 2,
				MSG_WAITALL) || !recv(socket_fd, height, 2, MSG_WAITALL)
				|| !recv(socket_fd, encodingType, 4, MSG_WAITALL)) {
			fprintf(stderr, "Error reading rectangle header\n");
			return NULL;
		}

		*frameBufferWidth = byteArrayToInt16(width);
		*frameBufferHeight = byteArrayToInt16(height);
		*finalHeight = *finalHeight + *frameBufferHeight;
		if (encodingType[3] == '\x6') // ZLIB encoding
		{
			// Read zlib compressed data size with timeout handling
			if (!recv(socket_fd, compressedDataSize, 4, MSG_WAITALL)) {
				if (errno == EWOULDBLOCK || errno == EAGAIN) {
					std::cerr
							<< "Receive timeout occurred while reading zlib compressed data size"
							<< std::endl;
				} else {
					std::cerr << "Zlib compressed data size not found: "
							<< strerror(errno) << std::endl;
				}
				close(socket_fd);
				return NULL;
			}

			char* compressedData = (char*) malloc(byteArrayToInt32(
					compressedDataSize));

			int compresedDataReceivedSize = recv(socket_fd, compressedData,
					byteArrayToInt32(compressedDataSize), MSG_WAITALL);
			if (compresedDataReceivedSize < 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN) {
					std::cerr
							<< "Receive timeout occurred while receiving compressed data"
							<< std::endl;
				} else {
					perror("Error receiving compressed data");
				}
				free(compressedData);
				close(socket_fd);
				return NULL;
			}

			// Allocate memory for decompressed data (assuming it's at most the same size as compressed)
			char* decompressedData = (char*) malloc(*frameBufferWidth
					* *frameBufferHeight * 4);
			if (!decompressedData) {
				perror("Error allocating memory for decompressed data");
				free(decompressedData);
				close(socket_fd);
				return NULL;
			}

			// Resize finalFrameBuffer to accommodate the appended data
			totalLoadedSize = totalLoadedSize + (*frameBufferWidth
					* *frameBufferHeight * 4);
			finalFrameBuffer = (char*) realloc(finalFrameBuffer,
					totalLoadedSize);

			// Decompress the data
			strm.avail_in = compresedDataReceivedSize;
			strm.next_in = (Bytef*) compressedData;
			strm.avail_out = *frameBufferWidth * *frameBufferHeight * 4; // Use the actual size of the decompressed data
			strm.next_out = (Bytef*) decompressedData;

			ret = inflate(&strm, Z_NO_FLUSH);

			if (ret < 0 && ret != Z_BUF_ERROR) {
				fprintf(stderr, "Error: Failed to decompress zlib data: %s\n",
						strm.msg);
				inflateEnd(&strm);
				free(decompressedData);
				free(compressedData);
				close(socket_fd);
				return NULL;
			}

			memcpy(finalFrameBuffer + offset, decompressedData,
					*frameBufferWidth * *frameBufferHeight * 4);
			offset = offset + (*frameBufferWidth * *frameBufferHeight * 4);
			// Free memory allocated for framebufferUpdateRectangle
			free(compressedData);
			free(decompressedData);
		}
	}
	return finalFrameBuffer;
}
void print_string(float x, float y, const char* text, float r, float g,
		float b, float size) {
	char inputBuffer[2000] = { 0 }; // ~20s chars
	GLfloat triangleBuffer[2000] = { 0 };
	int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer,
			sizeof(inputBuffer));

	// calculate movement inside viewport
	float ndcMovementX = (2.0f * x) / windowWidth;
	float ndcMovementY = (2.0f * y) / windowHeight;

	int triangleIndex = 0; // Index to keep track of the current position in the triangleBuffer
	// Convert each quad into two triangles and also apply size and offset to draw it to correct place
	for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); i += 8) {
		// Triangle 1
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[i * sizeof(GLfloat)])
						/ size + ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 1)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 2)
						* sizeof(GLfloat)]) / size + +ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 3)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 4)
						* sizeof(GLfloat)]) / size + ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 5)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;

		//// Triangle 2
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[i * sizeof(GLfloat)])
						/ size + ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 1)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 4)
						* sizeof(GLfloat)]) / size + ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 5)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 6)
						* sizeof(GLfloat)]) / size + ndcMovementX;
		triangleBuffer[triangleIndex++]
				= *reinterpret_cast<GLfloat*> (&inputBuffer[(i + 7)
						* sizeof(GLfloat)]) / size * -1 + ndcMovementY;

	}

	glUseProgram(programObjectTextRender);
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(triangleBuffer), triangleBuffer,
			GL_STATIC_DRAW);

	// Specify the layout of the vertex data
	GLint positionAttribute = glGetAttribLocation(programObject, "position");
	glEnableVertexAttribArray(positionAttribute);
	glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	// glEnableVertexAttribArray(0);

	// Render the triangle
	glDrawArrays(GL_TRIANGLES, 0, triangleIndex);

	glDeleteBuffers(1, &vbo);
}

void print_string_center(float y, const char* text, float r, float g, float b,
		float size) {
	print_string(-stb_easy_font_width(text) * (size / 200), y, text, r, g, b,
			size);
}

// Helper function to parse a line for GLfloat arrays
void parseLineArray(char *line, const char *key, GLfloat *dest, int count) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *values = strchr(line, '=');
        if (values) {
            values++; // Skip '='
            for (int i = 0; i < count; i++) {
                dest[i] = strtof(values, &values); // Parse floats
            }
        }
    }
}

// Helper function to parse a line for integers
void parseLineInt(char *line, const char *key, int *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value) {
            *dest = atoi(value + 1); // Parse integer
        }
    }
}

// Function to load the configuration file
void loadConfig(const char *filename) {
	FILE *file = fopen(filename, "r");
	if (!file) {
		printf("Config file not found. Using defaults.\n");
		return;
	}

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        parseLineArray(line, "landscapeVertices", landscapeVertices, 12);
        parseLineArray(line, "portraitVertices", portraitVertices, 12);
        parseLineArray(line, "landscapeTexCoords", landscapeTexCoords, 8);
        parseLineArray(line, "portraitTexCoords", portraitTexCoords, 8);
        parseLineInt(line, "windowWidth", &windowWidth);
        parseLineInt(line, "windowHeight", &windowHeight);
    }

	fclose(file);
}

// Function to print GLfloat arrays
void printArray(const char *label, GLfloat *array, int count, int elementsPerLine) {
    printf("%s:\n", label);
    for (int i = 0; i < count; i++) {
        printf("%f ", array[i]);
        if ((i + 1) % elementsPerLine == 0) printf("\n");
    }
    printf("\n");
}

// MAIN SECTION IS DIFFERENT ON QNX
int main(int argc, char* argv[]) {

	printf("QNX MOST VNC render 0.0.9 \n");
	printf("Loading libdisplayinit.so \n");
	printf("Loading config.txt \n");
    // Load config
    loadConfig("config.txt");

    // Print loaded or default values
    printArray("Landscape vertices", landscapeVertices, 12, 3);
    printArray("Portrait vertices", portraitVertices, 12, 3);
    printArray("Landscape texture coordinates", landscapeTexCoords, 8, 2);
    printArray("Portrait texture coordinates", portraitTexCoords, 8, 2);
    printf("windowWidth = %d;\n", windowWidth);
    printf("windowHeight = %d;\n", windowHeight);


	void* func_handle = dlopen("libdisplayinit.so", RTLD_LAZY);
	if (!func_handle) {
		fprintf(stderr, "Error using libdisplayinit.so: %s\n", dlerror());
		return 1; // Exit with error
	}

	// Load the function pointer
	void
			(*display_init)(int, int) = (void (*)(int, int))dlsym(func_handle, "display_init");
	if (!display_init) {
		fprintf(
				stderr,
				"Error while loading libdisplayinit.so function display_init: %s\n",
				dlerror());
		dlclose(func_handle); // Close the handle before returning
		return 1; // Exit with error
	}

	printf("Calling method display_init from libdisplayinit.so \n");
	// Call the function
	display_init(0, 0); // DONE

	// Close the handle
	if (dlclose(func_handle) != 0) {
		fprintf(stderr, "Error while closing libdisplayinit.so handle: %s\n",
				dlerror());
		return 1; // Exit with error
	}

	printf("OpenGL ES2.0 initialization started \n");
	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY); // DONE
	eglInitialize(eglDisplay, 0, 0); // DONE

	GLint maxSize;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
	printf("Maximum OpenGL texture size supported: %d\n", maxSize);

	// Specify EGL configurations
	EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE,
			1, EGL_GREEN_SIZE, 1, EGL_BLUE_SIZE, 1, EGL_ALPHA_SIZE, 1,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };

	EGLConfig* configs = new EGLConfig[5];
	EGLint num_configs;
	EGLNativeWindowType windowEgl;
	int kdWindow;
	if (!eglChooseConfig(eglDisplay, config_attribs, configs, 1, &num_configs)) { // DONE
		fprintf(stderr, "Error: Failed to choose EGL configuration\n");
		return 1; // Exit with error
	}
	eglConfig = configs[0];

	void* func_handle_display_create_window = dlopen("libdisplayinit.so",
			RTLD_LAZY);
	if (!func_handle_display_create_window) {
		fprintf(stderr, "Error: %s\n", dlerror());
		return 1; // Exit with error
	}

	void (*display_create_window)(EGLDisplay, // DAT_00102f68: void* parameter
			EGLConfig, // local_24[0]: void* parameter
			int, // 800: void* parameter
			int, // 0x1e0: void* parameter
			int, // DAT_00102f0c: void* parameter
			EGLNativeWindowType*, // &local_2c: void* parameter
			int* // &DAT_00102f78: void* parameter
			) = (void (*)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*))dlsym(func_handle_display_create_window, "display_create_window");
	if (!display_create_window) {
		fprintf(stderr, "Error: %s\n", dlerror());
		dlclose(func_handle_display_create_window); // Close the handle before returning
		return 1; // Exit with error
	}
	printf("libdisplayinit.so: display_create_window \n");
	display_create_window(eglDisplay, configs[0], windowWidth, windowHeight, 3,
			&windowEgl, &kdWindow);

	// Close the handle
	if (dlclose(func_handle_display_create_window) != 0) {
		fprintf(stderr, "Error: %s\n", dlerror());
		return 1; // Exit with error
	}
	printf("OpenGLES: eglCreateWindowSurface \n");
	eglSurface = eglCreateWindowSurface(eglDisplay, configs[0], windowEgl, 0);
	if (eglSurface == EGL_NO_SURFACE) {
		checkErrorEGL("eglCreateWindowSurface");
		fprintf(stderr, "Create surface failed: 0x%x\n", eglSurface);
		exit(EXIT_FAILURE);
	}
	eglBindAPI(EGL_OPENGL_ES_API);

	const EGLint
			context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	printf("OpenGLES: eglCreateContext \n");
	eglContext = eglCreateContext(eglDisplay, configs[0], EGL_NO_CONTEXT,
			context_attribs);
	checkErrorEGL("eglCreateContext");
	if (eglContext == EGL_NO_CONTEXT) {
		std::cerr << "Failed to create EGL context" << std::endl;
		return EXIT_FAILURE;
	}

	EGLBoolean madeCurrent = eglMakeCurrent(eglDisplay, eglSurface, eglSurface,
			eglContext);
	if (madeCurrent == EGL_FALSE) {
		std::cerr << "Failed to make EGL context current" << std::endl;
		return EXIT_FAILURE;
	}

	// THIS SECTIONS IS THE SAME ON QNX AND WINDOWS
	// Initialize OpenGL ES
	Init();
	while (true) {
		printf("Main loop executed \n");
		execute_final_commands();
		int sockfd;
		fd_set write_fds;
		int result;
		int so_error;
		socklen_t len = sizeof(so_error);
		struct sockaddr_in serv_addr;

		int keepalive = 1; // Enable keepalive
		int keepidle = 2; // Idle time before sending the first keepalive probe (in seconds)

		// Create socket
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			perror("Error opening socket");
			close(sockfd);
			continue;
		}

		int mib[4];
		int ival = 0;

		mib[0] = CTL_NET;
		mib[1] = AF_INET;
		mib[2] = IPPROTO_TCP;
		mib[3] = TCPCTL_KEEPCNT;
		ival = 3;
		sysctl(mib, 4, NULL, NULL, &ival, sizeof(ival));

		mib[0] = CTL_NET;
		mib[1] = AF_INET;
		mib[2] = IPPROTO_TCP;
		mib[3] = TCPCTL_KEEPINTVL;
		ival = 2;
		sysctl(mib, 4, NULL, NULL, &ival, sizeof(ival));

		// Enable TCP keepalive
		if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive,
				sizeof(keepalive)) < 0) {
			perror("setsockopt SO_KEEPALIVE");
			close(sockfd);
			continue;
		}

		// Set the time (in seconds) the connection needs to remain idle before TCP starts sending keepalive probes
		if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle,
				sizeof(keepidle)) < 0) {
			perror("setsockopt TCP_KEEPIDLE");
			close(sockfd);
			continue;
		}

		printf("TCP keepalive enabled and configured on the socket.\n");

		// Set socket to non-blocking mode
		int flags = fcntl(sockfd, F_GETFL, 0);
		if (flags < 0) {
			perror("fcntl F_GETFL");
			close(sockfd);
			continue;
		}
		if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
			perror("fcntl F_SETFL");
			close(sockfd);
			continue;
		}

		// Initialize socket structure
		memset((char*) &serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		if (argc > 1) {
			// Use IP address from command line argument
			serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
		} else {
			// Fallback to default IP address if no argument is provided
			serv_addr.sin_addr.s_addr = inet_addr(VNC_SERVER_IP_ADDRESS);
		}
		serv_addr.sin_port = htons(VNC_SERVER_PORT);

		struct timeval timeout;
		timeout.tv_sec = 10; // 10 seconds timeout read/write
		timeout.tv_usec = 0;

		// Set receive timeout
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout,
				sizeof(timeout)) < 0) {
			perror("Set receive timeout failed");
			close(sockfd);
			continue;
		}

		// Set send timeout
		if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*) &timeout,
				sizeof(timeout)) < 0) {
			perror("Set send timeout failed");
			close(sockfd);
			continue;
		}

		result = connect(sockfd, (struct sockaddr*) &serv_addr,
				sizeof(serv_addr));
		if (result < 0 && errno != EINPROGRESS) {
			perror("Error connecting to server");
			usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
			close(sockfd);
			continue;
		}

		// Initialize file descriptor set
		FD_ZERO(&write_fds);
		FD_SET(sockfd, &write_fds);

		// Set timeout value
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

		// Wait for the socket to be writable (connection established)
		result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
		if (result < 0) {
			perror("select failed");
			close(sockfd);
			usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
			continue;
		} else if (result == 0) {
			printf("Connection timed out\n");
			close(sockfd);
			usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
			continue;
		} else {
			// Check if the connection was successful
			if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
				perror("getsockopt");
				close(sockfd);
				usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
				continue;
			}
			if (so_error == 0) {
				printf("Connected to the server: %s\n", inet_ntoa(
						serv_addr.sin_addr));
			} else {
				printf("Connection failed: %s\n", strerror(so_error));
				usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
				close(sockfd);
				continue;
			}
		}

		// Reset socket to blocking mode
		if (fcntl(sockfd, F_SETFL, flags) < 0) {
			perror("fcntl F_SETFL");
			usleep(200000); // Sleep for 200 milliseconds (200,000 microseconds)
			close(sockfd);
			continue;
		}

		if (sockfd != NULL) {
			// we have connection so swap to different view
			execute_initial_commands();
		}

		// Receive server initialization message
		char serverInitMsg[12];
		int bytesReceived = recv(sockfd, serverInitMsg, sizeof(serverInitMsg),
				MSG_WAITALL);
		if (bytesReceived < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr << "Receive timeout occurred" << std::endl;
			} else {
				std::cerr << "Error receiving server initialization message: "
						<< strerror(errno) << std::endl;
			}
			close(sockfd);
			continue;
		} else if (bytesReceived == 0) {
			std::cerr << "Connection closed by peer" << std::endl;
			close(sockfd);
			continue;
		}

		// Send client protocol version message with timeout handling
		if (send(sockfd, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr << "Send timeout occurred" << std::endl;
			} else {
				std::cerr << "Error sending client initialization message: "
						<< strerror(errno) << std::endl;
			}
			close(sockfd);
			continue;
		}
		// Security handshake
		char securityHandshake[4];
		ssize_t bytesReceivedSecurity = recv(sockfd, securityHandshake,
				sizeof(securityHandshake), 0);
		if (bytesReceivedSecurity < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr << "Receive timeout occurred" << std::endl;
			} else {
				std::cerr << "Error reading security handshake: " << strerror(
						errno) << std::endl;
			}
			close(sockfd);
			continue;
		} else if (bytesReceivedSecurity == 0) {
			std::cerr << "Connection closed by peer" << std::endl;
			close(sockfd);
			continue;
		}

		if (send(sockfd, "\x01", 1, 0) < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr << "Send timeout occurred" << std::endl;
			} else {
				std::cerr << "Error sending client init message: " << strerror(
						errno) << std::endl;
			}
			close(sockfd);
			continue;
		}

		// Read framebuffer width and height
		char framebufferWidth[2];
		char framebufferHeight[2];

		ssize_t bytesReceivedFrameBuffer = recv(sockfd, framebufferWidth,
				sizeof(framebufferWidth), 0);
		if (bytesReceivedFrameBuffer < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr
						<< "Receive timeout occurred while reading framebuffer width"
						<< std::endl;
			} else {
				std::cerr << "Error reading framebuffer width: " << strerror(
						errno) << std::endl;
			}
			close(sockfd);
			continue;
		} else if (bytesReceivedFrameBuffer == 0) {
			std::cerr
					<< "Connection closed by peer while reading framebuffer width"
					<< std::endl;
			close(sockfd);
			continue;
		}

		bytesReceivedFrameBuffer = recv(sockfd, framebufferHeight,
				sizeof(framebufferHeight), 0);
		if (bytesReceivedFrameBuffer < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr
						<< "Receive timeout occurred while reading framebuffer height"
						<< std::endl;
			} else {
				std::cerr << "Error reading framebuffer height: " << strerror(
						errno) << std::endl;
			}
			close(sockfd);
			continue;
		} else if (bytesReceivedFrameBuffer == 0) {
			std::cerr
					<< "Connection closed by peer while reading framebuffer height"
					<< std::endl;
			close(sockfd);
			continue;
		}

		// Read pixel format and name length
		char pixelFormat[16];
		char nameLength[4];

		if (!recv(sockfd, pixelFormat, sizeof(pixelFormat), MSG_WAITALL)
				|| !recv(sockfd, nameLength, sizeof(nameLength), MSG_WAITALL)) {
			fprintf(stderr, "Error reading pixel format or name length\n");
			close(sockfd);
			continue;
		}

		uint32_t nameLengthInt = (nameLength[0] << 24) | (nameLength[1] << 16)
				| (nameLength[2] << 8) | nameLength[3];

		// Read server name
		char name[32];
		if (!recv(sockfd, name, nameLengthInt, MSG_WAITALL)) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr
						<< "Receive timeout occurred while reading server name"
						<< std::endl;
			} else {
				std::cerr << "Error reading server name: " << strerror(errno)
						<< std::endl;
			}
			close(sockfd);
			continue;
		}

		// Send encoding update requests
		if (send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0) < 0 || send(
				sockfd, FRAMEBUFFER_UPDATE_REQUEST,
				sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				std::cerr
						<< "Send timeout occurred while sending framebuffer update request"
						<< std::endl;
			} else {
				std::cerr << "Error sending framebuffer update request: "
						<< strerror(errno) << std::endl;
			}
			close(sockfd);
			continue;
		}
		int framebufferWidthInt = 0;
		int framebufferHeightInt = 0;
		int finalHeight = 0;

		int frameCount = 0;
		int switchToMap = 0;
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
			continue;
		}

		// Main loop
		while (true) {
			frameCount++;
			char* framebufferUpdate = parseFramebufferUpdate(sockfd,
					&framebufferWidthInt, &framebufferHeightInt, strm,
					&finalHeight);
			if (framebufferUpdate == NULL) {
				close(sockfd);
				break;
			}

			// Send framebuffer update request with timeout handling
			if (send(sockfd, FRAMEBUFFER_UPDATE_REQUEST,
					sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN) {
					std::cerr
							<< "Send timeout occurred while sending framebuffer update request"
							<< std::endl;
				} else {
					std::cerr << "Error sending framebuffer update request: "
							<< strerror(errno) << std::endl;
				}
				close(sockfd);
				break;
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
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebufferWidthInt,
					finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
					framebufferUpdate);

			// Set vertex positions
			GLint positionAttribute = glGetAttribLocation(programObject,
					"position");
			if (framebufferWidthInt > finalHeight)
				glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE,
						0, landscapeVertices);
			else
				glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE,
						0, portraitVertices);

			glEnableVertexAttribArray(positionAttribute);

			// Set texture coordinates
			GLint texCoordAttrib = glGetAttribLocation(programObject,
					"texCoord");
			if (framebufferWidthInt > finalHeight)
				glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0,
						landscapeTexCoords);
			else
				glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0,
						portraitTexCoords);

			glEnableVertexAttribArray(texCoordAttrib);
			finalHeight = 0;
			// Draw quad
			glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

			//print_string(-333, 150, readPersistanceData("s:2001:101").c_str(), 1, 1, 1, 64); // persistance data

			eglSwapBuffers(eglDisplay, eglSurface);
			switchToMap++;
			if (switchToMap > 20) {
				switchToMap = 0;
				execute_initial_commands();
			}
			free(framebufferUpdate); // Free the dynamically allocated memory
		}
		glDeleteTextures(1, &textureID);
		execute_final_commands();
	}
	// Cleanup
	eglSwapBuffers(eglDisplay, eglSurface);
	eglDestroySurface(eglDisplay, eglSurface);
	eglDestroyContext(eglDisplay, eglContext);
	eglTerminate(eglDisplay);
	execute_final_commands();

	return EXIT_SUCCESS;
}
