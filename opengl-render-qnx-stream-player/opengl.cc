// opengl-render-qnx-stream-player — pipelined multithreaded stream renderer
//
// OPTIMIZATION SUMMARY:
// 1. MEMORY: Persistent frame buffer - eliminates malloc/realloc/free per frame
// 2. GPU: glTexSubImage2D - partial texture updates instead of full re-upload
// 3. GPU: Persistent VBO for text overlay - eliminates glGen/glDelete per frame
// 4. GPU: Cache attribute locations - eliminates glGetAttribLocation per frame
// 5. THREADING: Dedicated pthread Network Thread for blocking socket recv()
// 6. MEMORY: Double-buffered framebuffers (Front/Back) to prevent tearing
// 7. SYNC: pthread_mutex_t for safe, low-latency cross-thread state transfer
// 8. PERF: C++98 compliant circular buffer for sliding-window decode FPS tracking

#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <time.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <syslog.h>

#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <fcntl.h>
#include <sys/sysctl.h>
#include <sys/param.h>

#include <pthread.h>

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif

static bool g_verbose = false;
static bool g_logToSyslog = false;
static bool g_logToRemoteSyslog = false;
static bool g_heapProbeEnabled = false;
static bool g_mpegOpenOptionsEnabled = false;
static bool g_dropFramesWhenBusy = true;
static bool g_debugOverlayEnabled = true;
static float g_debugOverlayMarginX = 16.0f;
static float g_debugOverlayMarginY = 16.0f;
static float g_debugOverlayScale = 110.0f;
static char g_ffmpegOptRwTimeout[32] = "5000000";
static char g_ffmpegOptFflags[64] = "nobuffer";
static char g_ffmpegOptFlags[64] = "low_delay";
static char g_ffmpegOptProbeSize[32] = "32768";
static char g_ffmpegOptAnalyzeDuration[32] = "0";
static int g_ffmpegThreadCount = 2;
static bool g_ffmpegFastDecode = true;
static int g_nudgeMs = 0;
static bool g_pixelPerfect = false;
static int g_ffmpegSkipFrame = AVDISCARD_DEFAULT;
static const char* g_daemonPidFilePath = "/tmp/opengl-render-qnx-daemon.pid";
static bool g_daemonPidFileOwned = false;
static int g_remoteSyslogSocket = -1;
static struct sockaddr_in g_remoteSyslogAddr;
static char g_remoteSyslogHost[64] = {0};
static int g_remoteSyslogPort = 0;
static char g_syslogIdent[64] = "opengl-render-qnx";

// ---------------- Timing ----------------
struct FrameTimings {
    double recv_ms;
    double inflate_ms;
    double parse_ms;
    double texture_upload_ms;
    double total_frame_ms;
    FrameTimings() : recv_ms(0.0), inflate_ms(0.0), parse_ms(0.0), texture_upload_ms(0.0), total_frame_ms(0.0) {}
};

static uint64_t now_us() {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
    }
#endif
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static double us_to_ms(uint64_t us) { return (double)us / 1000.0; }

static unsigned int next_backoff_ms(unsigned int currentMs, unsigned int maxMs) {
    if (currentMs == 0) return 1;
    if (currentMs >= maxMs) return maxMs;
    if (currentMs > (maxMs / 2)) return maxMs;
    return currentMs * 2;
}

static bool is_truthy_env_value(const char* value) {
    if (!value || value[0] == '\0') return false;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "OFF") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "NO") == 0) {
        return false;
    }
    return true;
}

static bool parse_bool_text(const char* text, bool* outValue) {
    if (!text || !outValue) return false;

    while (*text == ' ' || *text == '\t') text++;

    char buf[24];
    size_t i = 0;
    while (text[i] != '\0' && text[i] != '\n' && text[i] != '\r' && i < (sizeof(buf) - 1)) {
        buf[i] = (char)tolower((unsigned char)text[i]);
        i++;
    }
    while (i > 0 && (buf[i - 1] == ' ' || buf[i - 1] == '\t')) i--;
    buf[i] = '\0';

    if (strcmp(buf, "1") == 0 || strcmp(buf, "true") == 0 || strcmp(buf, "yes") == 0 || strcmp(buf, "on") == 0) {
        *outValue = true;
        return true;
    }
    if (strcmp(buf, "0") == 0 || strcmp(buf, "false") == 0 || strcmp(buf, "no") == 0 || strcmp(buf, "off") == 0) {
        *outValue = false;
        return true;
    }
    return false;
}

static bool parse_port_number(const char* text, int* outPort) {
    if (!text || !*text || !outPort) return false;

    long value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + (*p - '0');
        if (value > 65535L) return false;
    }

    if (value <= 0) return false;
    *outPort = (int)value;
    return true;
}

static bool parse_remote_spec(const char* spec, char* hostOut, size_t hostOutSize, int* portInOut) {
    if (!spec || !*spec || !hostOut || hostOutSize == 0 || !portInOut) return false;

    char tmp[128];
    strncpy(tmp, spec, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char* firstColon = strchr(tmp, ':');
    char* lastColon = strrchr(tmp, ':');
    if (firstColon && firstColon != lastColon) {
        // Keep parsing simple and explicit: IPv4/hostname only.
        return false;
    }

    if (firstColon) {
        char* portText = firstColon + 1;
        int parsedPort = 0;
        if (*portText == '\0' || !parse_port_number(portText, &parsedPort)) {
            return false;
        }
        *firstColon = '\0';
        *portInOut = parsedPort;
    }

    if (tmp[0] == '\0') return false;

    strncpy(hostOut, tmp, hostOutSize - 1);
    hostOut[hostOutSize - 1] = '\0';
    return true;
}

static void derive_syslog_ident(const char* argv0) {
    const char* ident = "opengl-render-qnx";
    if (argv0 && argv0[0]) {
        const char* lastSlash = strrchr(argv0, '/');
        if (lastSlash && *(lastSlash + 1) != '\0') ident = lastSlash + 1;
        else ident = argv0;
    }

    strncpy(g_syslogIdent, ident, sizeof(g_syslogIdent) - 1);
    g_syslogIdent[sizeof(g_syslogIdent) - 1] = '\0';
}

static bool resolve_ipv4_host(const char* host, struct in_addr* outAddr) {
    if (!host || !*host || !outAddr) return false;

    if (inet_aton(host, outAddr) != 0) return true;

    struct hostent* he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET || he->h_length < (int)sizeof(struct in_addr) ||
        !he->h_addr_list || !he->h_addr_list[0]) {
        return false;
    }

    memcpy(outAddr, he->h_addr_list[0], sizeof(struct in_addr));
    return true;
}

static bool init_remote_syslog_sink(const char* host, int port) {
    if (!host || !*host || port <= 0 || port > 65535) return false;

    struct in_addr addr;
    if (!resolve_ipv4_host(host, &addr)) return false;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return false;

    memset(&g_remoteSyslogAddr, 0, sizeof(g_remoteSyslogAddr));
    g_remoteSyslogAddr.sin_family = AF_INET;
    g_remoteSyslogAddr.sin_port = htons((uint16_t)port);
    g_remoteSyslogAddr.sin_addr = addr;

    g_remoteSyslogSocket = sockfd;
    strncpy(g_remoteSyslogHost, host, sizeof(g_remoteSyslogHost) - 1);
    g_remoteSyslogHost[sizeof(g_remoteSyslogHost) - 1] = '\0';
    g_remoteSyslogPort = port;
    g_logToRemoteSyslog = true;
    return true;
}

static void send_remote_syslog_packet(int priority, const char* msg) {
    if (!g_logToRemoteSyslog || g_remoteSyslogSocket < 0) return;

    const char* payload = msg ? msg : "";

    char timestamp[32] = {0};
    time_t now = time(NULL);
    struct tm tmLocal;
    struct tm* tmPtr = localtime(&now);
    if (tmPtr) {
        tmLocal = *tmPtr;
        strftime(timestamp, sizeof(timestamp), "%b %d %H:%M:%S", &tmLocal);
    } else {
        strncpy(timestamp, "Jan 01 00:00:00", sizeof(timestamp) - 1);
    }

    char host[64] = "qnx";
    if (gethostname(host, sizeof(host) - 1) == 0) host[sizeof(host) - 1] = '\0';

    int pri = (LOG_USER << 3) | (priority & 0x07);

    char packet[1400];
    int n = snprintf(packet, sizeof(packet), "<%d>%s %s %s[%ld]: %s",
                     pri, timestamp, host, g_syslogIdent, (long)getpid(), payload);
    if (n <= 0) return;

    size_t packetLen = (n < (int)sizeof(packet)) ? (size_t)n : (sizeof(packet) - 1);

    ssize_t sent = sendto(g_remoteSyslogSocket, packet, packetLen, 0,
                          (struct sockaddr*)&g_remoteSyslogAddr, sizeof(g_remoteSyslogAddr));
    if (sent < 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[%.3f] remote syslog sendto failed: %s\n", now_us() / 1e6, strerror(errno));
        }
    }
}

static void log_syslogf(int priority, const char* fmt, ...) {
    if (!g_logToSyslog && !g_logToRemoteSyslog) return;

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (g_logToSyslog) syslog(priority, "%s", msg);
    if (g_logToRemoteSyslog) send_remote_syslog_packet(priority, msg);
}

static void close_syslog_sink() {
    if (g_logToRemoteSyslog && g_remoteSyslogSocket >= 0) {
        send_remote_syslog_packet(LOG_NOTICE, "remote syslog sink closing");
        close(g_remoteSyslogSocket);
        g_remoteSyslogSocket = -1;
        g_logToRemoteSyslog = false;
        g_remoteSyslogHost[0] = '\0';
        g_remoteSyslogPort = 0;
    }

    if (g_logToSyslog) {
        syslog(LOG_NOTICE, "syslog sink closing");
        closelog();
        g_logToSyslog = false;
    }
}

static bool pid_is_running(pid_t pid) {
    if (pid <= 1) return false;
    if (kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

static bool read_pid_file(const char* path, pid_t* outPid) {
    if (!path || !outPid) return false;

    FILE* fp = fopen(path, "r");
    if (!fp) return false;

    long parsed = 0;
    int n = fscanf(fp, "%ld", &parsed);
    fclose(fp);

    if (n != 1 || parsed <= 1) return false;
    *outPid = (pid_t)parsed;
    return true;
}

static bool find_running_pid_from_file(const char* path, pid_t* runningPid) {
    pid_t pid = 0;
    if (!read_pid_file(path, &pid)) return false;

    if (pid_is_running(pid)) {
        if (runningPid) *runningPid = pid;
        return true;
    }

    // Stale PID file from a previous crash/kill.
    unlink(path);
    return false;
}

static bool write_pid_file(const char* path, pid_t pid) {
    if (!path || pid <= 1) return false;

    int fd = open(path, O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return false;

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld\n", (long)pid);
    if (len <= 0 || len >= (int)sizeof(buf)) {
        close(fd);
        return false;
    }

    ssize_t wrote = write(fd, buf, (size_t)len);
    close(fd);
    return wrote == (ssize_t)len;
}

static bool reserve_daemon_pid_file(pid_t* runningPid) {
    if (runningPid) *runningPid = 0;

    for (int attempt = 0; attempt < 2; ++attempt) {
        int fd = open(g_daemonPidFilePath, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            close(fd);
            if (!write_pid_file(g_daemonPidFilePath, getpid())) {
                unlink(g_daemonPidFilePath);
                return false;
            }
            g_daemonPidFileOwned = true;
            return true;
        }

        if (errno != EEXIST) {
            return false;
        }

        if (find_running_pid_from_file(g_daemonPidFilePath, runningPid)) {
            errno = EEXIST;
            return false;
        }
    }

    return false;
}

static void cleanup_daemon_pid_file() {
    if (g_daemonPidFileOwned) {
        unlink(g_daemonPidFilePath);
        g_daemonPidFileOwned = false;
    }
}

static int daemonize_process() {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "daemon: first fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        fprintf(stderr, "daemon: setsid failed: %s\n", strerror(errno));
        return -1;
    }

    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "daemon: second fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    umask(0);
    (void)chdir("/");

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        if (fd > 2) close(fd);
        return -1;
    }

    if (fd > 2) close(fd);
    return 0;
}

#define LOG(fmt, ...) \
    do { \
        if (g_verbose) { \
            double _t = now_us() / 1e6; \
            fprintf(stderr, "[%.3f] " fmt "\n", _t, ##__VA_ARGS__); \
            log_syslogf(LOG_DEBUG, "[%.3f] " fmt, _t, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOGE(fmt, ...) \
    do { \
        double _t = now_us() / 1e6; \
        fprintf(stderr, "[%.3f] " fmt "\n", _t, ##__VA_ARGS__); \
        log_syslogf(LOG_NOTICE, "[%.3f] " fmt, _t, ##__VA_ARGS__); \
    } while(0)

// Force QNX's malloc free-list check. If the heap is already corrupt at this
// point, the process aborts here with the probe label visible in the log.
// malloc(8) only exercises the small-block fast bin and misses corruption in
// the large-block doubly-linked free list. The 512 KB alloc forces a full
// free-list traversal so list corruption is caught at the right probe site
// rather than being silently skipped and detected later (e.g. in pthread_create).
static void heap_probe(const char *label) {
    if (!g_heapProbeEnabled) return;

    void *p = malloc(8);
    if (p) free(p);
    void *q = malloc(512 * 1024);
    if (q) free(q);
    LOGE("heap ok: %s", label);
}

// ============================================================================
// Persistent Double Buffers
// ============================================================================
struct PersistentBuffers {
    char* frontBuffer;
    char* backBuffer;
    size_t frontCap;
    size_t backCap;

    PersistentBuffers() : frontBuffer(NULL), backBuffer(NULL), frontCap(0), backCap(0) {}

    ~PersistentBuffers() {
        free(frontBuffer);
        free(backBuffer);
    }

    // Single-threaded use only (e.g. startup before network thread launches).
    bool ensureFrameBuffers(size_t needed) {
        if (needed > backCap) {
            size_t newCap = (needed > backCap * 2) ? needed : (backCap * 2);
            char* p = (char*)realloc(backBuffer, newCap);
            if (!p) return false;
            backBuffer = p;
            backCap = newCap;
        }
        if (needed > frontCap) {
            size_t newCap = (needed > frontCap * 2) ? needed : (frontCap * 2);
            char* p = (char*)realloc(frontBuffer, newCap);
            if (!p) return false;
            frontBuffer = p;
            frontCap = newCap;
        }
        return true;
    }

    // Called while g_frameMutex is held; resize, copy and swap stay serialized.
    bool ensureBackBuffer(size_t needed) {
        if (needed > backCap) {
            size_t newCap = (needed > backCap * 2) ? needed : (backCap * 2);
            char* p = (char*)realloc(backBuffer, newCap);
            if (!p) return false;
            backBuffer = p;
            backCap = newCap;
            LOG("buffers: back buffer grew to %zu bytes", newCap);
        }
        return true;
    }

    // Call WITH g_frameMutex held: safe to realloc frontBuffer.
    // Must be called before swapping so frontBuffer is large enough for the new frame.
    bool syncFrontBuffer() {
        if (backCap > frontCap) {
            char* p = (char*)realloc(frontBuffer, backCap);
            if (!p) return false;
            frontBuffer = p;
            frontCap = backCap;
            LOG("buffers: front buffer synced to %zu bytes", frontCap);
        }
        return true;
    }
};

static PersistentBuffers g_bufs;

// --- Threading & Sync Globals ---
pthread_mutex_t g_frameMutex = PTHREAD_MUTEX_INITIALIZER;
volatile bool g_newFrameReady = false;
volatile bool g_running = true;

// Shared state updated by Network Thread, read by Render Thread
int g_sharedFbW = 0;
int g_sharedFbH = 0;
int g_sharedFinalH = 0;
FrameTimings g_sharedTimings;
uint64_t g_sharedFramesDecoded = 0;
unsigned int g_mpegSessionSeq = 0;
volatile bool g_streamConnected = false;

static void log_buffer_caps_err(const char* where) {
    LOGE("buffers[%s]: front=%p cap=%zu back=%p cap=%zu",
         where,
         (void*)g_bufs.frontBuffer, g_bufs.frontCap,
         (void*)g_bufs.backBuffer, g_bufs.backCap);
}

static void log_ffmpeg_error(const char* stage, int errnum) {
    char errbuf[128] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    LOGE("MPEGTS: %s failed: %s (%d)", stage, errbuf, errnum);
}

// ---------------- GLES setup ----------------
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

GLint g_posAttr = -1;
GLint g_texAttr = -1;
GLint g_textPosAttr = -1;
GLint g_textColorUnif = -1;
GLuint g_textVBO = 0;

// ---------------- GL shaders ----------------
const char* vertexShaderSource =
    "attribute vec2 position;    \n"
    "attribute vec2 texCoord;     \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = vec4(position, 0.0, 1.0); \n"
    "   v_texCoord = texCoord;   \n"
    "   gl_PointSize = 4.0;      \n"
    "}                            \n";

const char* fragmentShaderSource =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D texture;\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, v_texCoord);\n"
    "}\n";

const char* vertexShaderSourceText =
    "attribute vec2 position;    \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = vec4(position, 0.0, 1.0); \n"
    "}                            \n";

const char* fragmentShaderSourceText =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

// Geometry
GLfloat landscapeVertices[] = { -1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f,  1.0f,-1.0f, 0.0f, -1.0f,-1.0f, 0.0f };
GLfloat portraitVertices[]  = { -1.0f, 1.0f,  0.0f,  1.0f, 1.0f,  0.0f,  1.0f,-1.0f, 0.0f, -1.0f,-1.0f, 0.0f };
GLfloat landscapeTexCoords[]= {  0.0f, 0.0f,         1.0f,0.0f,         1.0f,1.0f,         0.0f, 1.0f };
GLfloat portraitTexCoords[] = {  0.0f, 0.0f,         1.0f,0.0f,         1.0f,1.0f,         0.0f, 1.0f };
GLfloat backgroundColor[4]  = {  0.0f, 0.0f, 0.0f, 1.0f };

int windowWidth  = 800;
int windowHeight = 480;

char g_streamUrl[512] = "";

// ---------------- Signal handler ----------------
static void signal_handler(int sig) {
    LOGE("signal: received %d, shutting down", sig);
    (void)sig;
    g_running = false;
}

// ---------------- QNX Helpers ----------------
struct Command { const char* command; const char* error_message; };

void execute_initial_commands() {
    LOG("dmdt: activating display context (context 3)");
    struct Command commands[] = {
        { "IPL_CONFIG_DIR=/etc/eso/production /eso/bin/apps/dmdt dc 70 3",  "Create new display table with context 3 failed with error" },
        { "IPL_CONFIG_DIR=/etc/eso/production /eso/bin/apps/dmdt sc 4 70",  "Set display 4 (VC) to display table 70 failed with error" }
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        int ret = system(commands[i].command);
        if (ret != 0) fprintf(stderr, "%s: %d\n", commands[i].error_message, ret);
        else LOG("dmdt: '%s' ok", commands[i].command);
    }
}

void execute_final_commands() {
    LOG("dmdt: restoring display context (context 33)");
    struct Command commands[] = {
        { "IPL_CONFIG_DIR=/etc/eso/production /eso/bin/apps/dmdt dc 70 33", "Restore display table to context 33 failed with error" },
        { "IPL_CONFIG_DIR=/etc/eso/production /eso/bin/apps/dmdt sc 4 70",  "Set display 4 (VC) to display table 70 failed with error" }
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        int ret = system(commands[i].command);
        if (ret != 0) fprintf(stderr, "%s: %d\n", commands[i].error_message, ret);
        else LOG("dmdt: '%s' ok", commands[i].command);
    }
}

// ---------------- GL Init ----------------
static void check_shader(GLuint shader, const char* name) {
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log)-1, NULL, log);
        fprintf(stderr, "GL: shader '%s' compile failed: %s\n", name, log);
    } else {
        LOG("GL: shader '%s' compiled ok", name);
    }
}

static void check_program(GLuint prog, const char* name) {
    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512] = {0};
        glGetProgramInfoLog(prog, sizeof(log)-1, NULL, log);
        fprintf(stderr, "GL: program '%s' link failed: %s\n", name, log);
    } else {
        LOG("GL: program '%s' linked ok", name);
    }
}

void Init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    check_shader(vs, "vert-main");

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    check_shader(fs, "frag-main");

    GLuint vsT = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsT, 1, &vertexShaderSourceText, NULL);
    glCompileShader(vsT);
    check_shader(vsT, "vert-text");

    GLuint fsT = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsT, 1, &fragmentShaderSourceText, NULL);
    glCompileShader(fsT);
    check_shader(fsT, "frag-text");

    programObject = glCreateProgram();
    glAttachShader(programObject, vs);
    glAttachShader(programObject, fs);
    glLinkProgram(programObject);
    check_program(programObject, "main");

    programObjectTextRender = glCreateProgram();
    glAttachShader(programObjectTextRender, vsT);
    glAttachShader(programObjectTextRender, fsT);
    glLinkProgram(programObjectTextRender);
    check_program(programObjectTextRender, "text");

    g_posAttr = glGetAttribLocation(programObject, "position");
    g_texAttr = glGetAttribLocation(programObject, "texCoord");
    g_textPosAttr = glGetAttribLocation(programObjectTextRender, "position");
    g_textColorUnif = glGetUniformLocation(programObjectTextRender, "u_color");
    LOG("GL: attrib locations: pos=%d tex=%d textPos=%d colorUnif=%d",
        g_posAttr, g_texAttr, g_textPosAttr, g_textColorUnif);

    glGenBuffers(1, &g_textVBO);

    glClearColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3]);
}

// 5×7 bitmap font — ported from go-vnc-h264-bridge/framebuffer.go
// Each entry is 7 rows; bits 4..0 (MSB first) are the 5 pixel columns.
static const struct { unsigned char ch; unsigned char rows[7]; } kTinyFont[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x06,0x06}},
    {':', {0x00,0x06,0x06,0x00,0x06,0x06,0x00}},
    {'-', {0x00,0x00,0x1F,0x00,0x00,0x00,0x00}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
    {'/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
    {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'?', {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1E,0x01,0x01,0x06,0x01,0x01,0x1E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x12,0x11,0x11,0x11,0x12,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x10,0x13,0x11,0x0E}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    // lowercase — x-height rows 2-6, ascenders use rows 0-1, descenders use row 6
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
    {'b', {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}},
    {'c', {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}},
    {'d', {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}},
    {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
    {'f', {0x06,0x08,0x1E,0x08,0x08,0x08,0x08}},
    {'g', {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}},
    {'h', {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}},
    {'i', {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}},
    {'j', {0x01,0x00,0x03,0x01,0x01,0x11,0x0E}},
    {'k', {0x10,0x10,0x12,0x14,0x1C,0x14,0x12}},
    {'l', {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'m', {0x00,0x00,0x1B,0x15,0x15,0x15,0x15}},
    {'n', {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}},
    {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
    {'p', {0x00,0x1E,0x11,0x11,0x1E,0x10,0x10}},
    {'q', {0x00,0x0F,0x11,0x11,0x0F,0x01,0x01}},
    {'r', {0x00,0x00,0x0F,0x08,0x08,0x08,0x08}},
    {'s', {0x00,0x00,0x0E,0x10,0x0E,0x01,0x0E}},
    {'t', {0x04,0x04,0x1F,0x04,0x04,0x04,0x03}},
    {'u', {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}},
    {'v', {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}},
    {'w', {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}},
    {'x', {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}},
    {'y', {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}},
    {'z', {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}},
    // extra punctuation
    {',', {0x00,0x00,0x00,0x00,0x06,0x04,0x08}},
    {';', {0x00,0x06,0x06,0x00,0x06,0x04,0x08}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
    {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
    {'#', {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}},
};
static const int kTinyFontCount = (int)(sizeof(kTinyFont)/sizeof(kTinyFont[0]));

static const unsigned char* tiny_font_glyph(unsigned char ch) {
    for (int i = 0; i < kTinyFontCount; i++)
        if (kTinyFont[i].ch == ch) return kTinyFont[i].rows;
    return kTinyFont[11].rows; // '?'
}

// Emit one pixel-quad (two triangles, 12 floats) into buf at index n.
static void emit_quad(GLfloat* buf, int& n, float x0, float y0, float x1, float y1) {
    buf[n++]=x0; buf[n++]=y0;
    buf[n++]=x1; buf[n++]=y0;
    buf[n++]=x1; buf[n++]=y1;
    buf[n++]=x0; buf[n++]=y0;
    buf[n++]=x1; buf[n++]=y1;
    buf[n++]=x0; buf[n++]=y1;
}

void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    static GLfloat triangleBuffer[20000];
    int n = 0;

    // Convert pixel-space origin to NDC, same convention as the old stb_easyfont path.
    float ox = (2.0f * x) / windowWidth;
    float oy = (2.0f * y) / windowHeight;

    // One "font pixel" = 1/size NDC units in each axis.
    float ps = 1.0f / size;

    float penX = ox;
    float penY = oy;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            penX = ox;
            penY -= 9.0f * ps;  // 7 rows + 2 gap
            continue;
        }
        const unsigned char* glyph = tiny_font_glyph((unsigned char)*p);
        for (int gy = 0; gy < 7; gy++) {
            unsigned char row = glyph[gy];
            for (int gx = 0; gx < 5; gx++) {
                if (!(row & (1 << (4 - gx)))) continue;
                if (n + 12 > (int)(sizeof(triangleBuffer)/sizeof(triangleBuffer[0]))) goto done;
                float px = penX + gx * ps;
                float py = penY - gy * ps;
                emit_quad(triangleBuffer, n, px, py, px + ps, py - ps);
            }
        }
        penX += 6.0f * ps;  // 5 wide + 1 gap
    }
done:
    glUseProgram(programObjectTextRender);
    glUniform4f(g_textColorUnif, r, g, b, 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, g_textVBO);
    glBufferData(GL_ARRAY_BUFFER, n * sizeof(GLfloat), triangleBuffer, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_textPosAttr);
    glVertexAttribPointer(g_textPosAttr, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, n / 2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Config file parsing
void parseLineArray(char *line, const char *key, GLfloat *dest, int count) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *values = strchr(line, '=');
        if (values) {
            values++;
            for (int i = 0; i < count; i++) dest[i] = strtof(values, &values);
        }
    }
}
void parseLineInt(char *line, const char *key, int *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value) *dest = atoi(value + 1);
    }
}

void parseLineFloat(char *line, const char *key, float *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value) *dest = strtof(value + 1, NULL);
    }
}

void parseLineBool(char *line, const char *key, bool *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (!value) return;

        bool parsed = false;
        if (parse_bool_text(value + 1, &parsed)) {
            *dest = parsed;
        }
    }
}

void parseLineString(char *line, const char *key, char *dest, size_t destSize) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value) {
            value++;
            while (*value == ' ' || *value == '\t') value++;

            size_t i = 0;
            while (value[i] != '\0' && value[i] != '\n' && value[i] != '\r' && i < (destSize - 1)) {
                dest[i] = value[i];
                i++;
            }
            dest[i] = '\0';
        }
    }
}

static int parse_skip_frame_value(const char* text) {
    if (!text) return AVDISCARD_DEFAULT;

    while (*text == ' ' || *text == '\t') text++;

    char buf[24];
    size_t i = 0;
    while (text[i] != '\0' && text[i] != '\n' && text[i] != '\r' && i < (sizeof(buf) - 1)) {
        buf[i] = (char)tolower((unsigned char)text[i]);
        i++;
    }
    while (i > 0 && (buf[i - 1] == ' ' || buf[i - 1] == '\t')) i--;
    buf[i] = '\0';

    if (strcmp(buf, "none") == 0) return AVDISCARD_NONE;
    if (strcmp(buf, "noref") == 0) return AVDISCARD_NONREF;
    if (strcmp(buf, "bidir") == 0) return AVDISCARD_BIDIR;
    if (strcmp(buf, "nonkey") == 0) return AVDISCARD_NONKEY;
    if (strcmp(buf, "all") == 0) return AVDISCARD_ALL;
    return AVDISCARD_DEFAULT;
}

void parseLineSkipFrame(char *line, const char *key, int *dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char *value = strchr(line, '=');
        if (value && dest) {
            *dest = parse_skip_frame_value(value + 1);
        }
    }
}

void loadConfig(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        LOG("config: could not open '%s', using defaults", filename);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        parseLineArray(line, "landscapeVertices", landscapeVertices, 12);
        parseLineArray(line, "portraitVertices", portraitVertices, 12);
        parseLineArray(line, "landscapeTexCoords", landscapeTexCoords, 8);
        parseLineArray(line, "portraitTexCoords", portraitTexCoords, 8);
        parseLineArray(line, "backgroundColor", backgroundColor, 4);
        parseLineInt(line, "windowWidth", &windowWidth);
        parseLineInt(line, "windowHeight", &windowHeight);
        parseLineString(line, "streamUrl", g_streamUrl, sizeof(g_streamUrl));

        parseLineBool(line, "mpegOpenOptions", &g_mpegOpenOptionsEnabled);
        parseLineBool(line, "dropFramesWhenBusy", &g_dropFramesWhenBusy);
        parseLineBool(line, "debugOverlay", &g_debugOverlayEnabled);
        parseLineFloat(line, "debugOverlayMarginX", &g_debugOverlayMarginX);
        parseLineFloat(line, "debugOverlayMarginY", &g_debugOverlayMarginY);
        parseLineFloat(line, "debugOverlayScale", &g_debugOverlayScale);

        parseLineString(line, "ffmpegRwTimeout", g_ffmpegOptRwTimeout, sizeof(g_ffmpegOptRwTimeout));
        parseLineString(line, "ffmpegFflags", g_ffmpegOptFflags, sizeof(g_ffmpegOptFflags));
        parseLineString(line, "ffmpegFlags", g_ffmpegOptFlags, sizeof(g_ffmpegOptFlags));
        parseLineString(line, "ffmpegProbeSize", g_ffmpegOptProbeSize, sizeof(g_ffmpegOptProbeSize));
        parseLineString(line, "ffmpegAnalyzeDuration", g_ffmpegOptAnalyzeDuration, sizeof(g_ffmpegOptAnalyzeDuration));
        parseLineInt(line, "ffmpegThreadCount", &g_ffmpegThreadCount);
        parseLineInt(line, "ffmpegNudgeMs", &g_nudgeMs);
        parseLineBool(line, "ffmpegFastDecode", &g_ffmpegFastDecode);
        parseLineBool(line, "pixelPerfect", &g_pixelPerfect);
        parseLineSkipFrame(line, "ffmpegSkipFrame", &g_ffmpegSkipFrame);
    }
    fclose(file);

    if (windowWidth <= 0) windowWidth = 800;
    if (windowHeight <= 0) windowHeight = 480;
    if (g_ffmpegThreadCount < 1) g_ffmpegThreadCount = 1;
    if (g_debugOverlayScale < 24.0f) g_debugOverlayScale = 24.0f;
    if (g_debugOverlayMarginX < 0.0f) g_debugOverlayMarginX = 0.0f;
    if (g_debugOverlayMarginY < 0.0f) g_debugOverlayMarginY = 0.0f;

    LOG("config: loaded '%s'", filename);
}

// ============================================================================
// NETWORK THREAD - MPEG-TS / H.264
// ============================================================================
static int ffmpeg_interrupt_cb(void* ctx) {
    (void)ctx;
    return g_running ? 0 : 1;
}

// Connect briefly to trigger and drain the source's initial IDR keyframe, then
// disconnect so the source resets and will issue a fresh IDR when the real
// decoder connects.  This avoids a long wait for the next natural IDR cycle.
// Only works for tcp:// URLs; silently skips other schemes.
static void tcp_nudge(const char* url, int holdMs) {
    if (strncmp(url, "tcp://", 6) != 0) return;
    const char* hostStart = url + 6;
    const char* colon = strrchr(hostStart, ':');
    if (!colon || colon == hostStart) return;
    char host[256];
    int hostLen = (int)(colon - hostStart);
    if (hostLen <= 0 || hostLen >= (int)sizeof(host)) return;
    memcpy(host, hostStart, (size_t)hostLen);
    host[hostLen] = '\0';
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", port);
    struct addrinfo* res = NULL;
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return; }

    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res); close(fd); return;
    }
    freeaddrinfo(res);

    struct timeval drainTv = {0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &drainTv, sizeof(drainTv));
    uint64_t deadline = now_us() + (uint64_t)holdMs * 1000;
    char buf[32768];
    while (now_us() < deadline) {
        int n = (int)read(fd, buf, sizeof(buf));
        if (n == 0) break;
    }
    close(fd);
    usleep(50000);
}

void* MpegTsNetworkThreadFunc(void* arg) {
    (void)arg;
    const int kMaxFrameWidth = 4096;
    const int kMaxFrameHeight = 2160;
    const size_t kMaxFrameBytes = (size_t)kMaxFrameWidth * (size_t)kMaxFrameHeight * 4u;
    const unsigned int kReconnectBackoffMinMs = 100;
    const unsigned int kReconnectBackoffMaxMs = 2000;
    unsigned int reconnectBackoffMs = kReconnectBackoffMinMs;

    while (g_running) {
        unsigned int sessionId = ++g_mpegSessionSeq;
        AVFormatContext* formatCtx = NULL;
        AVCodecContext* codecCtx = NULL;
        const AVCodec* decoder = NULL;
        AVFrame* frame = NULL;
        AVFrame* rgbaFrame = NULL;
        AVPacket* packet = NULL;
        SwsContext* swsCtx = NULL;
        uint8_t* rgbaData = NULL;
        int videoStreamIndex = -1;
        int lastW = 0;
        int lastH = 0;
        bool sessionOk = true;
        bool displayActive = false;
        bool firstFramePublished = false;
        uint64_t droppedPackets = 0;
        uint64_t droppedFrames = 0;
        uint64_t decodeErrors = 0;
        const char* sessionStopReason = "loop-exit";

        LOGE("MPEGTS[%u]: session start, opening %s (thread=%lu)",
             sessionId, g_streamUrl, (unsigned long)pthread_self());
        log_buffer_caps_err("mpeg-session-start");

        // Allocate context first so interrupt callback is set before opening,
        // and so avformat_alloc_context's malloc primes the heap before any
        // option-related allocations (av_dict_set crashes on QNX dlist first use).
        formatCtx = avformat_alloc_context();
        if (!formatCtx) {
            LOGE("MPEGTS[%u]: avformat_alloc_context failed", sessionId);
            if (g_running) {
                unsigned int waitMs = reconnectBackoffMs;
                reconnectBackoffMs = next_backoff_ms(reconnectBackoffMs, kReconnectBackoffMaxMs);
                LOGE("MPEGTS[%u]: retrying in %u ms", sessionId, waitMs);
                usleep((useconds_t)waitMs * 1000);
            }
            continue;
        }
        formatCtx->interrupt_callback.callback = ffmpeg_interrupt_cb;
        formatCtx->interrupt_callback.opaque = NULL;

        // av_dict_set is safe here: avformat_alloc_context above has already
        // primed the heap, avoiding the QNX dlist-first-use crash.
        AVDictionary *openOpts = NULL;
        if (g_mpegOpenOptionsEnabled && g_ffmpegOptRwTimeout[0])
            av_dict_set(&openOpts, "rw_timeout", g_ffmpegOptRwTimeout, 0);

        if (g_mpegOpenOptionsEnabled) {
            if (strstr(g_ffmpegOptFflags, "nobuffer"))
                formatCtx->flags |= AVFMT_FLAG_NOBUFFER;
            if (strstr(g_ffmpegOptFlags, "low_delay"))
                formatCtx->flags |= AVFMT_FLAG_NOFILLIN;
        }

        int openRet = avformat_open_input(&formatCtx, g_streamUrl, NULL, &openOpts);
        av_dict_free(&openOpts);
        if (openRet < 0) {
            log_ffmpeg_error("avformat_open_input", openRet);
            sessionStopReason = "avformat_open_input";
            LOGE("MPEGTS[%u]: session aborted before decode, reason=%s", sessionId, sessionStopReason);
            if (formatCtx) {
                avformat_close_input(&formatCtx);
                if (formatCtx) {
                    avformat_free_context(formatCtx);
                    formatCtx = NULL;
                }
            }
            if (g_running) {
                unsigned int waitMs = reconnectBackoffMs;
                reconnectBackoffMs = next_backoff_ms(reconnectBackoffMs, kReconnectBackoffMaxMs);
                LOGE("MPEGTS[%u]: retrying in %u ms", sessionId, waitMs);
                usleep((useconds_t)waitMs * 1000);
            }
            continue;
        }

        LOGE("MPEGTS[%u]: transport connected", sessionId);
        heap_probe("mpeg-post-open-input");

        // Set probesize and max_analyze_duration AFTER avformat_open_input — it resets
        // these fields to defaults internally, so setting them before has no effect.
        if (g_mpegOpenOptionsEnabled) {
            LOGE("MPEGTS[%u]: open options: probesize=%s analyzeduration=%s",
                 sessionId, g_ffmpegOptProbeSize, g_ffmpegOptAnalyzeDuration);
            int64_t ps = (int64_t)atoll(g_ffmpegOptProbeSize);
            if (ps > 0) formatCtx->probesize = ps;
            int64_t ad = (int64_t)atoll(g_ffmpegOptAnalyzeDuration);
            if (ad >= 0) formatCtx->max_analyze_duration = ad;
        }

        LOG("MPEGTS[%u]: stream opened, probing stream info", sessionId);
        execute_initial_commands();
        displayActive = true;
        heap_probe("mpeg-post-display-init");

        int infoRet = avformat_find_stream_info(formatCtx, NULL);
        if (infoRet < 0) {
            log_ffmpeg_error("avformat_find_stream_info", infoRet);
            sessionStopReason = "avformat_find_stream_info";
            sessionOk = false;
        } else {
            LOGE("MPEGTS[%u]: stream info ok, streams=%u", sessionId, formatCtx->nb_streams);
            heap_probe("mpeg-post-find-stream-info");
        }

        if (sessionOk) {
            for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
                if (formatCtx->streams[i] && formatCtx->streams[i]->codecpar &&
                    formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    videoStreamIndex = (int)i;
                    break;
                }
            }
            if (videoStreamIndex < 0) {
                fprintf(stderr, "MPEGTS: no video stream found\n");
                sessionStopReason = "no-video-stream";
                sessionOk = false;
            } else {
                LOGE("MPEGTS[%u]: video stream index %d", sessionId, videoStreamIndex);
            }
        }

        if (sessionOk) {
            AVCodecParameters* codecpar = formatCtx->streams[videoStreamIndex]->codecpar;
            LOGE("MPEGTS[%u]: codecpar id=%d w=%d h=%d fmt=%d", sessionId,
                 codecpar->codec_id, codecpar->width, codecpar->height, codecpar->format);
            decoder = avcodec_find_decoder(codecpar->codec_id);
            if (!decoder) {
                fprintf(stderr, "MPEGTS: no decoder for codec_id %d\n", codecpar->codec_id);
                sessionStopReason = "avcodec_find_decoder";
                sessionOk = false;
            } else {
                LOGE("MPEGTS[%u]: decoder %s", sessionId, decoder->name);
                codecCtx = avcodec_alloc_context3(decoder);
                if (!codecCtx) {
                    fprintf(stderr, "MPEGTS: avcodec_alloc_context3 failed\n");
                    sessionStopReason = "avcodec_alloc_context3";
                    sessionOk = false;
                } else {
                    int paramsRet = avcodec_parameters_to_context(codecCtx, codecpar);
                    if (paramsRet < 0) {
                        log_ffmpeg_error("avcodec_parameters_to_context", paramsRet);
                        sessionStopReason = "avcodec_parameters_to_context";
                        sessionOk = false;
                    } else {
                        codecCtx->thread_count = g_ffmpegThreadCount;
                        if (g_ffmpegFastDecode) codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
                        else codecCtx->flags2 &= ~AV_CODEC_FLAG2_FAST;
                        codecCtx->skip_frame = (AVDiscard)g_ffmpegSkipFrame;

                        LOG("MPEGTS[%u]: decoder config threads=%d fast=%d skip_frame=%d drop_busy=%d",
                            sessionId,
                            codecCtx->thread_count,
                            g_ffmpegFastDecode ? 1 : 0,
                            g_ffmpegSkipFrame,
                            g_dropFramesWhenBusy ? 1 : 0);

                        int codecOpenRet = avcodec_open2(codecCtx, decoder, NULL);
                        if (codecOpenRet < 0) {
                            log_ffmpeg_error("avcodec_open2", codecOpenRet);
                            sessionStopReason = "avcodec_open2";
                            sessionOk = false;
                        } else {
                            LOGE("MPEGTS[%u]: decoder opened", sessionId);
                            heap_probe("mpeg-post-decoder-open");
                        }
                    }
                }
            }
        }

        if (sessionOk) {
            frame = av_frame_alloc();
            rgbaFrame = av_frame_alloc();
            packet = av_packet_alloc();
            if (!frame || !rgbaFrame || !packet) {
                fprintf(stderr, "MPEGTS: frame/packet alloc failed\n");
                LOGE("MPEGTS[%u]: frame=%p rgbaFrame=%p packet=%p", sessionId,
                     (void*)frame, (void*)rgbaFrame, (void*)packet);
                sessionStopReason = "av_frame_or_packet_alloc";
                sessionOk = false;
            }
        }

        uint64_t statLastUs = now_us();
        uint64_t statFrames = 0;
        bool firstFrame = true;

        if (sessionOk && g_nudgeMs > 0) {
            LOGE("MPEGTS[%u]: nudging %s for %d ms to trigger IDR on decode connection",
                 sessionId, g_streamUrl, g_nudgeMs);
            tcp_nudge(g_streamUrl, g_nudgeMs);
            LOGE("MPEGTS[%u]: nudge done", sessionId);
        }

        if (sessionOk) {
            LOGE("MPEGTS[%u]: entering decode loop", sessionId);
        }

        while (g_running && sessionOk) {
            int readRet = av_read_frame(formatCtx, packet);
            if (readRet < 0) {
                if (readRet == AVERROR(EAGAIN)) {
                    usleep(10000);
                    continue;
                }
                if (readRet == AVERROR_EXIT && !g_running) {
                    sessionStopReason = "shutdown";
                    break;
                }
                log_ffmpeg_error("av_read_frame", readRet);
                sessionStopReason = "av_read_frame";
                break;
            }

            if (packet->stream_index != videoStreamIndex) {
                av_packet_unref(packet);
                continue;
            }

            int sendRet = avcodec_send_packet(codecCtx, packet);
            if (sendRet < 0) {
                log_ffmpeg_error("avcodec_send_packet", sendRet);
                decodeErrors++;
                droppedPackets++;
                av_packet_unref(packet);
                continue;
            }
            av_packet_unref(packet);

            while (g_running) {
                int recvRet = avcodec_receive_frame(codecCtx, frame);
                if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF) break;
                if (recvRet < 0) {
                    log_ffmpeg_error("avcodec_receive_frame", recvRet);
                    decodeErrors++;
                    droppedPackets++;
                    break;
                }

                const int frameW = frame->width;
                const int frameH = frame->height;

                if (frameW <= 0 || frameH <= 0) {
                    av_frame_unref(frame);
                    continue;
                }

                if (frameW > kMaxFrameWidth || frameH > kMaxFrameHeight) {
                    fprintf(stderr, "MPEGTS: suspicious frame size %dx%d (max %dx%d)\n",
                            frameW, frameH, kMaxFrameWidth, kMaxFrameHeight);
                    sessionStopReason = "frame-size-suspicious";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }

                if (g_dropFramesWhenBusy && g_newFrameReady) {
                    droppedFrames++;
                    av_frame_unref(frame);
                    continue;
                }

                if (firstFrame) {
                    LOGE("MPEGTS[%u]: first frame decoded, %dx%d fmt=%d", sessionId,
                         frameW, frameH, frame->format);
                    heap_probe("mpeg-first-decoded-frame");
                    firstFrame = false;
                }

                bool layoutChanged = false;
                if (frameW != lastW || frameH != lastH || !swsCtx || !rgbaData) {
                    layoutChanged = true;
                    if (lastW != 0)
                        LOGE("MPEGTS[%u]: resolution change %dx%d -> %dx%d",
                             sessionId, lastW, lastH, frameW, frameH);

                    if (swsCtx) { sws_freeContext(swsCtx); swsCtx = NULL; }
                    if (rgbaData) { av_free(rgbaData); rgbaData = NULL; }

                    int rgbaSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frameW, frameH, 1);
                    if (rgbaSize <= 0) {
                        fprintf(stderr, "MPEGTS: av_image_get_buffer_size failed\n");
                        sessionStopReason = "av_image_get_buffer_size";
                        sessionOk = false;
                        av_frame_unref(frame);
                        break;
                    }

                    rgbaData = (uint8_t*)av_malloc((size_t)rgbaSize);
                    if (!rgbaData) {
                        fprintf(stderr, "MPEGTS: av_malloc rgba failed\n");
                        sessionStopReason = "av_malloc-rgba";
                        sessionOk = false;
                        av_frame_unref(frame);
                        break;
                    }

                    int fillRet = av_image_fill_arrays(rgbaFrame->data, rgbaFrame->linesize, rgbaData,
                                                       AV_PIX_FMT_RGBA, frameW, frameH, 1);
                    if (fillRet < 0) {
                        log_ffmpeg_error("av_image_fill_arrays", fillRet);
                        sessionStopReason = "av_image_fill_arrays";
                        fprintf(stderr, "MPEGTS: av_image_fill_arrays failed\n");
                        sessionOk = false;
                        av_frame_unref(frame);
                        break;
                    }

                    swsCtx = sws_getContext(frameW, frameH, (AVPixelFormat)frame->format,
                                            frameW, frameH, AV_PIX_FMT_RGBA,
                                            SWS_BILINEAR, NULL, NULL, NULL);
                    if (!swsCtx) {
                        fprintf(stderr, "MPEGTS: sws_getContext failed (format %d)\n", frame->format);
                        sessionStopReason = "sws_getContext";
                        sessionOk = false;
                        av_frame_unref(frame);
                        break;
                    }

                    lastW = frameW;
                    lastH = frameH;
                    LOG("MPEGTS[%u]: rgba bytes=%d stride=%d", sessionId, rgbaSize, rgbaFrame->linesize[0]);
                }

                int scaledLines = sws_scale(swsCtx,
                                            (const uint8_t* const*)frame->data,
                                            frame->linesize,
                                            0,
                                            frameH,
                                            rgbaFrame->data,
                                            rgbaFrame->linesize);
                if (scaledLines <= 0) {
                    LOGE("MPEGTS[%u]: sws_scale returned %d", sessionId, scaledLines);
                    decodeErrors++;
                    droppedFrames++;
                    av_frame_unref(frame);
                    continue;
                }

                size_t rowBytes = 0;
                size_t requiredSize = 0;
                if ((size_t)frameW > SIZE_MAX / 4u) {
                    fprintf(stderr, "MPEGTS: row byte size overflow for width %d\n", frameW);
                    sessionStopReason = "row-bytes-overflow";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }
                rowBytes = (size_t)frameW * 4u;
                if ((size_t)frameH > SIZE_MAX / rowBytes) {
                    fprintf(stderr, "MPEGTS: frame byte size overflow for %dx%d\n", frameW, frameH);
                    sessionStopReason = "required-size-overflow";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }
                requiredSize = rowBytes * (size_t)frameH;
                if (requiredSize > kMaxFrameBytes) {
                    LOGE("MPEGTS[%u]: required frame bytes %zu exceeds max %zu", sessionId,
                         requiredSize, kMaxFrameBytes);
                    sessionStopReason = "required-size-too-large";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }

                if (!rgbaFrame->data[0] || rgbaFrame->linesize[0] <= 0 || (size_t)rgbaFrame->linesize[0] < rowBytes) {
                    LOGE("MPEGTS[%u]: invalid RGBA output buffer line=%d need=%zu", sessionId,
                         rgbaFrame->linesize[0], rowBytes);
                    sessionStopReason = "rgba-linesize-invalid";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }

                size_t srcStride = (size_t)rgbaFrame->linesize[0];

                if (g_verbose && (!firstFramePublished || layoutChanged)) {
                    LOG("MPEGTS[%u]: copy plan w=%d h=%d row=%zu srcStride=%zu required=%zu backCap=%zu",
                        sessionId, frameW, frameH, rowBytes, srcStride, requiredSize, g_bufs.backCap);
                }

                uint64_t lockStartUs = now_us();
                pthread_mutex_lock(&g_frameMutex);
                uint64_t lockWaitUs = now_us() - lockStartUs;
                if (lockWaitUs > 2000ULL) {
                    LOG("MPEGTS[%u]: waited %.3f ms for g_frameMutex", sessionId, us_to_ms(lockWaitUs));
                }

                if (!g_bufs.ensureBackBuffer(requiredSize)) {
                    pthread_mutex_unlock(&g_frameMutex);
                    LOGE("MPEGTS[%u]: ensureBackBuffer failed (required=%zu backCap=%zu)",
                         sessionId, requiredSize, g_bufs.backCap);
                    sessionStopReason = "ensureBackBuffer";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }

                for (size_t y = 0; y < (size_t)frameH; ++y) {
                    memcpy(g_bufs.backBuffer + (y * rowBytes),
                           rgbaFrame->data[0] + (y * srcStride),
                           rowBytes);
                }

                if (!g_bufs.syncFrontBuffer()) {
                    pthread_mutex_unlock(&g_frameMutex);
                    fprintf(stderr, "MPEGTS: syncFrontBuffer OOM\n");
                    sessionStopReason = "syncFrontBuffer";
                    sessionOk = false;
                    av_frame_unref(frame);
                    break;
                }

                char* tmp = g_bufs.frontBuffer;
                g_bufs.frontBuffer = g_bufs.backBuffer;
                g_bufs.backBuffer = tmp;

                g_sharedFbW = frameW;
                g_sharedFbH = frameH;
                g_sharedFinalH = frameH;
                g_sharedTimings = FrameTimings();
                g_sharedFramesDecoded++;
                g_newFrameReady = true;

                pthread_mutex_unlock(&g_frameMutex);

                if (!firstFramePublished) {
                    firstFramePublished = true;
                    g_streamConnected = true;
                    log_buffer_caps_err("mpeg-post-first-publish");
                    heap_probe("mpeg-post-first-publish");
                }

                statFrames++;
                if (g_verbose) {
                    uint64_t now = now_us();
                    if (now - statLastUs >= 5000000ULL) {
                        double fps = (double)statFrames * 1e6 / (double)(now - statLastUs);
                        fprintf(stderr, "[%.3f] MPEGTS[%u]: %.1f fps, %dx%d caps(f=%zu b=%zu) drop(pkt=%llu frm=%llu) errs=%llu\n",
                            now / 1e6, sessionId, fps, frameW, frameH, g_bufs.frontCap, g_bufs.backCap,
                            (unsigned long long)droppedPackets,
                            (unsigned long long)droppedFrames,
                            (unsigned long long)decodeErrors);
                        statLastUs = now;
                        statFrames = 0;
                    }
                }

                av_frame_unref(frame);
            }
        }

        g_streamConnected = false;

        if (packet)   av_packet_free(&packet);
        if (frame)    av_frame_free(&frame);
        if (rgbaFrame) av_frame_free(&rgbaFrame);
        if (codecCtx) avcodec_free_context(&codecCtx);
        if (formatCtx) avformat_close_input(&formatCtx);
        if (swsCtx)   sws_freeContext(swsCtx);
        if (rgbaData) av_free(rgbaData);

        if (!g_running && strcmp(sessionStopReason, "loop-exit") == 0) {
            sessionStopReason = "shutdown";
        }

                LOGE("MPEGTS[%u]: session ended, reason=%s, running=%d, drop(pkt=%llu frm=%llu), errs=%llu",
                         sessionId,
                         sessionStopReason,
                         g_running ? 1 : 0,
                         (unsigned long long)droppedPackets,
                         (unsigned long long)droppedFrames,
                         (unsigned long long)decodeErrors);
        log_buffer_caps_err("mpeg-session-end");
        heap_probe("mpeg-session-end");

        if (displayActive) {
            LOGE("MPEGTS[%u]: restoring display context", sessionId);
            execute_final_commands();
            displayActive = false;
        }

        if (g_running) {
            if (firstFramePublished) {
                reconnectBackoffMs = kReconnectBackoffMinMs;
            } else {
                reconnectBackoffMs = next_backoff_ms(reconnectBackoffMs, kReconnectBackoffMaxMs);
            }
            LOGE("MPEGTS[%u]: reconnect backoff %u ms", sessionId, reconnectBackoffMs);
            usleep((useconds_t)reconnectBackoffMs * 1000);
        }
    }
    return NULL;
}

void* NetworkThreadFunc(void* arg) {
    LOGE("network: entering MPEGTS mode");
    return MpegTsNetworkThreadFunc(arg);
}

// ============================================================================
// MAIN
// ============================================================================
#define MAX_FRAME_WINDOW 1024

int main(int argc, char* argv[]) {
    // Snapshot built-in defaults before any env/config/CLI overrides.
    char dflt_probeSize[32];    strncpy(dflt_probeSize,   g_ffmpegOptProbeSize,        sizeof(dflt_probeSize)   - 1); dflt_probeSize[sizeof(dflt_probeSize)-1]     = '\0';
    char dflt_analyzeDur[32];   strncpy(dflt_analyzeDur,  g_ffmpegOptAnalyzeDuration,  sizeof(dflt_analyzeDur)  - 1); dflt_analyzeDur[sizeof(dflt_analyzeDur)-1]   = '\0';
    char dflt_rwTimeout[32];    strncpy(dflt_rwTimeout,   g_ffmpegOptRwTimeout,        sizeof(dflt_rwTimeout)   - 1); dflt_rwTimeout[sizeof(dflt_rwTimeout)-1]     = '\0';
    int  dflt_threadCount     = g_ffmpegThreadCount;
    int  dflt_nudgeMs         = g_nudgeMs;
    bool dflt_verbose         = g_verbose;
    bool dflt_syslog          = g_logToSyslog;
    bool dflt_mpegOpenOpts    = g_mpegOpenOptionsEnabled;

    // Scan logging flags before anything else so LOG/LOGE behave as configured.
    char remoteSyslogHost[64] = {0};
    int remoteSyslogPort = 514;
    bool runAsDaemon = false;
    bool showHelp = false;

    const char* envDaemon = getenv("RENDER_DAEMON");
    if (is_truthy_env_value(envDaemon)) runAsDaemon = true;

    const char* envHeapProbe = getenv("RENDER_HEAP_PROBE");
    if (is_truthy_env_value(envHeapProbe)) g_heapProbeEnabled = true;

    const char* envMpegOpenOpts = getenv("RENDER_MPEGTS_OPEN_OPTS");
    if (is_truthy_env_value(envMpegOpenOpts)) g_mpegOpenOptionsEnabled = true;

    const char* envSyslog = getenv("RENDER_SYSLOG");
    if (is_truthy_env_value(envSyslog)) g_logToSyslog = true;

    const char* envRemoteSpec = getenv("RENDER_SYSLOG_REMOTE");
    if (envRemoteSpec && envRemoteSpec[0] != '\0') {
        if (!parse_remote_spec(envRemoteSpec, remoteSyslogHost, sizeof(remoteSyslogHost), &remoteSyslogPort)) {
            fprintf(stderr, "[%.3f] invalid RENDER_SYSLOG_REMOTE '%s' (expected host[:port])\n",
                    now_us() / 1e6, envRemoteSpec);
        }
    }

    const char* envRemoteHost = getenv("RENDER_SYSLOG_HOST");
    if (envRemoteHost && envRemoteHost[0] != '\0') {
        strncpy(remoteSyslogHost, envRemoteHost, sizeof(remoteSyslogHost) - 1);
        remoteSyslogHost[sizeof(remoteSyslogHost) - 1] = '\0';
    }

    const char* envRemotePort = getenv("RENDER_SYSLOG_PORT");
    if (envRemotePort && envRemotePort[0] != '\0') {
        int parsedPort = 0;
        if (parse_port_number(envRemotePort, &parsedPort)) {
            remoteSyslogPort = parsedPort;
        } else {
            fprintf(stderr, "[%.3f] invalid RENDER_SYSLOG_PORT '%s'\n", now_us() / 1e6, envRemotePort);
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            showHelp = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "--syslog") == 0) {
            g_logToSyslog = true;
        } else if (strcmp(argv[i], "--no-syslog") == 0) {
            g_logToSyslog = false;
        } else if (strcmp(argv[i], "--heap-probe") == 0) {
            g_heapProbeEnabled = true;
        } else if (strcmp(argv[i], "--no-heap-probe") == 0) {
            g_heapProbeEnabled = false;
        } else if (strcmp(argv[i], "--mpegts-open-opts") == 0) {
            g_mpegOpenOptionsEnabled = true;
        } else if (strcmp(argv[i], "--no-mpegts-open-opts") == 0) {
            g_mpegOpenOptionsEnabled = false;
        } else if (strcmp(argv[i], "--daemon") == 0) {
            runAsDaemon = true;
        } else if (strcmp(argv[i], "--no-daemon") == 0) {
            runAsDaemon = false;
        } else if (strncmp(argv[i], "--syslog-remote=", 16) == 0) {
            if (!parse_remote_spec(argv[i] + 16, remoteSyslogHost, sizeof(remoteSyslogHost), &remoteSyslogPort)) {
                fprintf(stderr, "[%.3f] invalid --syslog-remote value '%s'\n", now_us() / 1e6, argv[i] + 16);
            }
        } else if (strncmp(argv[i], "--syslog-host=", 14) == 0) {
            strncpy(remoteSyslogHost, argv[i] + 14, sizeof(remoteSyslogHost) - 1);
            remoteSyslogHost[sizeof(remoteSyslogHost) - 1] = '\0';
        } else if (strncmp(argv[i], "--syslog-port=", 14) == 0) {
            int parsedPort = 0;
            if (parse_port_number(argv[i] + 14, &parsedPort)) {
                remoteSyslogPort = parsedPort;
            } else {
                fprintf(stderr, "[%.3f] invalid --syslog-port value '%s'\n", now_us() / 1e6, argv[i] + 14);
            }
        }
    }

    if (!showHelp && runAsDaemon && !g_logToSyslog && !g_logToRemoteSyslog) {
        g_logToSyslog = true;
    }

    if (!showHelp && runAsDaemon) {
        pid_t existingPid = 0;
        if (!reserve_daemon_pid_file(&existingPid)) {
            if (existingPid > 1) {
                fprintf(stderr, "daemon: already running (pid=%ld)\n", (long)existingPid);
                return 0;
            }
            fprintf(stderr, "daemon: pid file reservation failed: %s\n", strerror(errno));
            return 1;
        }

        if (daemonize_process() != 0) {
            cleanup_daemon_pid_file();
            return 1;
        }

        if (!write_pid_file(g_daemonPidFilePath, getpid())) {
            cleanup_daemon_pid_file();
            return 1;
        }

        atexit(cleanup_daemon_pid_file);
    }

    derive_syslog_ident(argv[0]);

    if (g_logToSyslog) {
        openlog(g_syslogIdent, LOG_PID | LOG_NDELAY, LOG_USER);
        syslog(LOG_NOTICE, "local syslog sink enabled");
    }

    if (remoteSyslogHost[0] != '\0') {
        if (!init_remote_syslog_sink(remoteSyslogHost, remoteSyslogPort)) {
            fprintf(stderr, "[%.3f] failed to initialize remote syslog sink '%s:%d'\n",
                    now_us() / 1e6, remoteSyslogHost, remoteSyslogPort);
        } else {
            send_remote_syslog_packet(LOG_NOTICE, "remote syslog sink enabled");
            fprintf(stderr, "[%.3f] remote syslog enabled: %s:%d\n",
                    now_us() / 1e6, g_remoteSyslogHost, g_remoteSyslogPort);
        }
    }

    if (g_logToSyslog || g_logToRemoteSyslog) {
        atexit(close_syslog_sink);
    }

    if (g_heapProbeEnabled) {
        LOGE("heap probe enabled (RENDER_HEAP_PROBE=1 or --heap-probe)");
    }

    if (g_mpegOpenOptionsEnabled) {
        LOGE("mpegts open options enabled (RENDER_MPEGTS_OPEN_OPTS=1 or --mpegts-open-opts)");
    }

    LOG("build: " __DATE__ " " __TIME__);

    char configPath[512];
    {
        char exePath[512] = {0};
        const char* base = argv[0];
        if (readlink("/proc/self/exefile", exePath, sizeof(exePath) - 1) > 0)
            base = exePath;
        const char* lastSlash = strrchr(base, '/');
        if (lastSlash && (size_t)(lastSlash - base) + 12 < sizeof(configPath)) {
            size_t dirLen = (size_t)(lastSlash - base) + 1;
            memcpy(configPath, base, dirLen);
            strcpy(configPath + dirLen, "config.txt");
        } else {
            strcpy(configPath, "config.txt");
        }
    }
    loadConfig(configPath);

// Helper: match --flag=value (returns pointer to value) or --flag value (sets *next, returns argv[i+1])
#define CLI_STR(flag, dest, destsz) \
    if (strncmp(argv[i], flag "=", sizeof(flag)) == 0) { \
        strncpy(dest, argv[i] + sizeof(flag), (destsz) - 1); \
        (dest)[(destsz)-1] = '\0'; \
    } else if (strcmp(argv[i], flag) == 0 && i + 1 < argc) { \
        strncpy(dest, argv[++i], (destsz) - 1); \
        (dest)[(destsz)-1] = '\0'; \
    }
#define CLI_INT(flag, dest, minval) \
    if (strncmp(argv[i], flag "=", sizeof(flag)) == 0) { \
        (dest) = atoi(argv[i] + sizeof(flag)); if ((dest) < (minval)) (dest) = (minval); \
    } else if (strcmp(argv[i], flag) == 0 && i + 1 < argc) { \
        (dest) = atoi(argv[++i]);             if ((dest) < (minval)) (dest) = (minval); \
    }

    // CLI args after config load — override individual settings
    for (int i = 1; i < argc; i++) {
        CLI_STR("--url",               g_streamUrl,            sizeof(g_streamUrl))
        else CLI_STR("--probe-size",        g_ffmpegOptProbeSize,   sizeof(g_ffmpegOptProbeSize))
        else CLI_STR("--analyze-duration",  g_ffmpegOptAnalyzeDuration, sizeof(g_ffmpegOptAnalyzeDuration))
        else CLI_STR("--rw-timeout",        g_ffmpegOptRwTimeout,   sizeof(g_ffmpegOptRwTimeout))
        else CLI_INT("--threads",           g_ffmpegThreadCount,    1)
        else CLI_INT("--nudge-ms",          g_nudgeMs,              0)
    }
#undef CLI_STR
#undef CLI_INT
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        strncpy(g_streamUrl, argv[i], sizeof(g_streamUrl) - 1);
        g_streamUrl[sizeof(g_streamUrl) - 1] = '\0';
        break;
    }

    if (showHelp) {
        char remoteSyslogSpec[80] = "(not set)";
        if (remoteSyslogHost[0] != '\0')
            snprintf(remoteSyslogSpec, sizeof(remoteSyslogSpec), "%s:%d", remoteSyslogHost, remoteSyslogPort);
        fprintf(stdout,
            "usage: %s [options] [--url=]<uri>\n"
            "\n"
            "  <uri>                          stream URI (tcp://, rtsp://, udp://, ...)  [%s]\n"
            "  --url=<uri>                    stream URI (overrides config.txt)\n"
            "\n"
            "  --probe-size <bytes>           ffmpegProbeSize (default: %s)  [%s]\n"
            "  --analyze-duration <us>        ffmpegAnalyzeDuration (default: %s, 0=fastest)  [%s]\n"
            "  --rw-timeout <us>              ffmpegRwTimeout (default: %s)  [%s]\n"
            "  --threads <n>                  ffmpegThreadCount (default: %d)  [%d]\n"
            "  --nudge-ms <ms>                ffmpegNudgeMs; tcp:// only (default: %d)  [%d]\n"
            "\n"
            "  --verbose                      enable verbose logging (default: %s)  [%s]\n"
            "  --syslog / --no-syslog         enable/disable local syslog (default: %s)  [%s]\n"
            "  --syslog-remote=<host[:port]>  send logs to remote syslog  [%s]\n"
            "  --daemon / --no-daemon         run as background daemon (default: no)  [%s]\n"
            "  --mpegts-open-opts / --no-mpegts-open-opts (default: %s)  [%s]\n"
            "\n"
            "config.txt in the same directory as the binary overrides built-in defaults.\n",
            argv[0],
            g_streamUrl[0] ? g_streamUrl : "(not set)",
            dflt_probeSize,      g_ffmpegOptProbeSize,
            dflt_analyzeDur,     g_ffmpegOptAnalyzeDuration,
            dflt_rwTimeout,      g_ffmpegOptRwTimeout,
            dflt_threadCount,    g_ffmpegThreadCount,
            dflt_nudgeMs,        g_nudgeMs,
            dflt_verbose         ? "yes" : "no", g_verbose             ? "yes" : "no",
            dflt_syslog          ? "yes" : "no", g_logToSyslog         ? "yes" : "no",
            remoteSyslogSpec,
            runAsDaemon          ? "yes" : "no",
            dflt_mpegOpenOpts    ? "yes" : "no", g_mpegOpenOptionsEnabled ? "yes" : "no");
        return 0;
    }

    if (g_streamUrl[0] == '\0') {
        fprintf(stderr, "usage: opengl-render-qnx-stream-player [--url=]<uri>\n");
        fprintf(stderr, "  e.g.  tcp://10.0.0.1:1234  rtsp://host/stream  udp://...\n");
        return 1;
    }

    LOG("config: url=%s window=%dx%d", g_streamUrl, windowWidth, windowHeight);

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    LOG("display: loading libdisplayinit.so");
    void* func_handle = dlopen("libdisplayinit.so", RTLD_LAZY);
    if (!func_handle) { fprintf(stderr, "Error: %s\n", dlerror()); return 1; }

    void (*display_init)(int, int) = (void (*)(int, int))dlsym(func_handle, "display_init");
    if (display_init) { display_init(0, 0); LOG("display: display_init ok"); }
    dlclose(func_handle);

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) { fprintf(stderr, "EGL: no display\n"); return 1; }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(eglDisplay, &major, &minor)) {
        fprintf(stderr, "EGL: eglInitialize failed (0x%x)\n", eglGetError());
        return 1;
    }
    LOG("EGL: initialized v%d.%d", major, minor);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 1, EGL_GREEN_SIZE, 1, EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };

    EGLConfig configs[5];
    EGLint num_configs = 0;
    EGLNativeWindowType windowEgl;
    int kdWindow = 0;

    eglChooseConfig(eglDisplay, config_attribs, configs, 5, &num_configs);
    if (num_configs == 0) { fprintf(stderr, "EGL: no suitable config\n"); return 1; }
    eglConfig = configs[0];
    LOG("EGL: %d config(s) found", num_configs);

    void* func_handle_d_c_w = dlopen("libdisplayinit.so", RTLD_LAZY);
    void (*display_create_window)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*) =
        (void (*)(EGLDisplay, EGLConfig, int, int, int, EGLNativeWindowType*, int*))dlsym(func_handle_d_c_w, "display_create_window");

    if (display_create_window) {
        display_create_window(eglDisplay, configs[0], windowWidth, windowHeight, 3, &windowEgl, &kdWindow);
        LOG("display: window created %dx%d", windowWidth, windowHeight);
    }
    dlclose(func_handle_d_c_w);

    eglSurface = eglCreateWindowSurface(eglDisplay, configs[0], windowEgl, 0);
    if (eglSurface == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
        return 1;
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, configs[0], EGL_NO_CONTEXT, context_attribs);
    if (eglContext == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL: eglCreateContext failed (0x%x)\n", eglGetError());
        return 1;
    }

    if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        fprintf(stderr, "EGL: eglMakeCurrent failed (0x%x)\n", eglGetError());
        return 1;
    }
    LOG("EGL: context current");

    heap_probe("pre-Init");
    Init();
    heap_probe("post-Init");

    size_t startupFrameBytes = (size_t)1280 * (size_t)720 * 4u;
    if (!g_bufs.ensureFrameBuffers(startupFrameBytes)) {
        LOGE("buffers: ensureFrameBuffers startup allocation failed");
        return 1;
    }
    LOG("buffers: pre-allocated");
    log_buffer_caps_err("main-post-prealloc");
    heap_probe("post-buffers");

    GLuint textureID;
    glGenTextures(1, &textureID);
    heap_probe("post-glGenTextures");
    glBindTexture(GL_TEXTURE_2D, textureID);
    heap_probe("post-glBindTexture");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    heap_probe("post-glTexParameteri");
    LOG("GL: texture %u allocated", textureID);

    pthread_t networkThread;
    int threadCreateRet = pthread_create(&networkThread, NULL, NetworkThreadFunc, NULL);
    if (threadCreateRet != 0) {
        LOGE("pthread_create(NetworkThreadFunc) failed: %s (%d)",
             strerror(threadCreateRet), threadCreateRet);
        return 1;
    }
    LOGE("network thread started (mpegts, tid=%lu)", (unsigned long)networkThread);
    heap_probe("post-pthread_create");

    // NOTE: execute_initial_commands() is NOT called here.
    // Each network thread calls it after a successful connection is established,
    // and execute_final_commands() when the session ends. This prevents the
    // double-dmdt-call that caused "Create new display table failed: 2".

    int prevFbW = 0, prevFinalH = 0;
    bool firstFrame = true;

    int frameCount = 0;
    double renderFps = 0.0;
    uint64_t lastFpsUs = now_us();

    // Sliding window for decode FPS (1-second window)
    uint64_t frameTimestamps[MAX_FRAME_WINDOW];
    int frameHead = 0;
    int frameTail = 0;
    int frameCountWindow = 0;
    uint64_t lastDecodedCount = 0;
    uint64_t noNewFrameSinceUs = 0;
    uint64_t lastNoFrameLogUs = 0;
    uint64_t lastInvalidFrameLogUs = 0;

    FrameTimings displayTimings;

    while (g_running) {
        uint64_t frameStartUs = now_us();
        double textureUploadMs = 0.0;
        int renderFbW = 0, renderFinalH = 0;

        pthread_mutex_lock(&g_frameMutex);

        uint64_t currentDecoded = g_sharedFramesDecoded;

        if (g_newFrameReady) {
            renderFbW = g_sharedFbW;
            renderFinalH = g_sharedFinalH;
            displayTimings = g_sharedTimings;

            if (renderFbW <= 0 || renderFinalH <= 0 || !g_bufs.frontBuffer) {
                uint64_t now = now_us();
                if (now - lastInvalidFrameLogUs >= 1000000ULL) {
                    LOGE("render: dropping invalid frame publish w=%d h=%d front=%p",
                         renderFbW, renderFinalH, (void*)g_bufs.frontBuffer);
                    log_buffer_caps_err("render-invalid-frame");
                    lastInvalidFrameLogUs = now;
                }
                g_newFrameReady = false;
            } else {
                uint64_t texStart = now_us();
                if (firstFrame || renderFbW != prevFbW || renderFinalH != prevFinalH) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderFbW, renderFinalH, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
                    if (firstFrame)
                        LOGE("render: first frame %dx%d", renderFbW, renderFinalH);
                    prevFbW = renderFbW;
                    prevFinalH = renderFinalH;
                    firstFrame = false;
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, renderFbW, renderFinalH,
                                    GL_RGBA, GL_UNSIGNED_BYTE, g_bufs.frontBuffer);
                }
                textureUploadMs = us_to_ms(now_us() - texStart);
            }
            g_newFrameReady = false;
        } else {
            renderFbW = prevFbW;
            renderFinalH = prevFinalH;
        }
        pthread_mutex_unlock(&g_frameMutex);

        // --- Calculate Sliding Window Decode FPS ---
        uint64_t newFrames = currentDecoded - lastDecodedCount;
        lastDecodedCount = currentDecoded;
        uint64_t currentLoopTimeUs = now_us();

        if (newFrames == 0) {
            if (noNewFrameSinceUs == 0) noNewFrameSinceUs = currentLoopTimeUs;
            if ((currentLoopTimeUs - noNewFrameSinceUs) >= 3000000ULL &&
                (currentLoopTimeUs - lastNoFrameLogUs) >= 3000000ULL) {
                LOGE("render: no published frames for %.1f s (decoded=%llu)",
                     (double)(currentLoopTimeUs - noNewFrameSinceUs) / 1e6,
                     (unsigned long long)currentDecoded);
                log_buffer_caps_err("render-no-new-frames");
                lastNoFrameLogUs = currentLoopTimeUs;
            }
        } else {
            noNewFrameSinceUs = 0;
            if (g_verbose && newFrames > 1) {
                LOG("render: burst publish count=%llu", (unsigned long long)newFrames);
            }
        }

        for (uint64_t i = 0; i < newFrames; i++) {
            frameTimestamps[frameHead] = currentLoopTimeUs;
            frameHead = (frameHead + 1) % MAX_FRAME_WINDOW;
            if (frameCountWindow < MAX_FRAME_WINDOW) {
                frameCountWindow++;
            } else {
                frameTail = (frameTail + 1) % MAX_FRAME_WINDOW;
            }
        }

        while (frameCountWindow > 0) {
            if ((currentLoopTimeUs - frameTimestamps[frameTail]) > 1000000ULL) {
                frameTail = (frameTail + 1) % MAX_FRAME_WINDOW;
                frameCountWindow--;
            } else {
                break;
            }
        }
        double decodeFps = (double)frameCountWindow;

        // --- Rendering ---
        glViewport(0, 0, windowWidth, windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(programObject);

        if (g_pixelPerfect && renderFbW > 0 && renderFinalH > 0) {
            int vpX = (windowWidth  - renderFbW)  / 2;
            int vpY = (windowHeight - renderFinalH) / 2;
            glViewport(vpX, vpY, renderFbW, renderFinalH);
        }

        if (renderFbW > renderFinalH) {
            glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, landscapeVertices);
            glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, landscapeTexCoords);
        } else {
            glVertexAttribPointer(g_posAttr, 3, GL_FLOAT, GL_FALSE, 0, portraitVertices);
            glVertexAttribPointer(g_texAttr, 2, GL_FLOAT, GL_FALSE, 0, portraitTexCoords);
        }
        glEnableVertexAttribArray(g_posAttr);
        glEnableVertexAttribArray(g_texAttr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableVertexAttribArray(g_posAttr);
        glDisableVertexAttribArray(g_texAttr);

        if (!g_streamConnected) {
            float msgX = -(float)windowWidth * 0.19f;
            glUseProgram(programObjectTextRender);
            print_string(msgX, 0.0f, "No signal", 1.0f, 0.0f, 0.0f, 110.0f);
        }

        if (g_verbose) {
            frameCount++;
            uint64_t nowUs = now_us();
            displayTimings.total_frame_ms = us_to_ms(nowUs - frameStartUs);

            if ((nowUs - lastFpsUs) >= 1000000ULL) {
                renderFps = (double)frameCount * 1000000.0 / (double)(nowUs - lastFpsUs);
                frameCount = 0;
                lastFpsUs = nowUs;
            }

            char overlayText[512];
            snprintf(overlayText, sizeof(overlayText),
                "Render FPS: %.1f\nDecode FPS: %.1f\nFrame: %.2f ms\nGPU Up: %.2f ms",
                renderFps, decodeFps, displayTimings.total_frame_ms, textureUploadMs);

            if (g_debugOverlayEnabled) {
                float overlayX = -((float)windowWidth * 0.5f) + g_debugOverlayMarginX;
                float overlayY = ((float)windowHeight * 0.5f) - g_debugOverlayMarginY;
                glUseProgram(programObjectTextRender);
                print_string(overlayX, overlayY, overlayText, 1.0f, 1.0f, 0.0f, g_debugOverlayScale);
            }
        }

        eglSwapBuffers(eglDisplay, eglSurface);
    }

    LOG("shutting down, joining network thread");
    pthread_join(networkThread, NULL);
    LOG("network thread joined");

    glDeleteTextures(1, &textureID);
    return EXIT_SUCCESS;
}
