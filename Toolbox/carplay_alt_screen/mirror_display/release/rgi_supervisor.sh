#!/bin/sh
# Allemon renderer supervision; election is a kernel-owned loopback lease.
set -eu
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/bin:/usr/bin:/mnt/app/armle/bin:/eso/bin
export PATH
HERE=$(CDPATH= cd "${0%/*}" && pwd) || exit 2
TMP_ROOT=${ALT111_MIRROR_TMP_ROOT:-/tmp}
BIN=${RGI_RENDERER_BIN:-$HERE/../rgi/maneuver_render}
PIDFILE=$TMP_ROOT/altscreen_rgi_supervisor.pid
CHILDFILE=$TMP_ROOT/altscreen_rgi_renderer.pid
LOG=$TMP_ROOT/maneuver_render.log
DISABLED=$TMP_ROOT/mmi-rgi.disabled
PERSISTENT_DISABLED=$HERE/../../state/rgi.disabled
STOP=$TMP_ROOT/altscreen_mirror.stop.requested
MAX_RESTARTS=${RGI_MAX_RESTARTS:-3}
if [ -d /proc/boot ] && [ -d /mnt/app ]; then
  LD_LIBRARY_PATH=${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}/proc/boot:/usr/lib:/armle/lib:/armle/lib/dll:/lib:/mnt/app/root/carplay-altscreen/lib:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib:/lib/dll
  export LD_LIBRARY_PATH
fi
log() {
  log_message=$*
  log_size=$(wc -c < "$LOG" 2>/dev/null) || log_size=0
  set -- $log_size
  log_size=${1:-0}
  case "$log_size" in ''|*[!0-9]*) log_size=0 ;; esac
  if [ "$log_size" -ge 1048576 ]; then printf '' > "$LOG" 2>/dev/null || return 0; fi
  printf '%s\n' "$log_message" >> "$LOG"; }
if ! printf '' >> "$LOG"; then
  echo "RGI_SUPERVISOR=FAIL stage=log_open path=$LOG" >&2
  exit 2
fi
case "$MAX_RESTARTS" in ''|*[!0-9]*) log "RGI_SUPERVISOR=FAIL reason=invalid_restart_limit"; exit 2 ;; esac
if [ -f "$PERSISTENT_DISABLED" ] || [ -f "$DISABLED" ]; then
  log "RGI_SUPERVISOR=DISABLED install_mode=NO"
  exit 0
fi
[ -x "$BIN" ] || { log "RGI_SUPERVISOR=FAIL reason=missing_renderer path=$BIN"; exit 2; }
if [ "${1:-}" != --lease-held ]; then
  log "RGI_SUPERVISOR=ATTEMPT renderer=$BIN lease=kernel_socket no_tmp_directory=1"
  # The native election execs this shell with the SAME PID and retained fd 9.
  # Both shell and renderer keep the lease; crashes cannot leave a stale lock.
  LD_PRELOAD= exec "$BIN" --supervisor "$0" >> "$LOG" 2>&1
fi
exec 9>&9 || { log "RGI_SUPERVISOR=FAIL reason=lease_fd_missing"; exit 2; }
OWNER=${RGI_SUPERVISOR_OWNER:-}
case "$OWNER" in ''|*[!0-9]*) log "RGI_SUPERVISOR=FAIL reason=lease_owner_missing"; exit 2 ;; esac
child=""
cleanup() {
  trap - 0 1 2 15
  if [ -n "$child" ]; then
    kill -TERM "$child" 2>/dev/null || true
    n=0
    while kill -0 "$child" 2>/dev/null && [ "$n" -lt 2 ]; do sleep 1; n=$((n+1)); done
    if kill -0 "$child" 2>/dev/null; then kill -KILL "$child" 2>/dev/null || true; fi
    wait "$child" 2>/dev/null || true
  fi
  if [ "$(cat "$PIDFILE" 2>/dev/null || true)" = "$OWNER" ]; then
    rm -f "$PIDFILE" "$CHILDFILE"
  fi
  log "RGI_SUPERVISOR=STOPPED owner=$OWNER"
}
trap cleanup 0
trap 'exit 0' 1 2 15
printf '%s\n' "$OWNER" > "$PIDFILE" || { log "RGI_SUPERVISOR=FAIL stage=pid_publish"; exit 2; }
failures=0
epoch_seconds() {
  value=$(LD_PRELOAD= "$BIN" --monotonic-seconds 9>&- 2>/dev/null) || value=""
  case "$value" in
    ''|*[!0-9]*) ;;
    *) printf 'm:%s\n' "$value"; return 0 ;;
  esac
  value=$(date +%s 2>/dev/null) || value=""
  case "$value" in ''|*[!0-9]*) return 1 ;; esac
  printf 'd:%s\n' "$value"
}
log "RGI_SUPERVISOR=STARTED owner=$OWNER restart_limit=$MAX_RESTARTS"
while [ ! -f "$PERSISTENT_DISABLED" ] && [ ! -f "$DISABLED" ] && [ ! -f "$STOP" ]; do
  started=$(epoch_seconds) || started=""
  (cd "${BIN%/*}" && LD_PRELOAD= exec "$BIN") >> "$LOG" 2>&1 &
  child=$!
  printf '%s\n' "$child" > "$CHILDFILE" || { log "RGI_SUPERVISOR=FAIL stage=child_pid_publish"; exit 2; }
  rc=0
  wait "$child" || rc=$?
  child=""
  rm -f "$CHILDFILE"
  [ ! -f "$PERSISTENT_DISABLED" ] && [ ! -f "$DISABLED" ] && [ ! -f "$STOP" ] || break
  ended=$(epoch_seconds) || ended=""
  elapsed=unknown
  if [ -n "$started" ] && [ -n "$ended" ] && [ "${started%%:*}" = "${ended%%:*}" ] && [ "${ended#*:}" -ge "${started#*:}" ]; then
    elapsed=$((${ended#*:}-${started#*:}))
    [ "$elapsed" -lt 60 ] || failures=0
  fi
  failures=$((failures+1))
  log "RGI_RENDERER_EXIT rc=$rc uptime=$elapsed consecutive=$failures"
  if [ "$failures" -gt "$MAX_RESTARTS" ]; then
    # Screen/graphics may become ready long after early startup. Keep the
    # kernel lease, then retry slowly instead of abandoning this whole boot.
    log "RGI_SUPERVISOR=RESTART_LIMIT action=COOLDOWN_RETRY seconds=60"
    cooldown=0
    while [ "$cooldown" -lt 60 ] && [ ! -f "$STOP" ] && [ ! -f "$DISABLED" ] && [ ! -f "$PERSISTENT_DISABLED" ]; do
      sleep 1
      cooldown=$((cooldown+1))
    done
    failures=0
    continue
  fi
  sleep $((failures*2))
done
