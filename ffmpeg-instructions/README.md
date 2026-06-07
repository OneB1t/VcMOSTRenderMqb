# FFmpeg 6.1.5 — QNX ARMv7 Build Notes

## Source state

The FFmpeg 6.1.5 source tree is **unmodified** from the official release tarball.
No patches are applied. The build is entirely driven by configure flags and
make-time variable overrides.

```
SHA256: 64a87651c41e830550719b1c06102d20ed3447cb5e45ad80bea0fc65d2401aae  ffmpeg-6.1.5.tar.bz2
```

Official source: https://ffmpeg.org/releases/ffmpeg-6.1.5.tar.bz2

## Requirements

- QNX SDP 6.5.0 installed at `/usr/qnx650`
- bash at `/usr/local/bin/bash` (QNX `/bin/sh` is ksh; configure uses bash syntax)
- `/usr/local/bin` in PATH for `mktemp`

## Configure — full build (recommended)

Enables all built-in decoders, demuxers, and protocols. Gives you RTSP, HLS,
UDP multicast, MPEG-TS over TCP, and anything else FFmpeg supports.

```sh
export PATH=/usr/local/bin:/usr/qnx650/host/qnx6/x86/usr/bin:$PATH
export QNX_HOST=/usr/qnx650/host/qnx6/x86
export QNX_TARGET=/usr/qnx650/target/qnx6

cd /path/to/ffmpeg-6.1.5
/usr/local/bin/bash ./configure \
  --cc=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-gcc \
  --ar=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-ar \
  --ld=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-gcc \
  --arch=arm \
  --target-os=qnx \
  --disable-asm \
  --disable-debug \
  --enable-cross-compile \
  --enable-network \
  '--extra-cflags=-D_QNX_SOURCE -I/usr/qnx650/target/qnx6/usr/include -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16' \
  --extra-ldflags=-L/usr/qnx650/target/qnx6/armle-v7/usr/lib \
  --extra-libs=-lsocket \
  --disable-doc
```

## Configure — minimal build (H.264 / MPEG-TS / TCP only)

Restricts the build to exactly what is needed for MPEG-TS over TCP with H.264.
Useful if binary size matters or you only need aa-proxy-rs style streaming.

```sh
export PATH=/usr/local/bin:/usr/qnx650/host/qnx6/x86/usr/bin:$PATH
export QNX_HOST=/usr/qnx650/host/qnx6/x86
export QNX_TARGET=/usr/qnx650/target/qnx6

cd /path/to/ffmpeg-6.1.5
/usr/local/bin/bash ./configure \
  --cc=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-gcc \
  --ar=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-ar \
  --ld=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-gcc \
  --arch=arm \
  --target-os=qnx \
  --disable-asm \
  --disable-debug \
  --enable-cross-compile \
  --enable-network \
  --enable-protocol=tcp \
  --enable-demuxer=mpegts \
  --enable-decoder=h264 \
  --enable-muxer=null \
  '--extra-cflags=-D_QNX_SOURCE -I/usr/qnx650/target/qnx6/usr/include -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16' \
  --extra-ldflags=-L/usr/qnx650/target/qnx6/armle-v7/usr/lib \
  --extra-libs=-lsocket \
  --disable-doc
```

## Build command

```sh
/usr/qnx650/host/qnx6/x86/usr/bin/make -j1 \
  RANLIB=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-ranlib \
  STRIP=/usr/qnx650/host/qnx6/x86/usr/bin/ntoarmv7-strip
```

`-j1` is required — parallel make is unreliable with this toolchain.

`RANLIB` must be overridden because `config.mak` writes `RANLIB=ranlib` without a
path, and plain `ranlib` is not in the default SSH PATH.

`STRIP` must be overridden for the same reason.

## Known configure quirk: ARCH_ARM stays 0

`configure`'s `check_arm_arch` probe reads `$cpuflags`, not `$CFLAGS`. Without
`--cpu`, `$cpuflags` is empty and the ARM detection probe fails. `config.h` will
contain `#define ARCH_ARM 0` even though the output binary is built with
`-march=armv7-a`.

Effect: no ARM-specific fast paths are compiled. The ARMv7 flags in `--extra-cflags`
still reach the compiler via `$CFLAGS` and produce correct code — they are just
invisible to configure's feature probes.

## Expected build warnings (harmless)

- `-pthread` passed to `ntoarmv7-gcc` — silently ignored on QNX; network uses `-lsocket`.
- `libavfilter` ranlib step fails — the filter library is not used by the application.
- x86 FFmpeg programs always fail to link — the static libraries are ARM-only.
