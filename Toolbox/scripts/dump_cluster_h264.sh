#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Dump cluster video to SD (diagnostic)".
# The hook keeps the last few MB of received cluster video (stream 111) in the
# shared-memory ring /carplay111_h264 (P111_H264_SHM_NAME). Run this while the
# cluster shows the moving CarPlay map; the copy lands on the SD in
# MMI-Cockpit-Carplay/logs/h264/ with the map display's last RUN line next to it.
# Analyse on a computer: python3 Tools/h264_ring_analyze.py <file>
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    VOLUME=${ALTSCREEN_CHAIN_VOLUME:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
    RING=${ALTSCREEN_H264_RING:-$ROOT/dev/shmem/carplay111_h264}
else
    RING=/dev/shmem/carplay111_h264
    VOLUME=""
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -d "$candidate/Toolbox" ]; then VOLUME=$candidate; break; fi
    done
fi

result(){
    echo "=============================================="
    echo " RESULT: $1"
    shift
    for line in "$@"; do echo " $line"; done
    echo "=============================================="
}

[ -n "$VOLUME" ] && [ -d "$VOLUME" ] || {
    result FAILED "No SD card with a Toolbox folder was found."
    echo "H264_DUMP=FAILED reason=NO_SD"
    exit 1
}
[ -s "$RING" ] || {
    result FAILED "No cluster video yet." "Connect CarPlay with the map on the cluster" "and try again."
    echo "H264_DUMP=FAILED reason=NO_RING ring=$RING"
    exit 1
}

OUT_DIR="$VOLUME/MMI-Cockpit-Carplay/logs/h264"
mkdir -p "$OUT_DIR" 2>/dev/null || {
    result FAILED "Cannot create MMI-Cockpit-Carplay/logs/h264 on the SD."
    echo "H264_DUMP=FAILED reason=MKDIR"
    exit 1
}
n=1
while [ -e "$OUT_DIR/cluster_h264_$n.bin" ]; do n=$((n + 1)); done
OUT="$OUT_DIR/cluster_h264_$n.bin"

# The writer keeps running; a torn record at the ring head is skipped by the analyser.
cat "$RING" > "$OUT.part" 2>/dev/null && mv -f "$OUT.part" "$OUT" || {
    rm -f "$OUT.part" 2>/dev/null || true
    result FAILED "Copying the video ring failed."
    echo "H264_DUMP=FAILED reason=COPY"
    exit 1
}
for log in "$ROOT/tmp/MMI-Cockpit-Carplay/mirror/mirror.log" "$ROOT/tmp/altscreen_mirror.log"; do
    [ -f "$log" ] || continue
    grep "PHASE=RUN " "$log" 2>/dev/null | tail -n 1 > "$OUT_DIR/cluster_h264_$n.mirror.txt"
    break
done
sync
bytes=$(wc -c < "$OUT" | tr -d ' ')
result SUCCESS "Saved MMI-Cockpit-Carplay/logs/h264/" "cluster_h264_$n.bin ($bytes bytes)."
echo "H264_DUMP=OK file=MMI-Cockpit-Carplay/logs/h264/cluster_h264_$n.bin bytes=$bytes"
exit 0
