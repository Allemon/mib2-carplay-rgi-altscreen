#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Map ETA: on / off".
# on   creates state/map-eta-on: the hook asks the phone for the cluster map with its own
#      arrival panel (showETA=yes: arrival time, minutes, distance), as on the MMI.
# off  (default, V3.8) showETA=no; the arrival time is in the VC's lower info bar only.
# Applies on the next phone connection.
set -u
mode=${1:-}
case "$mode" in
    off) TEXT= ;;
    on) TEXT=on ;;
    *) echo "usage: map_eta.sh on|off"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/map-eta-on"
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
    echo "MAP_ETA=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "MAP_ETA=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
if [ -z "$TEXT" ]; then
    rm -f "$TARGET" "$TMP" 2>/dev/null || rc=1
    [ ! -e "$TARGET" ] || rc=1
else
    printf '%s\n' "$TEXT" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
    rm -f "$TMP" 2>/dev/null || true
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
[ -z "$TEXT" ] || { IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$TEXT" ] || {
    result FAILED "Cannot save the map ETA setting."
    echo "MAP_ETA=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TEXT" ]; then
    result SUCCESS "Map ETA: off (V3.8)." "Reconnect the iPhone to apply."
else
    result SUCCESS "Map ETA: on." "Arrival time, minutes and distance" "on the map. Reconnect the iPhone." \
        "A new INSTALL resets it to off."
fi
echo "MAP_ETA=$mode applies=NEXT_CONNECTION"
exit 0
