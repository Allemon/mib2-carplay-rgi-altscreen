#!/bin/sh
# MMI-Cockpit-Carplay: RGI verbose logging (hook + Java at INFO) for bug reports.
#   rgi_verbose.sh on|off
# Creates / removes /mnt/app/carplay_verbose; hook and Java read it at every CarPlay session
# start, so reconnect the phone. The boot diagnostics copy /tmp/carplay_hook.log and
# /tmp/carplay_java.log to MMI-Cockpit-Carplay/logs/boots/<boot>/streams/ (hmi_hook, rgi_java).
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
[ "$TESTING" = 1 ] && DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
MARK="$DEVICE_ROOT/mnt/app/carplay_verbose"
case "${1:-}" in on|off) ;; *) echo "usage: rgi_verbose.sh on|off"; exit 2 ;; esac
[ "$TESTING" = 1 ] || mount -uw /mnt/app || { echo "FAIL: cannot mount /mnt/app writable"; exit 1; }
if [ "$1" = on ]; then : > "$MARK"; else rm -f "$MARK"; fi
rc=$?
sync
[ "$TESTING" = 1 ] || mount -ur /mnt/app
[ "$rc" = 0 ] || { echo "FAIL: cannot change $MARK"; exit 1; }
echo "RGI_VERBOSE=$1 (reconnect the iPhone)"
