#!/bin/sh
# MMI-Cockpit-Carplay: choose how the iPhone lays out the CarPlay map on the cluster.
#   cluster_layout.sh default|top|right|noeta
# Writes /mnt/app/root/hooks/cluster_ui.url; the RGI Java sends it as showUI to the
# cluster display each time the AltScreen video comes up (reconnect the phone).
# "default" removes the file: AltScreen's own maps:/car/instrumentcluster/map stays.
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
[ "$TESTING" = 1 ] && DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
URL_FILE="$DEVICE_ROOT/mnt/app/root/hooks/cluster_ui.url"
BASE="maps:/car/instrumentcluster/map"
case "${1:-}" in
    default) URL= ;;
    top)     URL="$BASE?maneuverLayout=topaligned" ;;
    right)   URL="$BASE?maneuverLayout=rightaligned" ;;
    noeta)   URL="$BASE?showETA=no" ;;
    *) echo "usage: cluster_layout.sh default|top|right|noeta"; exit 2 ;;
esac
[ -d "$(dirname -- "$URL_FILE")" ] || { echo "FAIL: RGI is not installed (no /mnt/app/root/hooks)"; exit 1; }
[ "$TESTING" = 1 ] || mount -uw /mnt/app || { echo "FAIL: cannot mount /mnt/app writable"; exit 1; }
if [ -z "$URL" ]; then
    rm -f "$URL_FILE"
    echo "CLUSTER_LAYOUT=default (AltScreen maps:/car/instrumentcluster/map)"
else
    printf '%s\n' "$URL" > "$URL_FILE.new" && mv -f "$URL_FILE.new" "$URL_FILE" || {
        rm -f "$URL_FILE.new"; [ "$TESTING" = 1 ] || mount -ur /mnt/app; echo "FAIL: cannot write $URL_FILE"; exit 1; }
    echo "CLUSTER_LAYOUT=$1 url=$URL"
fi
sync
[ "$TESTING" = 1 ] || mount -ur /mnt/app
echo "Reconnect the iPhone to apply."
