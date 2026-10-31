#!/bin/bash
# Current RGI build: run inside qnx-v37-gcc8-renderer with this project mounted.
# Example: bash /work/Tools/build_rgi_qnx.sh [/work/Toolbox/carplay_alt_screen/dev-build/rgi-current]
set -eu
export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
PROJECT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
SRC="$PROJECT/Toolbox/carplay_alt_screen"
PUBLISH="${1:-$SRC/dev-build/rgi-current}"
OUT=$(mktemp -d /tmp/rgi-build.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$PUBLISH"
CC=arm-unknown-nto-qnx6.5.0eabi-gcc
CXX=arm-unknown-nto-qnx6.5.0eabi-g++
NM=arm-unknown-nto-qnx6.5.0eabi-nm
cd "$SRC/rgi_native/src"
SRCS="framework/logging.c framework/state_trace.c framework/signal_guard.c framework/bus.c framework/iap2_protocol.c framework/hook_framework.c routeguidance/rgd_tlv.c routeguidance/rgd_hook.c coverart/jpeg_safety.c coverart/coverart_stream.c coverart/coverart_hook.c oemmap/oem_map_gate.c telephony/tel_hook.c main.c"
$CC -shared -fPIC -O2 -std=gnu99 -D__QNX__ -fvisibility=hidden -fdata-sections -ffunction-sections -I. $SRCS -o "$OUT/libcarplay_rgi_meta.so" -Wl,--gc-sections -Wl,--version-script="$SRC/rgi_native/src/carplay_hook.exports.map" -l:libz.so.2 -lsocket
awk '/global:/{g=1;next} /local:/{g=0} g{gsub(/[;[:space:]]/,""); if(length) print}' carplay_hook.exports.map | sort > "$OUT/expected"
$NM -D --defined-only "$OUT/libcarplay_rgi_meta.so" | awk 'NF>=3{print $3}' | sort > "$OUT/actual"
diff -u "$OUT/expected" "$OUT/actual"
cd "$SRC/rgi_renderer/src"
MR_SRCS="main.c render.c maneuver.c route_path.c server.c supervisor_lease.c platform_qnx.c ../common/cluster_surface.c"
gen_stub(){ local so="$1" rx="$2"; shift 2
 grep -rhoE "$rx" "$@" | sort -u | sed 's/.*/int &(){return 0;}/' > "$OUT/st_$so.c"
 $CC -shared -fPIC -Wl,-soname,"$so" "$OUT/st_$so.c" -o "$OUT/$so"
}
gen_stub libscreen.so.1 '\bscreen_[a-z_]+' $MR_SRCS
gen_stub libEGL.so.1 '\begl[A-Z][A-Za-z0-9]+' $MR_SRCS
gen_stub libGLESv2.so.1 '\bgl[A-Z][A-Za-z0-9]+' $MR_SRCS
mkdir -p "$OUT/rgi-scene"
OBJS=""
for source in scene/scene.cpp scene/geometry.cpp scene/layout.cpp scene/lane_panel.cpp; do
 obj="$OUT/rgi-scene/$(basename "$source" .cpp).o"
 $CXX -O2 -std=c++11 -Wall -Wextra -fno-exceptions -fno-rtti -D__QNX__ -DPLATFORM_QNX -fdata-sections -ffunction-sections -isystem "$QNX_TARGET/usr/include/c++/4.4.2" -isystem "$QNX_TARGET/usr/include/c++/4.4.2/arm-unknown-nto-qnx6.5.0eabi" -I. -I../common -I"$SRC/rgi_renderer/toolchain/qnx65-abi/include" -c "$source" -o "$obj"
 OBJS="$OBJS $obj"
done
$CC -O2 -std=gnu99 -Wall -D__QNX__ -DPLATFORM_QNX -fdata-sections -ffunction-sections -I. -I../common -I"$SRC/rgi_renderer/toolchain/qnx65-abi/include" $MR_SRCS $OBJS -o "$OUT/maneuver_render" -Wl,--gc-sections -Wl,--allow-shlib-undefined -L"$OUT" -l:libscreen.so.1 -l:libEGL.so.1 -l:libGLESv2.so.1 -lsocket -lm
if $NM -u $OBJS | grep -E '(__cxa|_ZSt|_ZTI|_ZTV|_Zn[aw]|_Zd[al]|gxx_personality)'; then exit 1; fi
for b in "$OUT/libcarplay_rgi_meta.so" "$OUT/maneuver_render"; do
 if $NM "$b" | grep -i emutls; then exit 1; fi
 arm-unknown-nto-qnx6.5.0eabi-readelf -h "$b"
 arm-unknown-nto-qnx6.5.0eabi-readelf -d "$b"
done
cp "$OUT/libcarplay_rgi_meta.so" "$OUT/maneuver_render" "$PUBLISH/"
echo RGI_START_DEBUG_ARM_BUILD=PASS
