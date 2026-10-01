#!/bin/sh
# Thin smartphone_integrator child wrapper.
# Publish this generation, start the renderer monitor, then immediately become
# dio_manager so SI retains exact PID/watchdog ownership.

DIODIR=${DIODIR:-/mnt/app/eso/bin/apps}
H=${H:-/mnt/app/root/hooks}
WLOG=${WLOG:-/tmp/carplay_wrapper.log}
OWNER_FILE=${OWNER_FILE:-/tmp/carplay_supervisor.owner}
DIO_PID=$$
MONITOR_PID=

startup_fail()
{
    echo "[startup] abort pid=$DIO_PID" >> "$WLOG"
    [ -n "$MONITOR_PID" ] && kill -15 "$MONITOR_PID" 2>/dev/null
    exit 127
}
trap 'startup_fail' 1 2 15

[ -x "$DIODIR/dio_manager" ] || {
    echo "[startup] missing $DIODIR/dio_manager" >> "$WLOG"
    exit 127
}
[ -r "$H/libcarplay_hook.so" ] || {
    echo "[startup] missing $H/libcarplay_hook.so" >> "$WLOG"
    exit 127
}
[ -x "$H/carplay_monitor.sh" ] || {
    echo "[startup] missing $H/carplay_monitor.sh" >> "$WLOG"
    exit 127
}
[ -r "$H/carplay_processes.sh" ] || {
    echo "[startup] missing $H/carplay_processes.sh" >> "$WLOG"
    exit 127
}
cd "$DIODIR" 2>/dev/null || startup_fail

# Never expose a partially-written owner file to an older monitor.
OWNER_STAGE=${OWNER_FILE}.${DIO_PID}
echo "$DIO_PID" > "$OWNER_STAGE" || startup_fail
mv "$OWNER_STAGE" "$OWNER_FILE" || startup_fail
echo "===== CarPlay generation pid=$DIO_PID ppid=${PPID:-unknown} =====" >> "$WLOG"

LD_PRELOAD= "$H/carplay_monitor.sh" "$DIO_PID" </dev/null >>"$WLOG" 2>&1 &
MONITOR_PID=$!

# Only dio_manager receives the hook. The monitor and the renderer explicitly
# clear LD_PRELOAD. Ours goes first, so its AirPlayReceiverSessionScreen_CopyDisplaysInfo
# wraps AltScreen's (cluster view area); the others interpose disjoint symbols. Then the
# other preloads for dio_manager (the AltScreen universal hook): CARPLAY_PRELOAD_EXTRA
# from the child env, then whatever LD_PRELOAD still holds here - a preload loaded into
# this shell may already have removed itself from it. Each entry once.
INHERITED_PRELOAD=${LD_PRELOAD:-}
PRELOAD_LIST=
OLD_IFS=$IFS
IFS=:
for lib in $H/libcarplay_hook.so ${CARPLAY_PRELOAD_EXTRA:-} $INHERITED_PRELOAD; do
    [ -n "$lib" ] || continue
    case ":$PRELOAD_LIST:" in
        *":$lib:"*) ;;
        *) PRELOAD_LIST=${PRELOAD_LIST:+$PRELOAD_LIST:}$lib ;;
    esac
done
IFS=$OLD_IFS
LD_PRELOAD=$PRELOAD_LIST
export LD_PRELOAD
echo "[startup] preload inherited='${INHERITED_PRELOAD}' extra='${CARPLAY_PRELOAD_EXTRA:-}' -> '$LD_PRELOAD'" >> "$WLOG"

echo "[startup] exec dio_manager pid=$DIO_PID monitor=$MONITOR_PID" >> "$WLOG"
exec "$DIODIR/dio_manager" "$@"

# Preflight makes this reachable only for an exec/runtime loader failure.
STARTUP_EXEC_RC=$?
echo "[startup] exec dio_manager failed rc=$STARTUP_EXEC_RC" >> "$WLOG"
startup_fail
