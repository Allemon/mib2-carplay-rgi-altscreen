#!/bin/sh
# MMI-Cockpit-Carplay diagnostic: save the CarPlay cluster (stream 111) H.264 ring.
# AltScreen keeps the last ~4 MB of received cluster video packets in the shared-memory
# ring /carplay111_h264 (~15-20 s). Run this while the cluster shows the CarPlay map and
# the map is moving; the copy lands on the SD in MMI-Cockpit-Carplay/logs/h264/.
# Analyse on a computer: python3 tools/h264_ring_analyze.py <file>
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
if [ "$TESTING" = 1 ]; then
    RING=${ALTSCREEN_H264_RING:-}
    VOLUME=${ALTSCREEN_CHAIN_VOLUME:-}
else
    RING=/dev/shmem/carplay111_h264
    VOLUME=""
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        [ -d "$candidate/MMI-Cockpit-Carplay" ] && { VOLUME=$candidate; break; }
    done
fi
[ -n "$VOLUME" ] || { echo "FAIL: no SD card with MMI-Cockpit-Carplay/"; exit 1; }
[ -s "$RING" ] || { echo "FAIL: $RING missing - start CarPlay with the cluster video first"; exit 1; }

OUT_DIR="$VOLUME/MMI-Cockpit-Carplay/logs/h264"
mkdir -p "$OUT_DIR" 2>/dev/null || { mount -uw "$VOLUME" 2>/dev/null; mkdir -p "$OUT_DIR"; } || {
    echo "FAIL: cannot create $OUT_DIR"; exit 1; }
n=1
while [ -e "$OUT_DIR/cluster_h264_$n.bin" ]; do n=$((n + 1)); done
OUT="$OUT_DIR/cluster_h264_$n.bin"

# The writer keeps running; a torn record at the ring head is skipped by the analyser.
cat "$RING" > "$OUT.part" 2>/dev/null && mv -f "$OUT.part" "$OUT" || {
    rm -f "$OUT.part"; echo "FAIL: copy of $RING failed"; exit 1; }
sync
echo "H264_DUMP=OK file=MMI-Cockpit-Carplay/logs/h264/cluster_h264_$n.bin bytes=$(wc -c < "$OUT" | tr -d ' ')"
grep "PHASE=RUN " /tmp/MMI-Cockpit-Carplay.mirror.log 2>/dev/null | tail -n 1 > "$OUT_DIR/cluster_h264_$n.mirror.txt"
