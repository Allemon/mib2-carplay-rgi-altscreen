#!/bin/sh
# MMI-Cockpit-Carplay: the area the iPhone draws the CarPlay map into on the cluster.
#   cluster_area.sh full|w1200|w1140|w1080
# Writes /mnt/app/root/hooks/cluster_viewarea.cfg; the RGI hook reports it to the iPhone
# as the cluster display's view area (reconnect the phone to apply).
# Apps that ignore the safe area (Amap, Baidu) place the vehicle relative to the view area,
# right of its centre: a narrower view area, starting at the left edge, brings it back to
# the middle of the cluster.  The safe area stays centred on the cluster (x=720), so Apple /
# Google Maps keep the vehicle in the middle.  Right of the view area the video is black
# (mostly behind the maneuver panel and the speedometer).
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
[ "$TESTING" = 1 ] && DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
CFG="$DEVICE_ROOT/mnt/app/root/hooks/cluster_viewarea.cfg"
case "${1:-}" in
    full)  W= ;;
    w1200) W=1200 ;;
    w1140) W=1140 ;;
    w1080) W=1080 ;;
    *) echo "usage: cluster_area.sh full|w1200|w1140|w1080"; exit 2 ;;
esac
[ -d "$(dirname -- "$CFG")" ] || { echo "FAIL: RGI is not installed (no /mnt/app/root/hooks)"; exit 1; }
[ "$TESTING" = 1 ] || mount -uw /mnt/app || { echo "FAIL: cannot mount /mnt/app writable"; exit 1; }
if [ -z "$W" ]; then
    rm -f "$CFG"
    echo "CLUSTER_AREA=full (AltScreen 1440x542)"
else
    SAFE_X=$((1440 - W))          # safe area [1440-W, W]: centred on x=720
    SAFE_W=$((2 * W - 1440))
    printf 'view 0 0 %s 542\nsafe %s 0 %s 542\n' "$W" "$SAFE_X" "$SAFE_W" > "$CFG.new" && mv -f "$CFG.new" "$CFG" || {
        rm -f "$CFG.new"; [ "$TESTING" = 1 ] || mount -ur /mnt/app; echo "FAIL: cannot write $CFG"; exit 1; }
    echo "CLUSTER_AREA=$1 view=0,0,${W}x542 safe=$SAFE_X,0,${SAFE_W}x542"
fi
sync
[ "$TESTING" = 1 ] || mount -ur /mnt/app
echo "Reconnect the iPhone to apply."
