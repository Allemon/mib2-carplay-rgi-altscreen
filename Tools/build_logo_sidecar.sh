#!/bin/sh
# Run inside the existing qnx-v37-gcc8-renderer image, project mounted at /work.
set -eu
export LC_ALL=C
OUTPUT=${1:-/work/Toolbox/carplay_alt_screen/mirror_display/build}
mkdir -p /build/project
cp -R /work/Toolbox/carplay_alt_screen/mirror_display /build/project/mirror_display
cp -R /work/Toolbox/carplay_alt_screen/src /build/project/src
cd /build/project/mirror_display
make CC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc" \
    QNX_HOST="$QNX_HOST" QNX_TARGET="$QNX_TARGET" \
    "CXXFLAGS=-O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -nostdinc++ -I$QNX_TARGET/usr/include/c++/4.4.2 -I$QNX_TARGET/usr/include/c++/4.4.2/arm-unknown-nto-qnx6.5.0eabi -isystem $QNX_TARGET/usr/include" \
    'LDFLAGS=-l:libEGL.so.1 -l:libGLESv2.so.1 -lm -l:libz.a -lsocket' BUILD_DIR=/build/mirror-out
mkdir -p "$OUTPUT"
cp /build/mirror-out/carplay-alt111-mirror-display \
    "$OUTPUT/"
readelf -h /build/mirror-out/carplay-alt111-mirror-display
readelf -d /build/mirror-out/carplay-alt111-mirror-display
