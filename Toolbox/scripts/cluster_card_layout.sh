#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM: CARD LEFT / CARD TOP / CARD RIGHT. Selects where the iPhone draws its
# maneuver card (INSTALL WITH CARD / INSTALL WITH RGI CARD). The hook re-reads
# the choice every 100 ms, so with CarPlay connected in the FULL cluster view
# the card moves on the same stream without a reconnect.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    left) MODE_UPPER=LEFT ;;
    top) MODE_UPPER=TOP ;;
    right) MODE_UPPER=RIGHT ;;
    *) echo "usage: cluster_card_layout.sh left|top|right"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-card-layout.txt"
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
    result FAILED "MMI-Cockpit-Carplay is not installed." "Run INSTALL WITH CARD or INSTALL WITH RGI CARD first."
    echo "CLUSTER_CARD_LAYOUT=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}
# INSTALL rebuilds the runtime (default TOP), so a position saved without the
# card profile would never be used.
[ -f "$STATE/card.enabled" ] || {
    result FAILED "The phone card is not enabled in the current" \
        "install mode. Nothing was changed." \
        "Run INSTALL WITH CARD or INSTALL WITH RGI CARD first."
    echo "CLUSTER_CARD_LAYOUT=REFUSED requested=$mode reason=CARD_NOT_ENABLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_CARD_LAYOUT=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
printf '%s\n' "$mode" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
rm -f "$TMP" 2>/dev/null || true
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
{ IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$mode" ] || {
    result FAILED "Cannot save the card position."
    echo "CLUSTER_CARD_LAYOUT=FAILED requested=$mode reason=WRITE"
    exit 1
}

result SUCCESS "Card position: $MODE_UPPER." \
    "Applies right away in the FULL cluster view" "while CarPlay is connected." \
    "A new INSTALL resets it to TOP."
echo "CLUSTER_CARD_LAYOUT=$mode card_enabled=1 applies=NEXT_SHOWUI_SAME_STREAM"
exit 0
