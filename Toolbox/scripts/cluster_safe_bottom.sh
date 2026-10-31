#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Map bottom edge: normal / 420 / 400 / 380".
# Writes state/cluster-safe-bottom.txt; the hook reports that bottom edge of the
# cluster safeArea to the iPhone (src/p1404_airplay.c, alt_safe_height).
#
# Google Maps and Waze put the vehicle just above the safeArea's bottom edge. V3.8
# puts it at y=450, behind the stock lower info bar (road name | arrival time);
# a higher edge moves the vehicle (and the map's centre) up above the bar. The
# phone reads the safeArea at connection: reconnect the iPhone to apply.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    normal) BOTTOM= ;;
    420) BOTTOM=420 ;;
    400) BOTTOM=400 ;;
    380) BOTTOM=380 ;;
    *) echo "usage: cluster_safe_bottom.sh normal|420|400|380"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-safe-bottom.txt"
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
    echo "CLUSTER_SAFE_BOTTOM=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_SAFE_BOTTOM=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
if [ -z "$BOTTOM" ]; then
    rm -f "$TARGET" "$TMP" 2>/dev/null || rc=1
    [ ! -e "$TARGET" ] || rc=1
else
    printf '%s\n' "$BOTTOM" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
    rm -f "$TMP" 2>/dev/null || true
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
[ -z "$BOTTOM" ] || { IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$BOTTOM" ] || {
    result FAILED "Cannot save the map bottom edge."
    echo "CLUSTER_SAFE_BOTTOM=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$BOTTOM" ]; then
    result SUCCESS "Map bottom edge: normal (450)." "Reconnect the iPhone to apply."
else
    result SUCCESS "Map bottom edge: $BOTTOM." \
        "The vehicle sits higher, above the" "road name bar. Reconnect the iPhone." \
        "A new INSTALL resets it to normal."
fi
echo "CLUSTER_SAFE_BOTTOM=$mode bottom=${BOTTOM:-450} applies=NEXT_CONNECTION"
exit 0
