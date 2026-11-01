#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Map top edge: normal / 90 / 105 / 120".
# Writes state/cluster-safe-top.txt; the hook reports that top edge of the
# cluster safeArea to the iPhone (src/p1404_airplay.c, alt_safe_top).
#
# The phone's maneuver card hangs from the safeArea's top edge. V3.8 puts it at
# y=72, where the stock VC top bar (range | media) overlaps the card; a lower
# edge gives the card a margin below the bar. The phone reads the safeArea at
# connection: reconnect the iPhone to apply.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    normal) TOP= ;;
    90) TOP=90 ;;
    105) TOP=105 ;;
    120) TOP=120 ;;
    *) echo "usage: cluster_safe_top.sh normal|90|105|120"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-safe-top.txt"
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
    echo "CLUSTER_SAFE_TOP=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_SAFE_TOP=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
if [ -z "$TOP" ]; then
    rm -f "$TARGET" "$TMP" 2>/dev/null || rc=1
    [ ! -e "$TARGET" ] || rc=1
else
    printf '%s\n' "$TOP" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
    rm -f "$TMP" 2>/dev/null || true
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
[ -z "$TOP" ] || { IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$TOP" ] || {
    result FAILED "Cannot save the map top edge."
    echo "CLUSTER_SAFE_TOP=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TOP" ]; then
    result SUCCESS "Map top edge: normal (72)." "Reconnect the iPhone to apply."
else
    result SUCCESS "Map top edge: $TOP." \
        "The card starts lower, below the" "top bar. Reconnect the iPhone." \
        "A new INSTALL resets it to normal."
fi
echo "CLUSTER_SAFE_TOP=$mode top=${TOP:-72} applies=NEXT_CONNECTION"
exit 0
