#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "Verbose Java logs: on / off".
# Creates or removes /mnt/app/carplay_verbose. The Java HMI (framework/Log.java)
# reads it at start and at every CarPlay session start and then also writes INFO
# to /tmp/carplay_hook.log; the boot logs copy it to the SD. The native RGI hook
# is already verbose while the runtime is installed (state/diagnostics.enabled).
# RESTORE ORIGINAL removes the marker.
set -u
case "${1:-}" in on|off) ;; *) echo "usage: rgi_verbose.sh on|off"; exit 2 ;; esac

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
MARK="$ROOT/mnt/app/carplay_verbose"

result(){
    echo "=============================================="
    echo " RESULT: $1"
    shift
    for line in "$@"; do echo " $line"; done
    echo "=============================================="
}

[ "$TESTING" = 1 ] || mount -uw /mnt/app >/dev/null 2>&1 || {
    result FAILED "Cannot make /mnt/app writable. Nothing was changed."
    echo "VERBOSE_LOGS=FAILED requested=$1 reason=MOUNT_RW"
    exit 1
}
rc=0
if [ "$1" = on ]; then
    : > "$MARK" || rc=1
    [ -f "$MARK" ] || rc=1
else
    rm -f "$MARK" 2>/dev/null || rc=1
    [ ! -e "$MARK" ] || rc=1
fi
[ "$rc" -ne 0 ] || sync || rc=1
[ "$TESTING" = 1 ] || mount -ur /mnt/app >/dev/null 2>&1 || true
[ "$rc" -eq 0 ] || {
    result FAILED "Cannot change $MARK."
    echo "VERBOSE_LOGS=FAILED requested=$1 reason=WRITE"
    exit 1
}
if [ "$1" = on ]; then
    result SUCCESS "Verbose Java logs: on." "Reconnect the iPhone to apply."
else
    result SUCCESS "Verbose Java logs: off." "Reconnect the iPhone to apply."
fi
echo "VERBOSE_LOGS=$1 marker=/mnt/app/carplay_verbose"
exit 0
