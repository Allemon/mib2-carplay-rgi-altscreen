#!/bin/bash
#
# Build the two binaries the fork changes, in Docker:
#
#   libcarplay_altscreen.so        Toolbox/carplay_alt_screen/src (cluster map area)
#                                  clang/lld, image from docker/hook.Dockerfile
#   carplay-alt111-mirror-display  Toolbox/carplay_alt_screen/mirror_display (cluster map shift)
#                                  QNX 6.5 SDP + GCC 8.5, image qnx65-armv7-toolchain
#                                  (https://github.com/luka-dev/qnx65-armv7-toolchain:
#                                   ./host-scripts/qnx-run.sh build 8.5, then tag it :latest)
#
#   ./scripts/build_native.sh            # both
#   ./scripts/build_native.sh hook       # only the preload
#   ./scripts/build_native.sh mirror     # only the map display
#
# Output: build/native/libcarplay_altscreen.so, build/native/carplay-alt111-mirror-display.
# The HMI JAR, libcarplay_rgi_meta.so and maneuver_render are unchanged from V3.8 Fix1;
# scripts/build_sd.sh takes them from the release, checked against SHA256SUMS-SD.txt.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$PROJECT_DIR/build/native"
HOOK_IMG=mmi-cockpit-hook-builder
QNX_IMG=${QNX_IMG:-qnx65-armv7-toolchain:latest}
what=${1:-all}
case "$what" in all|hook|mirror) ;; *) echo "usage: ./scripts/build_native.sh [hook|mirror]"; exit 2 ;; esac
mkdir -p "$OUT"

build_hook() {
    if ! docker image inspect "$HOOK_IMG" >/dev/null 2>&1; then
        echo "=== building image $HOOK_IMG (docker/hook.Dockerfile) ==="
        docker build --platform=linux/amd64 -t "$HOOK_IMG" -f "$PROJECT_DIR/docker/hook.Dockerfile" "$PROJECT_DIR/docker"
    fi
    echo "=== libcarplay_altscreen.so ==="
    rm -rf "$OUT/universal"
    docker run --rm --platform=linux/amd64 -v "$PROJECT_DIR":/src -w /src "$HOOK_IMG" \
        /bin/sh /src/Toolbox/carplay_alt_screen/src/build_qnx_arm.sh /src/build/native/universal \
        > "$OUT/universal.log" 2>&1 || { tail -n 40 "$OUT/universal.log"; echo "ERROR: hook build failed ($OUT/universal.log)"; exit 1; }
    grep -q '^QNX_ELF_INVARIANTS=PASS' "$OUT/universal.log" \
        || { echo "ERROR: hook ELF invariants not confirmed ($OUT/universal.log)"; exit 1; }
    cp "$OUT/universal/libcarplay_altscreen.so" "$OUT/libcarplay_altscreen.so"
    echo "  $OUT/libcarplay_altscreen.so"
}

build_mirror() {
    if ! docker image inspect "$QNX_IMG" >/dev/null 2>&1; then
        echo "ERROR: docker image '$QNX_IMG' not found."
        echo "Build it once from https://github.com/luka-dev/qnx65-armv7-toolchain :"
        echo "  ./host-scripts/qnx-run.sh build 8.5 && docker tag qnx65-armv7-toolchain:8.5 qnx65-armv7-toolchain:latest"
        exit 1
    fi
    echo "=== carplay-alt111-mirror-display ==="
    docker run --rm --platform=linux/amd64 -v "$PROJECT_DIR":/src -w /src "$QNX_IMG" bash -c '
        set -e
        export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
        export PATH=$QNX_HOST/usr/bin:$PATH
        for s in "$QNX_HOST"/usr/bin/arm-unknown-nto-qnx6.5.0eabi-strings "$(command -v strings || true)"; do
            [ -n "$s" ] && [ -x "$s" ] && { export STRINGS=$s; break; }
        done
        cd /src/Toolbox/carplay_alt_screen/mirror_display
        /bin/sh ./build_qnx.sh
    ' > "$OUT/mirror.log" 2>&1 || { tail -n 40 "$OUT/mirror.log"; echo "ERROR: mirror build failed ($OUT/mirror.log)"; exit 1; }
    grep -q '^MIRROR_BUILD=PASS' "$OUT/mirror.log" \
        || { echo "ERROR: mirror marker checks not confirmed ($OUT/mirror.log)"; exit 1; }
    cp"$PROJECT_DIR/Toolbox/carplay_alt_screen/mirror_display/build/carplay-alt111-mirror-display" \
       "$OUT/carplay-alt111-mirror-display"
    echo "  $OUT/carplay-alt111-mirror-display"
}

case "$what" in
    all) build_hook; build_mirror ;;
    hook) build_hook ;;
    mirror) build_mirror ;;
esac
