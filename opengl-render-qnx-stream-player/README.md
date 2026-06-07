# stream-player

Standalone stream player for QNX ARM. Plays any stream supported by FFmpeg (RTSP, HLS, MPEG-TS over TCP, etc.). No VNC support.

## Prerequisites

- **QNX SDP 6.5** installed on the build host (`ntoarmv7-gcc`, `ntoarmv7-ar`, etc.)
- **bash** built from source and installed at `/usr/local/bin/bash`. QNX's default shell is ksh; FFmpeg's `configure` requires bash.
  See `bash-instructions/` for source patches and build instructions.
- **mktemp** — QNX ships without it. Install the replacement from this repo:
  ```sh
  cp tools/mktemp /usr/local/bin/mktemp && chmod +x /usr/local/bin/mktemp
  ```
- **FFmpeg 6.1.5** built for ARMv7. See `ffmpeg-instructions/` for configure and build instructions.
  Use the full build to get RTSP, HLS, UDP multicast, and everything else. The minimal build
  (H.264 / MPEG-TS / TCP only) is sufficient if you only need aa-proxy-rs style streaming.

## Build

On the QNX build host, from the `stream-player` directory:

```sh
QNX_HOST=/usr/qnx650/host/qnx6/x86
QNX_TARGET=/usr/qnx650/target/qnx6

$QNX_HOST/usr/bin/make \
  FFMPEG_ROOT=/root/ffmpeg-6.1.5 \
  CPULIST=arm
```

Output binary: `arm/o-le-v7/stream-player`

## Run on target

```sh
./stream-player <uri>
# or
./stream-player --url=<uri>
```

Examples:

```sh
./stream-player tcp://10.173.189.62:1234
./stream-player rtsp://192.168.1.1/stream
./stream-player udp://239.0.0.1:5004
```

## Arguments

```
stream-player [options] [--url=]<uri>
```

| Argument | Description |
|---|---|
| `<uri>` | Stream URI as a bare positional argument |
| `--url=<uri>` | Stream URI (overrides `config.txt`) |
| `--probe-size <bytes>` | Override `ffmpegProbeSize` (e.g. `32768`) |
| `--analyze-duration <us>` | Override `ffmpegAnalyzeDuration` (`0` = fastest) |
| `--rw-timeout <us>` | Override `ffmpegRwTimeout` (e.g. `5000000`) |
| `--threads <n>` | Override `ffmpegThreadCount` |
| `--nudge-ms <ms>` | Override `ffmpegNudgeMs`. **TCP only** — silently ignored for non-`tcp://` URLs. Opens a brief second connection after probing to trigger a fresh IDR before the decode loop starts (default: 0) |
| `--verbose` | Enable verbose logging |
| `--syslog` / `--no-syslog` | Enable/disable local syslog |
| `--syslog-remote=<host[:port]>` | Send logs to a remote syslog server |
| `--daemon` / `--no-daemon` | Run as a background daemon |
| `--mpegts-open-opts` / `--no-mpegts-open-opts` | Enable/disable FFmpeg MPEG-TS open options |
| `--help` | Show usage |

All arguments that take a value accept both `--flag value` and `--flag=value` forms.

Config file `config.txt` (located in the same directory as the binary) overrides built-in defaults; CLI arguments override config.

```
streamUrl = tcp://10.173.189.62:1234
```
