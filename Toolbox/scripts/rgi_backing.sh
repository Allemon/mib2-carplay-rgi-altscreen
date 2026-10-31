#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "RGI backing: on / off".
# on   (default) the stock KDK backing (displayable 101 / 102) sits under the route arrow and
#      fades with it, as before V3.8 Fix1: the map does not show through the maneuver panel.
# off  creates state/rgi-backing-off: the arrow sits directly on the map (V3.8 Fix1).
# ClusterLayerController re-reads it within 2 s, so it applies while CarPlay is connected.
set -u
mode=${1:-}
case "$mode" in
    on) TEXT= ;;
    off) TEXT=off ;;
    *) echo "usage: rgi_backing.sh on|off"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/rgi-backing-off"
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
    echo "RGI_BACKING=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "RGI_BACKING=FAILED requested=$mode reason=MOUNT_RW"
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
    result FAILED "Cannot save the RGI backing setting."
    echo "RGI_BACKING=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TEXT" ]; then
    result SUCCESS "RGI backing: on." "The arrow gets its dark panel back." "Applies within 2 seconds."
else
    result SUCCESS "RGI backing: off." "The arrow sits directly on the map." \
        "Applies within 2 seconds. A new INSTALL resets it."
fi
echo "RGI_BACKING=$mode applies=LIVE"
exit 0
