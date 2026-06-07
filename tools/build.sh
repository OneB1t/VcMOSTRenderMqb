#!/usr/local/bin/bash
set -e

QNX_SDP=/usr/qnx650
export QNX_HOST=${QNX_HOST:-$QNX_SDP/host/qnx6/x86}
export QNX_TARGET=${QNX_TARGET:-$QNX_SDP/target/qnx6}
export QCONFIG=${QCONFIG:-$QNX_TARGET/usr/include/qconfig.mk}
export PATH=$QNX_HOST/usr/bin:/usr/local/bin:$PATH

FFMPEG_ROOT_ARM=${FFMPEG_ROOT_ARM:-/root/ffmpeg-6.1.5}
STRIP=$QNX_HOST/usr/bin/ntoarmv7-strip

cd "$(dirname "$0")/../opengl-render-qnx-stream-player"

make FFMPEG_ROOT="$FFMPEG_ROOT_ARM" CPULIST=arm "$@"

if [ -x "$STRIP" ] && [ -f arm/o-le-v7/stream-player ]; then
    "$STRIP" arm/o-le-v7/stream-player
    echo "stripped: arm/o-le-v7/stream-player"
fi
