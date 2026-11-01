#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Day/night sync: on / off".
# on   (default) once the cluster video runs, the hook sends the phone the opposite day/night
#      mode and then the real one (twice), so Google Maps' cluster map takes the right theme
#      without toggling the lights (src/p1404_airplay.c, alt_night_sync_schedule).
# off  creates state/night-sync-off: nothing is sent (V3.8).
# Applies on the next phone connection.
set -u
mode=${1:-}
case "$mode" in
    on) TEXT= ;;
    off) TEXT=off ;;
    *) echo "usage: night_sync.sh on|off"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/night-sync-off"
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
    echo "NIGHT_SYNC=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "NIGHT_SYNC=FAILED requested=$mode reason=MOUNT_RW"
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
    result FAILED "Cannot save the day/night sync setting."
    echo "NIGHT_SYNC=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TEXT" ]; then
    result SUCCESS "Day/night sync: on." "Reconnect the iPhone to apply."
else
    result SUCCESS "Day/night sync: off." "Reconnect the iPhone to apply." \
        "A new INSTALL resets it to on."
fi
echo "NIGHT_SYNC=$mode applies=NEXT_CONNECTION"
exit 0
