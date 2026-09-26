#!/bin/sh
# MMI-Cockpit-Carplay: cluster encoder rate while the CarPlay video is on the VC.
#   cluster_fps.sh 30|60
# Writes /mnt/app/root/hooks/cluster_fps; the RGI Java applies it when the cluster enters
# the AltScreen video context (reconnect the phone). 30 is the tested default; 60 is an
# experiment (the mirror's stock renderer shows every second cluster tick).
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
[ "$TESTING" = 1 ] && DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
FPS_FILE="$DEVICE_ROOT/mnt/app/root/hooks/cluster_fps"
case "${1:-}" in
    30|60) FPS=$1 ;;
    *) echo "usage: cluster_fps.sh 30|60"; exit 2 ;;
esac
[ -d "$(dirname -- "$FPS_FILE")" ] || { echo "FAIL: RGI is not installed (no /mnt/app/root/hooks)"; exit 1; }
[ "$TESTING" = 1 ] || mount -uw /mnt/app || { echo "FAIL: cannot mount /mnt/app writable"; exit 1; }
if [ "$FPS" = 30 ]; then
    rm -f "$FPS_FILE"
else
    printf '%s\n' "$FPS" > "$FPS_FILE.new" && mv -f "$FPS_FILE.new" "$FPS_FILE" || {
        rm -f "$FPS_FILE.new"; [ "$TESTING" = 1 ] || mount -ur /mnt/app; echo "FAIL: cannot write $FPS_FILE"; exit 1; }
fi
sync
[ "$TESTING" = 1 ] || mount -ur /mnt/app
echo "CLUSTER_FPS=$FPS"
echo "Reconnect the iPhone to apply. STATUS shows encoder_fps and the mirror present_fps."
