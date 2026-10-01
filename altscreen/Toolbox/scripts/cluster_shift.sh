#!/bin/sh
# MMI-Cockpit-Carplay: move the CarPlay video plane (displayable 3) on the cluster.
#   cluster_shift.sh off|left120|left240|sporttest
# Writes /mnt/app/root/hooks/cluster_shift.cfg (full_dx / small_dx in pixels); the RGI
# Java re-reads it within a second and moves the plane, no reconnect needed.
#   left120 / left240  whole picture left in the full map view, for apps that draw the
#                      vehicle right of centre (Amap, Baidu)
#   sporttest          Classic only: in the small view (VIEW button) move the plane by
#                      -476 px as Sport does, to check the Sport placement on a Classic car
# Sport clusters get their -476 small-view offset from the stock layout without this file.
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
[ "$TESTING" = 1 ] && DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
CFG="$DEVICE_ROOT/mnt/app/root/hooks/cluster_shift.cfg"
case "${1:-}" in
    off)       TEXT= ;;
    left120)   TEXT="full_dx=-120" ;;
    left240)   TEXT="full_dx=-240" ;;
    sporttest) TEXT="small_dx=-476" ;;
    *) echo "usage: cluster_shift.sh off|left120|left240|sporttest"; exit 2 ;;
esac
[ -d "$(dirname -- "$CFG")" ] || { echo "FAIL: RGI is not installed (no /mnt/app/root/hooks)"; exit 1; }
[ "$TESTING" = 1 ] || mount -uw /mnt/app || { echo "FAIL: cannot mount /mnt/app writable"; exit 1; }
if [ -z "$TEXT" ]; then
    rm -f "$CFG"
    echo "CLUSTER_SHIFT=off"
else
    printf '%s\n' "$TEXT" > "$CFG.new" && mv -f "$CFG.new" "$CFG" || {
        rm -f "$CFG.new"; [ "$TESTING" = 1 ] || mount -ur /mnt/app; echo "FAIL: cannot write $CFG"; exit 1; }
    echo "CLUSTER_SHIFT=$1 $TEXT"
fi
sync
[ "$TESTING" = 1 ] || mount -ur /mnt/app
echo "Applied within a second while CarPlay is connected."
