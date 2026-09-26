#!/bin/sh
set -u
BASE="$0"
RESOLVED=$(command -v -- "$BASE" 2>/dev/null)
[ -n "$RESOLVED" ] || RESOLVED="$BASE"
SCRIPTDIR=$(cd -P -- "$(dirname -- "$RESOLVED")" 2>/dev/null && pwd -P)
[ -n "$SCRIPTDIR" ] || { echo "FAIL: cannot resolve STATUS directory"; exit 126; }

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
if [ "$TESTING" = 1 ]; then DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}; fi
APP_BIN="$DEVICE_ROOT/mnt/app/root/carplay-altscreen/bin"
APP_SELF="$APP_BIN/status_mmi_cockpit_carplay_test.sh"
if [ "$SCRIPTDIR" != "$APP_BIN" ] && [ -f "$APP_SELF" ] && [ -f "$APP_BIN/altscreen_chain_test.sh" ]; then
    echo "APP_RUNTIME_FORWARD action=STATUS from=$SCRIPTDIR to=/mnt/app/root/carplay-altscreen/bin"
    if [ "$#" -gt 0 ]; then exec /bin/sh "$APP_SELF" "$@"; else exec /bin/sh "$APP_SELF"; fi
fi

CONTROLLER="$SCRIPTDIR/altscreen_chain_test.sh"
[ -f "$CONTROLLER" ] || { echo "FAIL: installed chain controller missing"; exit 127; }
/bin/sh "$CONTROLLER" status
STATUS_RC=$?

RUNTIME="$DEVICE_ROOT/mnt/app/root/carplay-altscreen"
JAR="$DEVICE_ROOT/mnt/app/eso/hmi/lsd/jars/carplay_hook.jar"
ENABLED="$RUNTIME/state/basevideo3.enabled"
ACTIVE="$DEVICE_ROOT/tmp/mmi-mirror-active"
DEST_READY="$DEVICE_ROOT/tmp/mmi-mirror-basevideo.ready"
STARTED="$DEVICE_ROOT/tmp/mmi-mirror-controller.started"
JAVA_LOG="$DEVICE_ROOT/tmp/mmi-mirror-controller.log"
CTX_STATE="$DEVICE_ROOT/tmp/carplay_cluster.ctx"
RGI_HOOKS="$DEVICE_ROOT/mnt/app/root/hooks"
MIRROR="$RUNTIME/bin/mirror"
MIRROR_PID="$DEVICE_ROOT/tmp/MMI-Cockpit-Carplay.mirror.pid"
MIRROR_LOG="$DEVICE_ROOT/tmp/MMI-Cockpit-Carplay.mirror.log"
EXPECTED_SIZE=@CARPLAY_JAR_SIZE@
EXPECTED_CKSUM=@CARPLAY_JAR_CKSUM@

file_size(){ n=$(wc -c < "$1" 2>/dev/null) || { echo 0; return; }; set -- $n; echo "${1:-0}"; }
file_cksum(){ if command -v cksum >/dev/null 2>&1; then cksum < "$1" 2>/dev/null | awk '{print $1}'; else echo unavailable; fi; }

echo "=== CarPlay private111 Direct Display V2 ==="
echo "SOURCE_PATH=private111_ScreenStreamProcessData"
echo "H264_TAP=/carplay111_h264"
echo "DECODER_BACKEND=stock_omx_screen_linearized_shm decoded_shm=/carplay111_decoded"
echo "DESTINATION=displayable3_gles"
echo "CONTEXT_POLICY=JAVA_ONLY context=81 composite=98,101,102,3 nav_without_video=80 native_dmdt=0"
echo "WINDOW58_ENUMERATION=DISABLED sidecar_screen_read_window=0 hook_exact_stock_window_readback=1"

if [ -s "$JAR" ]; then
    SIZE=$(file_size "$JAR"); SUM=$(file_cksum "$JAR")
    if [ "$SIZE" = "$EXPECTED_SIZE" ] && { [ "$SUM" = unavailable ] || [ "$SUM" = "$EXPECTED_CKSUM" ]; }; then
        echo "HMI_CONTROL_PLANE=PASS size=$SIZE cksum=$SUM"
    else
        echo "HMI_CONTROL_PLANE=FAIL reason=identity_mismatch size=$SIZE cksum=$SUM"
        [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1
    fi
else
    echo "HMI_CONTROL_PLANE=FAIL reason=jar_missing"
    [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1
fi

[ -f "$ENABLED" ] && echo "DIRECT_DISPLAY_ENABLE=ENABLED" || echo "DIRECT_DISPLAY_ENABLE=DISABLED"
[ -f "$ACTIVE" ] && echo "JAVA80_DEMAND=YES" || echo "JAVA80_DEMAND=NO"

MIRROR_RUNNING=0
PID=""
if [ -f "$MIRROR_PID" ]; then
    PID=$(cat "$MIRROR_PID" 2>/dev/null || true)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then MIRROR_RUNNING=1; fi
fi
[ "$MIRROR_RUNNING" = 1 ] && echo "DIRECT_DISPLAY_SIDECAR=RUNNING pid=$PID" || echo "DIRECT_DISPLAY_SIDECAR=NOT_RUNNING"
[ -x "$MIRROR/carplay-alt111-mirror-display" ] && echo "DIRECT_DISPLAY_BINARY=INSTALLED" || echo "DIRECT_DISPLAY_BINARY=MISSING"
if [ -s "$MIRROR/logo.rgba" ]; then
    echo "SECOND_SCREEN_LOGO=INSTALLED"
else
    echo "SECOND_SCREEN_LOGO=MISSING"
    [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1
fi
if [ -s "$MIRROR/watermark.rgba" ]; then
    echo "SECOND_SCREEN_WATERMARK=INSTALLED asset=TRANSPARENT_DISABLED"
else
    echo "SECOND_SCREEN_WATERMARK=MISSING"
    [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1
fi

HOOK_LOG=""
for candidate in "$DEVICE_ROOT/tmp/MMI-Cockpit-Carplay.altscreen_hook.log" "$DEVICE_ROOT/tmp/altscreen_hook.log"; do
    [ -f "$candidate" ] && { HOOK_LOG=$candidate; break; }
done

H264_SHM=0
FRAME_SHM=0
SHM_RECOVERED=0
AVCC_PROPERTY=0
AVCC_CONFIG=0
H264_DATA=0
H264_SPS=0
H264_PPS=0
H264_IDR=0
FRAME_LAYOUT=0
FRAME_LAYOUT_UNSUPPORTED=0
DECODER_FRAME=0
MAP_FAILURE=0
if [ -n "$HOOK_LOG" ]; then
    grep -q 'PHASE=H264_TAP_SHM_READY' "$HOOK_LOG" 2>/dev/null && H264_SHM=1
    grep -q 'PHASE=FRAME_TAP_SHM_READY' "$HOOK_LOG" 2>/dev/null && FRAME_SHM=1
    grep -q 'PHASE=H264_TAP_SHM_RECOVERED\|PHASE=FRAME_TAP_SHM_RECOVERED' "$HOOK_LOG" 2>/dev/null && SHM_RECOVERED=1
    grep -q 'PHASE=H264_AVCC_PROPERTY' "$HOOK_LOG" 2>/dev/null && AVCC_PROPERTY=1
    grep -q 'PHASE=H264_AVCC_CONFIG' "$HOOK_LOG" 2>/dev/null && AVCC_CONFIG=1
    grep -q 'ERROR PHASE=H264_TAP_SHM_MAP\|ERROR PHASE=FRAME_TAP_SHM_MAP' "$HOOK_LOG" 2>/dev/null && MAP_FAILURE=1
    grep -q 'PHASE=H264_TAP_FIRST_DATA' "$HOOK_LOG" 2>/dev/null && H264_DATA=1
    grep -q 'PHASE=H264_TAP_FIRST_SPS' "$HOOK_LOG" 2>/dev/null && H264_SPS=1
    grep -q 'PHASE=H264_TAP_FIRST_PPS' "$HOOK_LOG" 2>/dev/null && H264_PPS=1
    grep -q 'PHASE=H264_TAP_FIRST_IDR' "$HOOK_LOG" 2>/dev/null && H264_IDR=1
    grep -q 'PHASE=FRAME_TAP_LAYOUT' "$HOOK_LOG" 2>/dev/null && FRAME_LAYOUT=1
    grep -q 'ERROR PHASE=FRAME_TAP_UNSUPPORTED_LAYOUT' "$HOOK_LOG" 2>/dev/null && FRAME_LAYOUT_UNSUPPORTED=1
    grep -q 'PHASE=DECODER_FIRST_FRAME backend=stock-omx-tap' "$HOOK_LOG" 2>/dev/null && DECODER_FRAME=1

    echo "SHM_WRITER_READY=h264:$H264_SHM decoded:$FRAME_SHM map_failure:$MAP_FAILURE recovered:$SHM_RECOVERED"
    echo "H264_AVCC_PROPERTY=$AVCC_PROPERTY H264_CODEC_CONFIG_EMITTED=$AVCC_CONFIG"
    echo "H264_TAP_DATA=$H264_DATA SPS=$H264_SPS PPS=$H264_PPS IDR=$H264_IDR"
    echo "FRAME_TAP_LAYOUT=$FRAME_LAYOUT unsupported:$FRAME_LAYOUT_UNSUPPORTED DECODER_FIRST_FRAME=$DECODER_FRAME backend=stock-omx-screen-linearized-shm"
    LINEARIZER_LAST="$(grep 'PHASE=FRAME_LINEARIZER_PROGRESS' "$HOOK_LOG" 2>/dev/null | tail -n 1 || true)"
    [ -z "$LINEARIZER_LAST" ] || echo "FRAME_LINEARIZER_LAST='$LINEARIZER_LAST'"
    SLOW_READBACKS="$(grep -c 'PHASE=FRAME_LINEARIZER_SLOW' "$HOOK_LOG" 2>/dev/null || true)"
    case "$SLOW_READBACKS" in ''|*[!0-9]*) SLOW_READBACKS=0 ;; esac
    echo "FRAME_LINEARIZER_SLOW_EVENTS=$SLOW_READBACKS threshold_us=20000"
    echo "HOOK_DIRECT111_LOG_TAIL_BEGIN"
    grep -E 'PHASE=(STREAM_111_|VIDEO_111_|H264_TAP_|H264_AVCC_|DIRECT111_TAP_|FRAME_TAP_|FRAME_LINEARIZER_|DECODER_)|ERROR PHASE=(H264_TAP_SHM_|FRAME_TAP_SHM_|FRAME_TAP_UNSUPPORTED_LAYOUT|FRAME_LINEARIZER_)' "$HOOK_LOG" 2>/dev/null | tail -n 120 || true
    echo "HOOK_DIRECT111_LOG_TAIL_END"
else
    echo "H264_TAP_DATA=UNKNOWN hook_log_missing=1"
    echo "DECODER_FIRST_FRAME=UNKNOWN hook_log_missing=1"
fi

H264_VALID=0
SIDECAR_DECODE=0
DISPLAY3=0
DIRECT_ACTIVE=0
if [ -f "$MIRROR_LOG" ]; then
    grep -q 'PHASE=H264_STREAM_VALID' "$MIRROR_LOG" 2>/dev/null && H264_VALID=1
    grep -q 'PHASE=DECODER_FIRST_FRAME' "$MIRROR_LOG" 2>/dev/null && SIDECAR_DECODE=1
    grep -q 'PHASE=DISPLAYABLE3_FIRST_PRESENT result=OK' "$MIRROR_LOG" 2>/dev/null && DISPLAY3=1
    grep -q 'PHASE=DIRECT111_ACTIVE' "$MIRROR_LOG" 2>/dev/null && DIRECT_ACTIVE=1

    echo "H264_STREAM_VALID=$H264_VALID"
    echo "SIDECAR_DECODER_FRAME=$SIDECAR_DECODE"
    echo "DISPLAYABLE3_FIRST_PRESENT=$DISPLAY3"
    echo "DIRECT111_ACTIVE=$DIRECT_ACTIVE"
    echo "DIRECT111_LOG_TAIL_BEGIN"
    tail -n 80 "$MIRROR_LOG" 2>/dev/null || true
    echo "DIRECT111_LOG_TAIL_END"
else
    echo "H264_STREAM_VALID=UNKNOWN mirror_log_missing=1"
    echo "SIDECAR_DECODER_FRAME=UNKNOWN mirror_log_missing=1"
    echo "DISPLAYABLE3_FIRST_PRESENT=UNKNOWN mirror_log_missing=1"
    echo "DIRECT111_ACTIVE=UNKNOWN mirror_log_missing=1"
fi

DEST=0
if [ -f "$DEST_READY" ]; then
    DEST=1
    echo "DEST_FRAME_READY=YES"
    cat "$DEST_READY" 2>/dev/null || true
else
    echo "DEST_FRAME_READY=NO"
fi

CTXREQ=0
CTXACT=0
# RGI Java (ScreenModule) publishes the last applied cluster context; ctx 81 carries
# displayable 3. The legacy AltScreen controller log below is kept for old JARs.
if [ -f "$CTX_STATE" ]; then
    echo "JAVA_CONTROLLER=RGI_SCREEN_MODULE"
    CTX_NOW=$(awk -F= '$1=="ctx"{print $2}' "$CTX_STATE" 2>/dev/null)
    echo "JAVA_CTX_STATE_BEGIN"; cat "$CTX_STATE" 2>/dev/null || true; echo "JAVA_CTX_STATE_END"
    if [ "$CTX_NOW" = 81 ]; then
        CTXREQ=1; CTXACT=1
        echo "JAVA_CTX81_ACTUAL=81"
    else
        echo "JAVA_CTX81_ACTUAL=NOT_ACTIVE ctx=${CTX_NOW:-unknown}"
    fi
elif [ -f "$JAVA_LOG" ]; then
    [ -f "$STARTED" ] && echo "JAVA_CONTROLLER=STARTED" || echo "JAVA_CONTROLLER=NOT_STARTED"
    if grep -q 'ownership acquire requested' "$JAVA_LOG" 2>/dev/null; then
        CTXREQ=1
        echo "JAVA_CTX80_REQUEST=YES"
    else
        echo "JAVA_CTX80_REQUEST=NO"
    fi
    if grep -q 'CTX80_OBSERVED actual=80' "$JAVA_LOG" 2>/dev/null; then
        CTXACT=1
        CTX_LINE=$(grep 'CTX80_OBSERVED actual=80' "$JAVA_LOG" 2>/dev/null | tail -n 1)
        echo "JAVA_CTX80_ACTUAL=80 proof='$CTX_LINE'"
    else
        echo "JAVA_CTX80_ACTUAL=NOT_OBSERVED desired=80"
    fi
    echo "JAVA80_LOG_TAIL_BEGIN"
    tail -n 40 "$JAVA_LOG" 2>/dev/null || true
    echo "JAVA80_LOG_TAIL_END"
else
    echo "JAVA_CONTROLLER=NOT_STARTED"
    echo "JAVA_CTX_REQUEST=UNKNOWN state_missing=1"
    echo "JAVA_CTX_ACTUAL=UNKNOWN state_missing=1"
fi

# mib2q-carplay-rgi companion: route guidance hook, maneuver renderer, supervisor.
RGI_MISSING=
for name in libcarplay_hook.so maneuver_render flag_atlas.rgba carplay_startup.sh carplay_monitor.sh carplay_processes.sh carplay_cleanup.sh; do
    [ -s "$RGI_HOOKS/$name" ] || RGI_MISSING="$RGI_MISSING $name"
done
if [ -z "$RGI_MISSING" ]; then echo "RGI_NATIVE=INSTALLED"; else echo "RGI_NATIVE=MISSING files:$RGI_MISSING"; [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1; fi
SI_JSON="$DEVICE_ROOT/mnt/system/etc/eso/production/smartphone_integrator.json"
grep -q '"exec": "carplay_startup.sh"' "$SI_JSON" 2>/dev/null && echo "RGI_SI_CHILD=WRAPPER" || { echo "RGI_SI_CHILD=NOT_WRAPPED"; [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1; }
DIO_JSON="$DEVICE_ROOT/mnt/system/etc/eso/production/dio_manager.json"
RGI_IDS=0; for id in 0x5200 0x5201 0x5202 0x5203 0x5204; do grep -q "\"$id\"" "$DIO_JSON" 2>/dev/null && RGI_IDS=$((RGI_IDS+1)); done
echo "RGI_DIO_IDS=$RGI_IDS/5"
[ "$RGI_IDS" = 5 ] || { [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1; }
# Both preloads must be mapped in the live dio_manager: AltScreen (video) and RGI (route guidance).
if command -v pidin >/dev/null 2>&1 && [ -z "$DEVICE_ROOT" ]; then
    DIO_LIBS=$(pidin -p dio_manager libs 2>/dev/null)
    echo "$DIO_LIBS" | grep -q libcarplay_altscreen.so && echo "DIO_PRELOAD_ALTSCREEN=LOADED" || { echo "DIO_PRELOAD_ALTSCREEN=NOT_LOADED"; [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1; }
    echo "$DIO_LIBS" | grep -q libcarplay_hook.so && echo "DIO_PRELOAD_RGI=LOADED" || { echo "DIO_PRELOAD_RGI=NOT_LOADED"; [ "$STATUS_RC" -ne 0 ] || STATUS_RC=1; }
else
    echo "DIO_PRELOAD=UNKNOWN pidin_unavailable"
fi
grep "\[startup\] preload" "$DEVICE_ROOT/tmp/carplay_wrapper.log" 2>/dev/null | tail -n 1
# Cluster video smoothness: encoder rate chosen in the GEM menu and the mirror's real output rate.
[ -f "$RGI_HOOKS/cluster_fps" ] && echo "CLUSTER_FPS_CHOICE=$(cat "$RGI_HOOKS/cluster_fps" 2>/dev/null)" || echo "CLUSTER_FPS_CHOICE=30 (default)"
MIRROR_RUN=$(grep "PHASE=RUN " "$MIRROR_LOG" 2>/dev/null | tail -n 1)
[ -n "$MIRROR_RUN" ] && echo "MIRROR_PRESENT_FPS=$(echo "$MIRROR_RUN" | sed -n 's/.*present_fps=\([0-9.]*\).*/\1/p') decoded_frames=$(echo "$MIRROR_RUN" | sed -n 's/.*decoded_frames=\([0-9]*\).*/\1/p') presented_frames=$(echo "$MIRROR_RUN" | sed -n 's/.*presented_frames=\([0-9]*\).*/\1/p')" || echo "MIRROR_PRESENT_FPS=UNKNOWN (no RUN line yet)"
[ -f "$DEVICE_ROOT/tmp/carplay_hook.log" ] && echo "RGI_HOOK_LOG=PRESENT" || echo "RGI_HOOK_LOG=ABSENT (hook not loaded in dio_manager this boot)"
RGI_RENDER_PID=$(cat "$DEVICE_ROOT/tmp/carplay_maneuver_render.pid" 2>/dev/null)
if [ -n "$RGI_RENDER_PID" ] && [ -d "$DEVICE_ROOT/proc/$RGI_RENDER_PID" ]; then echo "RGI_RENDERER=RUNNING pid=$RGI_RENDER_PID"; else echo "RGI_RENDERER=NOT_RUNNING (starts with a CarPlay session)"; fi

# V2 vehicle display readiness is driven by the Screen-linearized decoded path.
# H264 SPS/PPS/IDR evidence is reported independently for the future standalone
# decoder and must not falsely mark a working displayable3/Context80 route bad.
if [ "$DECODER_FRAME" = 1 ] && [ "$DISPLAY3" = 1 ] &&
   [ "$DEST" = 1 ] && [ "$MIRROR_RUNNING" = 1 ] &&
   [ "$CTXACT" = 1 ]; then
    echo "PHYSICAL_ROUTE_READY=SOFTWARE_CHAIN_COMPLETE decoder=1 displayable3=1 ctx81=1 human_vc_confirmation_required=YES"
    echo "COMPRESSED_PATH_EVIDENCE=h264_data:$H264_DATA avcc_property:$AVCC_PROPERTY config:$AVCC_CONFIG sps:$H264_SPS pps:$H264_PPS idr:$H264_IDR"
else
    echo "PHYSICAL_ROUTE_READY=NO decoded_shm=$FRAME_SHM frame_layout=$FRAME_LAYOUT layout_unsupported=$FRAME_LAYOUT_UNSUPPORTED decoder=$DECODER_FRAME displayable3=$DISPLAY3 destination=$DEST sidecar=$MIRROR_RUNNING ctx81_actual=$CTXACT"
    echo "COMPRESSED_PATH_EVIDENCE=h264_shm:$H264_SHM h264_data:$H264_DATA avcc_property:$AVCC_PROPERTY config:$AVCC_CONFIG sps:$H264_SPS pps:$H264_PPS idr:$H264_IDR"
fi

exit "$STATUS_RC"
