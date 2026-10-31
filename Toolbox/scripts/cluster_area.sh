#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Cluster map area: full / 1200 / 1140 / 1080 px".
# Writes state/cluster-view-width.txt; the hook reports that width to the iPhone
# as the cluster viewArea (src/p1404_airplay.c, alt_cluster_view_width).
#
# Amap and Baidu ignore the safeArea and lay out against the viewArea with the
# vehicle right of its centre; a narrower viewArea starting at the left edge
# brings it back towards the middle. Apple / Google Maps follow the safeArea,
# which does not move. Right of the viewArea the phone sends black. The phone
# reads the viewArea at connection: reconnect the iPhone to apply.
# The card profiles need at least 1140 px (their FULL safeArea ends at 1140);
# the hook ignores a narrower width there.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    full) WIDTH= ;;
    w1200) WIDTH=1200 ;;
    w1140) WIDTH=1140 ;;
    w1080) WIDTH=1080 ;;
    *) echo "usage: cluster_area.sh full|w1200|w1140|w1080"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-view-width.txt"
TMP="$TARGET.new"

result(){
    echo "=============================================="
    echo " RESULT: $1"
    shift
    for line in "$@"; do echo " $line"; done
    echo "=============================================="
}

[ -f "$RUNTIME/.mmi-cockpit-carplay-runtime-owner" ] &&
grep -Fxq 'owner=MMI-Cockpit-Carplay' "$RUNTIME/.mmi-cockpit-carplay-runtime-owner" 2>/dev/null &&
[ -d "$STATE" ] && [ ! -L "$STATE" ] || {
    result FAILED "MMI-Cockpit-Carplay is not installed." "Run one of the INSTALL entries first."
    echo "CLUSTER_AREA=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}
if [ -n "$WIDTH" ] && [ "$WIDTH" -lt 1140 ] && [ -f "$STATE/card.enabled" ]; then
    result FAILED "The phone card needs at least 1140 px." "Nothing was changed."
    echo "CLUSTER_AREA=REFUSED requested=$mode reason=CARD_NEEDS_1140"
    exit 1
fi

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_AREA=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
if [ -z "$WIDTH" ]; then
    rm -f "$TARGET" "$TMP" 2>/dev/null || rc=1
    [ ! -e "$TARGET" ] || rc=1
else
    printf '%s\n' "$WIDTH" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
    rm -f "$TMP" 2>/dev/null || true
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
[ -z "$WIDTH" ] || { IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$WIDTH" ] || {
    result FAILED "Cannot save the map area."
    echo "CLUSTER_AREA=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$WIDTH" ]; then
    result SUCCESS "Map area: full width (1440 px)." "Reconnect the iPhone to apply."
else
    result SUCCESS "Map area: $WIDTH px from the left edge." \
        "For apps that ignore the safe area" "(Amap, Baidu). Reconnect the iPhone." \
        "A new INSTALL resets it to full width."
fi
echo "CLUSTER_AREA=$mode width=${WIDTH:-1440} applies=NEXT_CONNECTION"
exit 0
