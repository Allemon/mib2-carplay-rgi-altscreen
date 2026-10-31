#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Cluster map shift: off / Sport test".
# Writes state/cluster-shift.txt (full_dx= / small_dx=, pixels); the map display
# adds it to the OEM stage offset of the current view
# (mirror_display/src/cluster_shift.h) and re-reads it every 50 ms, so it moves
# the map while CarPlay is connected.
#   sporttest   small_dx=-476: on a Classic car the SMALL view (VIEW button)
#               shows the map where Sport puts it. Sport clusters already get
#               their -476 from the stock layout; do not use this on Sport.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    off) TEXT= ;;
    sporttest) TEXT="small_dx=-476" ;;
    *) echo "usage: cluster_shift.sh off|sporttest"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-shift.txt"
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
    echo "CLUSTER_SHIFT=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_SHIFT=FAILED requested=$mode reason=MOUNT_RW"
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
    result FAILED "Cannot save the map shift."
    echo "CLUSTER_SHIFT=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TEXT" ]; then
    result SUCCESS "Map shift: off (stock placement)." "Applies right away."
else
    result SUCCESS "Map shift: Sport test." "SMALL view: map moved 476 px left." \
        "Applies right away. A new INSTALL resets it."
fi
echo "CLUSTER_SHIFT=$mode ${TEXT:-none} applies=LIVE"
exit 0
