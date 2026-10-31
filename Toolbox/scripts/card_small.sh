#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Card in small view: keep / hide" (INSTALL WITH ... CARD).
# keep (default) the phone keeps its card screen in the SMALL view too, so VIEW only changes
#      the area it draws into and the map does not rebuild on every press.
# hide creates state/card-small-hide: SMALL switches to the map-only screen (V3.8), so the card
#      disappears there, but every VIEW press makes the phone rebuild the cluster UI.
# The hook re-reads it on the next VIEW change.
set -u
mode=${1:-}
case "$mode" in
    keep) TEXT= ;;
    hide) TEXT=hide ;;
    *) echo "usage: card_small.sh keep|hide"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/card-small-hide"
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
    echo "CARD_SMALL=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CARD_SMALL=FAILED requested=$mode reason=MOUNT_RW"
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
    result FAILED "Cannot save the card setting."
    echo "CARD_SMALL=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$TEXT" ]; then
    result SUCCESS "Card in small view: keep." "VIEW no longer rebuilds the map." "Applies on the next VIEW press."
else
    result SUCCESS "Card in small view: hide (V3.8)." "The map rebuilds on every VIEW press." \
        "Applies on the next VIEW press. A new INSTALL resets it."
fi
echo "CARD_SMALL=$mode applies=NEXT_VIEW"
exit 0
