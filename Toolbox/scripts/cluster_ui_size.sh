#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Cluster UI size: V3.8 / compact".
# Writes state/cluster-physical-width.txt; the hook reports that panel width (mm)
# to the iPhone (src/p1404_airplay.c, alt_build_cluster_display).
#
# iOS picks the UI scale of the cluster display from pixels per mm. V3.8 reports
# LIVI's 200 mm (183 ppi), so the maneuver card, labels and buttons come out large
# (the card is tall and narrow and reaches under the dials). "compact" reports the
# real 12.3-inch VC, 292 mm (125 ppi, close to the MMI): the same elements drawn as
# on the MMI, smaller and wider. Reconnect the iPhone to apply.
#
# The file lives on /mnt/app (not the flat /tmp), so write-then-rename is safe.
set -u
mode=${1:-}
case "$mode" in
    v38) WIDTH_MM= ;;
    compact) WIDTH_MM=292 ;;
    *) echo "usage: cluster_ui_size.sh v38|compact"; exit 2 ;;
esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
RUNTIME="$ROOT/mnt/app/root/carplay-altscreen"
STATE="$RUNTIME/state"
TARGET="$STATE/cluster-physical-width.txt"
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
    echo "CLUSTER_UI_SIZE=REFUSED requested=$mode reason=RUNTIME_NOT_INSTALLED"
    exit 1
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "CLUSTER_UI_SIZE=FAILED requested=$mode reason=MOUNT_RW"
    exit 1
}
rc=0
if [ -z "$WIDTH_MM" ]; then
    rm -f "$TARGET" "$TMP" 2>/dev/null || rc=1
    [ ! -e "$TARGET" ] || rc=1
else
    printf '%s\n' "$WIDTH_MM" > "$TMP" && chmod 644 "$TMP" && mv -f "$TMP" "$TARGET" || rc=1
    rm -f "$TMP" 2>/dev/null || true
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
saved=""
[ -z "$WIDTH_MM" ] || { IFS= read -r saved < "$TARGET"; } 2>/dev/null || true
[ "$rc" -eq 0 ] && [ "$saved" = "$WIDTH_MM" ] || {
    result FAILED "Cannot save the cluster UI size."
    echo "CLUSTER_UI_SIZE=FAILED requested=$mode reason=WRITE"
    exit 1
}

if [ -z "$WIDTH_MM" ]; then
    result SUCCESS "Cluster UI size: V3.8 (200 mm)." "Reconnect the iPhone to apply."
else
    result SUCCESS "Cluster UI size: compact (292 mm)." \
        "Card and labels drawn as on the MMI." "Reconnect the iPhone." \
        "A new INSTALL resets it to V3.8."
fi
echo "CLUSTER_UI_SIZE=$mode width_mm=${WIDTH_MM:-200} applies=NEXT_CONNECTION"
exit 0
