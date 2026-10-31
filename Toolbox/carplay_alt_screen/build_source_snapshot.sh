#!/bin/sh
# Build entry point for source files intentionally vendored into this repository.
# GitHub Download ZIP users do not need git submodules.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
OUTROOT="${ALTSCREEN_SOURCE_BUILD_ROOT:-$ROOT/dev-build}"
fail(){ echo "FAIL: $*" >&2; exit 1; }
case "${1:-}" in
  mirror)
    ME="$ROOT/mirror_display"
    [ -f "$ME/src/main.cpp" ] || fail "vendored Mirror source is missing"
    OUT="${2:-$OUTROOT/mirror}"
    mkdir -p "$OUT"
    /bin/sh "$ME/build_qnx.sh"
    BIN="$ME/build/carplay-alt111-mirror-display"
    [ -x "$BIN" ] || fail "Mirror build completed without expected binary: $BIN"
    cp "$BIN" "$OUT/carplay-alt111-mirror-display"
    echo "MIRROR_BUILD=PASS output=$OUT/carplay-alt111-mirror-display"
    ;;
  hook|universal)
    SRC="$ROOT/src"
    [ -f "$SRC/altscreen_hook.c" ] || fail "vendored universal-hook source is missing"
    [ -x "$SRC/build_qnx_arm.sh" ] || fail "universal-hook build script is missing or not executable"
    OUT="${2:-$OUTROOT/universal}"
    mkdir -p "$OUT"
    /bin/sh "$SRC/build_qnx_arm.sh" "$OUT"
    BIN="$OUT/libcarplay_altscreen.so"
    [ -s "$BIN" ] || fail "universal hook build completed without expected binary: $BIN"
    echo "UNIVERSAL_BUILD=PASS output=$BIN"
    ;;
  *)
    echo "usage: $0 mirror [output-dir]" >&2
    echo "       $0 universal [output-dir]" >&2
    echo "or run ./BUILD-MIRROR-QNX.sh / ./BUILD-UNIVERSAL-QNX.sh from repository root" >&2
    exit 2
    ;;
esac
