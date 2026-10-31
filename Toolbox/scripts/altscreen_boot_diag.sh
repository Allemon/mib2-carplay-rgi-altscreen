#!/bin/sh
# QNX /tmp can be a process-manager link to /dev/shmem. That namespace
# supports flat files, but neither mkdir nor atomic rename. Use the boot-local
# QNX4 ramdisk for directory locks and atomically replaced metadata. Never fall
# back to a second lock namespace: all contenders must use the same path.
alts_posix_tmp_dir() (
    alts_storage_input=$1
    alts_storage_physical=$(CDPATH= cd "$alts_storage_input" 2>/dev/null && pwd -P) || alts_storage_physical=""
    case "$alts_storage_physical" in
        */dev/shmem) printf '%s/ramdisk/var/run\n' "${alts_storage_physical%/dev/shmem}"; return 0 ;;
    esac
    case "$alts_storage_input" in /tmp|/dev/shmem)
        if [ "$(uname -s 2>/dev/null)" = QNX ]; then
            printf '%s\n' /ramdisk/var/run; return 0
        fi ;;
    esac
    printf '%s\n' "$alts_storage_input"
)

# Independent boot observer: never loads, restarts, or arms CarPlay.
set -u

# QNX compatibility: some vehicle mkdir implementations return EEXIST for
# `mkdir -p` when the final directory already exists. Idempotent directory
# creation must therefore test first; lock acquisition still uses bare mkdir.
ensure_dirs() {
    for dir in "$@"; do
        [ -d "$dir" ] && continue
        mkdir -p "$dir" || return 1
    done
    return 0
}
# The boot block runs before OEM startup exports its application environment.
# A background child cannot inherit exports performed later by its parent.
PATH=${PATH:-/bin:/usr/bin}:/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}:/proc/boot:/usr/lib:/armle/lib:/armle/lib/dll:/lib:/mnt/app/root/carplay-altscreen/lib:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib:/lib/dll
export PATH LD_LIBRARY_PATH
ROOT=""; INTERVAL=2; ITERATIONS=0; PROBE_SECONDS=5
RAW_LIMIT=8388608; LOG_LIMIT=4194304; AUDIO_EVERY=5; META_EVERY=15
if [ "${ALTSCREEN_CHAIN_TESTING:-0}" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) exit 2 ;; esac
    INTERVAL=${ALTS_DIAG_INTERVAL:-0.1}
    ITERATIONS=${ALTS_DIAG_ITERATIONS:-5}
    PROBE_SECONDS=${ALTS_DIAG_PROBE_SECONDS:-1}
    RAW_LIMIT=${ALTS_DIAG_RAW_LIMIT:-8388608}
    LOG_LIMIT=${ALTS_DIAG_LOG_LIMIT:-4194304}
    AUDIO_EVERY=${ALTS_DIAG_AUDIO_EVERY:-5}
    META_EVERY=${ALTS_DIAG_META_EVERY:-15}
fi
for numeric in "$RAW_LIMIT" "$LOG_LIMIT" "$AUDIO_EVERY" "$META_EVERY"; do
    case "$numeric" in ''|*[!0-9]*|0) exit 2 ;; esac
done
ENABLED="$ROOT/mnt/app/root/carplay-altscreen/state/diagnostics.enabled"
[ -f "$ENABLED" ] || exit 0
# Probe execution, not just command presence: mounted commands can still lack
# their shared libraries early in boot. This observer is already asynchronous.
ready_wait=0
while ! (date +%Y >/dev/null && printf '%s' ready | wc -c >/dev/null &&
         printf '%s' ready | tail -c 1 >/dev/null &&
         printf '%s' ready | cksum >/dev/null) 2>/dev/null; do
    echo "BOOT_ENV_WAIT step=$ready_wait"
    ready_wait=$((ready_wait + 1))
    [ "$ready_wait" -lt 60 ] || { echo "BOOT_ENV_UNAVAILABLE"; exit 1; }
    sleep 2 || exit 1
done
select_hook_source() {
    for candidate in \
        "$ROOT/tmp/altscreen_hook.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay/altscreen_hook.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay.altscreen_hook.log"; do
        [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
    done
    printf '%s\n' "$ROOT/tmp/altscreen_hook.log"
}
select_boot_entry_source() {
    for candidate in \
        "$ROOT/tmp/altscreen_boot_entry.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay/boot_entry.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay.boot_entry.log"; do
        [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
    done
    printf '%s\n' "$ROOT/tmp/altscreen_boot_entry.log"
}
select_mirror_log_source() {
    for candidate in \
        "$ROOT/tmp/altscreen_mirror.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay/mirror/mirror.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay.mirror.log"; do
        [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
    done
    printf '%s\n' "$ROOT/tmp/altscreen_mirror.log"
}
select_mirror_autostart_source() {
    for candidate in \
        "$ROOT/tmp/altscreen_autostart.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay/mirror/autostart.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay.mirror.autostart.log"; do
        [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
    done
    printf '%s\n' "$ROOT/tmp/altscreen_autostart.log"
}
select_controller_log_source() {
    for candidate in \
        "$ROOT/tmp/mmi-mirror-controller.log" \
        "$ROOT/tmp/MMI-Cockpit-Carplay/mmi-mirror-controller.log"; do
        [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
    done
    # ClusterStateController writes this exact path on the vehicle.
    printf '%s\n' "$ROOT/tmp/mmi-mirror-controller.log"
}
select_wheel_log_source() {
    printf '%s\n' "$ROOT/tmp/mmi-mirror-wheel-zoom.log"
}
select_wheel_events_source() {
    printf '%s\n' "$ROOT/tmp/mmi-mirror-wheel-zoom.events"
}
select_carplay_hook_log_source() {
    printf '%s\n' "$ROOT/tmp/carplay_hook.log"
}
select_oem_geometry_history_source() {
    printf '%s\n' "$ROOT/tmp/carplay-oem-geometry.log"
}
select_oem_displaymanager_api_source() {
    printf '%s\n' "$ROOT/tmp/carplay-oem-displaymanager-read-api.log"
}
flat_plain_append() (
    # One writer per stream. Keep the latest four segments instead of stopping
    # capture at a size cap; the unfiltered system stream remains available.
    append_source=$1; append_target=$2
    if [ -f "$append_target" ] && [ "$(wc -c < "$append_target")" -ge "$LOG_LIMIT" ]; then
        rm -f "$append_target.3" || exit 1
        for segment in 2 1; do
            next=$((segment + 1))
            [ ! -f "$append_target.$segment" ] || mv "$append_target.$segment" "$append_target.$next" || exit 1
        done
        mv "$append_target" "$append_target.1" || exit 1
    fi
    cat "$append_source" >> "$append_target"
)
flat_read_cursor() {
    flat_cursor_path=$1
    [ -f "$flat_cursor_path" ] || { echo 0; return 0; }
    flat_cursor_value=$(cat "$flat_cursor_path" 2>/dev/null || true)
    case "$flat_cursor_value" in
        ''|*[!0-9]*) echo 0 ;;
        *) echo "$flat_cursor_value" ;;
    esac
}
flat_file_signature() {
    flat_sig_path=$1
    [ -f "$flat_sig_path" ] || { echo absent; return 0; }
    flat_sig_line=$(cksum "$flat_sig_path" 2>/dev/null) || { echo unreadable; return 0; }
    set -- $flat_sig_line
    [ "$#" -ge 2 ] || { echo unreadable; return 0; }
    printf '%s:%s\n' "$1" "$2"
}
flat_capture_delta() {
    flat_source=$1; flat_offset=$2; flat_target=$3; flat_temp=$4
    flat_cursor_path=${5:-}
    flat_initial_offset=$flat_offset
    [ -f "$flat_source" ] || { echo "$flat_offset"; return 0; }
    flat_size=$(wc -c < "$flat_source")
    [ "$flat_size" -ge "$flat_offset" ] || flat_offset=0
    if [ "$flat_size" -gt "$flat_offset" ]; then
        tail -c "+$((flat_offset + 1))" "$flat_source" > "$flat_temp" 2>/dev/null || { echo "$flat_offset"; return 0; }
        # tail can include bytes appended after the size sample. Advance by
        # the bytes actually copied, or the next poll duplicates those bytes.
        flat_bytes=$(wc -c < "$flat_temp")
        if flat_plain_append "$flat_temp" "$flat_target" 2>/dev/null; then
            rm -f "${flat_temp}.write-failed" 2>/dev/null || true
            flat_offset=$((flat_offset + flat_bytes))
            if [ "$flat_source" = "${flat_system:-}" ]; then flat_audio_capture "$flat_temp"; fi
        else
            # The source remains captured with the same offset. One marker
            # per source suppresses repeated SD failure messages across command
            # substitutions; clear it only after a successful write.
            if [ ! -f "${flat_temp}.write-failed" ]; then
                flat_entry=$(select_boot_entry_source)
                flat_entry_size=$(wc -c < "$flat_entry" 2>/dev/null) || flat_entry_size=0
                set -- $flat_entry_size
                case "${1:-0}" in *[!0-9]*) ;; *)
                    if [ "${1:-0}" -ge 65536 ]; then (printf '' > "$flat_entry") 2>/dev/null || true; fi ;;
                esac
                printf 'CAPTURE_WRITE_FAILED target=%s retained_offset=%s further_errors=suppressed_until_success\n' "$flat_target" "$flat_offset" >&2
                printf '' > "${flat_temp}.write-failed" 2>/dev/null || true
            fi
        fi
        rm -f "$flat_temp"
    fi
    if [ -n "$flat_cursor_path" ] && [ "$flat_offset" != "$flat_initial_offset" ]; then
        flat_cursor_tmp="${flat_cursor_path}.new.$$"
        if printf '%s\n' "$flat_offset" > "$flat_cursor_tmp" 2>/dev/null; then
            mv "$flat_cursor_tmp" "$flat_cursor_path" 2>/dev/null || rm -f "$flat_cursor_tmp"
        fi
    fi
    echo "$flat_offset"
}
flat_probe() {
    [ ! -f "${FLAT_PREFIX}_probe.stop" ] || return 0
    flat_name=$1; shift
    flat_raw="${FLAT_PREFIX}_${flat_name}.raw"
    flat_out="${FLAT_PREFIX}_${flat_name}.out"
    printf '%s PROBE_BEGIN command=%s timeout_seconds=%s\n' "$(date +%Y%m%d_%H%M%S)" "$*" "$PROBE_SECONDS" > "$flat_out"
    flat_plain_append "$flat_out" "$FLAT_DEST/$flat_name.log" 2>/dev/null || true
    rm -f "${FLAT_PREFIX}_probe.timeout"
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "MISSING_COMMAND $1" > "$flat_out"
    else
        (exec "$@") > "$flat_raw" 2>&1 &
        flat_child=$!
        printf '%s\n' "$flat_child" > "${FLAT_PREFIX}_probe.child"
        (sleep "$PROBE_SECONDS"; echo 1 > "${FLAT_PREFIX}_probe.timeout"; kill -KILL "$flat_child" 2>/dev/null || true) &
        flat_timer=$!
        printf '%s\n' "$flat_timer" > "${FLAT_PREFIX}_probe.timer"
        wait "$flat_child"; flat_rc=$?
        kill "$flat_timer" 2>/dev/null || true; wait "$flat_timer" 2>/dev/null || true
        rm -f "${FLAT_PREFIX}_probe.child" "${FLAT_PREFIX}_probe.timer"
        tail -c 1048576 "$flat_raw" > "$flat_out"
        flat_timed_out=0; [ ! -f "${FLAT_PREFIX}_probe.timeout" ] || flat_timed_out=1
        printf '%s PROBE_RESULT status=%s timed_out=%s command=%s\n' "$(date +%Y%m%d_%H%M%S)" "$flat_rc" "$flat_timed_out" "$*" >> "$flat_out"
    fi
    flat_plain_append "$flat_out" "$FLAT_DEST/$flat_name.log" 2>/dev/null || true
    rm -f "$flat_raw" "$flat_out"
}
# Which file pf runs depends on the uplink: with the LTE ECM data link up the
# stock network service loads /etc/pf.ecm0.conf (pf.ppp0.conf, pf.mlan0.conf)
# in place of /etc/pf.conf. Record every pf configuration, whether it carries
# the fixed type-111 rule, every script that runs pfctl, and which live
# ruleset was active.
# Bounded: 4 captures per boot, files up to 256 KiB, 40 files per capture.
flat_firewall_sources() {
    [ ! -f "${FLAT_PREFIX}_probe.stop" ] || return 0
    fw_count_file="${FLAT_PREFIX}_fwsrc.count"
    fw_count=0
    [ ! -f "$fw_count_file" ] || fw_count=$(cat "$fw_count_file" 2>/dev/null || echo 0)
    case "$fw_count" in 0|1|2|3) ;; *) return 0 ;; esac
    fw_count=$((fw_count + 1))
    echo "$fw_count" > "$fw_count_file"
    fw_out="${FLAT_PREFIX}_fwsrc.out"
    {
        printf '%s FIREWALL_SOURCES_BEGIN capture=%s\n' "$(date +%Y%m%d_%H%M%S)" "$fw_count"
        fw_live=UNKNOWN
        if [ -f "$FLAT_DEST/pf_rules.txt.log" ]; then
            fw_tail=$(tail -c 32768 "$FLAT_DEST/pf_rules.txt.log" 2>/dev/null)
            case "$fw_tail" in
                *" on ecm1 "*) fw_live=ECM ;;
                *" on ppp0 "*) fw_live=PPP ;;
                *) fw_live=OTHER ;;
            esac
            case "$fw_tail" in *"port = 6031"*) fw_6031=LIVE ;; *) fw_6031=ABSENT ;; esac
            printf 'FIREWALL_LIVE_RULESET kind=%s type111_6031=%s\n' "$fw_live" "$fw_6031"
        fi
        for fw_dir in "$ROOT/etc" "$ROOT/etc/scripts" "$ROOT/mnt/system/etc" "$ROOT/mnt/system/etc/scripts" "$ROOT/tmp" "$ROOT/var" "$ROOT/proc/boot"; do
            [ -d "$fw_dir" ] || continue
            printf 'FIREWALL_DIR %s\n' "$fw_dir"
            ls -la "$fw_dir" 2>&1 | grep -i -E 'pf|huawei|nad|ecm|dhcp|ppp|lte|network|firewall|wan' || true
        done
        fw_files=0
        for fw_file in "$ROOT"/etc/* "$ROOT"/etc/scripts/* "$ROOT"/mnt/system/etc/* "$ROOT"/mnt/system/etc/scripts/* "$ROOT"/tmp/*pf* "$ROOT"/var/*pf*; do
            [ -f "$fw_file" ] || continue
            [ "$fw_files" -lt 40 ] || { printf 'FIREWALL_FILES_TRUNCATED limit=40\n'; break; }
            fw_size=$(wc -c < "$fw_file" 2>/dev/null) || continue
            fw_size=$((fw_size + 0))
            [ "$fw_size" -gt 0 ] && [ "$fw_size" -le 262144 ] || continue
            case "${fw_file##*/}" in
                *pf*) ;;
                *) grep -q -e carplay0 -e pfctl "$fw_file" 2>/dev/null || continue ;;
            esac
            fw_files=$((fw_files + 1))
            fw_sum=$(cksum < "$fw_file" 2>/dev/null | awk '{print $1}')
            case "${fw_file##*/}" in
                pf*.conf)
                    if grep -q 'port = 6031' "$fw_file" 2>/dev/null; then fw_rule=PRESENT; else fw_rule=ABSENT; fi
                    printf 'FIREWALL_TYPE111_FILE path=%s rule6031=%s\n' "$fw_file" "$fw_rule" ;;
            esac
            # Bodies once per boot; later captures only show whether they changed.
            if [ "$fw_count" = 1 ]; then
                printf 'FILE_BEGIN path=%s size=%s cksum=%s\n' "$fw_file" "$fw_size" "$fw_sum"
                cat "$fw_file" 2>&1
                printf '\nFILE_END path=%s\n' "$fw_file"
            else
                printf 'FILE path=%s size=%s cksum=%s\n' "$fw_file" "$fw_size" "$fw_sum"
            fi
        done
        printf '%s FIREWALL_SOURCES_END capture=%s files=%s live=%s\n' "$(date +%Y%m%d_%H%M%S)" "$fw_count" "$fw_files" "$fw_live"
    } > "$fw_out" 2>&1
    flat_plain_append "$fw_out" "$FLAT_DEST/firewall_sources.txt.log" 2>/dev/null || true
    rm -f "$fw_out"
}
flat_audio_capture() (
    # Preserve a partial line between polls so an error split across writes is
    # still indexed. This is an index of existing stock messages, no PCM dump.
    audio_input="${FLAT_PREFIX}_audio.input"
    audio_partial="${FLAT_PREFIX}_audio.partial"
    audio_output="${FLAT_PREFIX}_audio.chunk"
    { [ ! -f "$audio_partial" ] || cat "$audio_partial"; cat "$1"; } > "$audio_input" || exit 1
    complete=0; [ -n "$(tail -c 1 "$1")" ] || complete=1
    printf '' > "$audio_partial"
    awk -v complete="$complete" -v pending="$audio_partial" '
      function emit(line) {
        if (line ~ /AudioStream|CQNXSoundIF|RTP_AUDIO|ann3_dio_ann|io-audio|audio_service|<AUDIO>|AUDIO_FINALIZE|Modes changed|audioThreadPriority|audioLatency|[Uu]nderrun|[Oo]verrun|HB: T=|HB:.*(dio_manager|smartphone_integrator|io-audio|audio_service|mirror-display|maneuver_render|j9)/)
          print line
      }
      NR > 1 { emit(previous) }
      { previous=$0 }
      END {
        if (complete) emit(previous)
        else if (length(previous) <= 8192) printf "%s", previous > pending
        else print "AUDIO_INDEX_PARTIAL_LINE_DROPPED bytes=" length(previous) " full_system_log_preserved=1"
      }
    ' "$audio_input" > "$audio_output" || exit 1
    if [ -s "$audio_output" ]; then
        flat_plain_append "$audio_output" "$FLAT_DEST/streams/audio.log" ||
            printf 'AUDIO_INDEX_WRITE_FAILED full_system_log_preserved=1\n' >&2
    fi
    rm -f "$audio_input" "$audio_output"
)
flat_pid_alive() {
    case "$1" in ''|*[!0-9]*|0|1) return 1 ;; esac
    kill -0 "$1" 2>/dev/null
}
flat_start_slog() {
    flat_follow_generation=$((flat_follow_generation + 1))
    # Called only after the previous writer is gone and its final bytes copied.
    # A replay is a new source boundary, not the continuation of an incomplete
    # line left by the old follower. Preserve that line with an explicit marker.
    if [ -s "${FLAT_PREFIX}_audio.partial" ]; then
        printf '\n' > "${FLAT_PREFIX}_audio.boundary"
        flat_audio_capture "${FLAT_PREFIX}_audio.boundary"
        rm -f "${FLAT_PREFIX}_audio.boundary"
        flat_log_event "AUDIO_INDEX_SOURCE_BOUNDARY incomplete_line_terminated=1"
    fi
    printf '' > "$flat_system"
    flat_system_offset=0
    (exec sloginfo -w -t) >> "$flat_system" 2>&1 & flat_slog_pid=$!
    flat_follow_stop=0
    flat_log_event "SYSTEM_FOLLOW_START pid=$flat_slog_pid generation=$flat_follow_generation replay_possible=1 command=sloginfo_-w_-t"
}
flat_follow_poll() {
    if ! flat_pid_alive "$flat_slog_pid"; then
        [ -z "$flat_slog_pid" ] || wait "$flat_slog_pid" 2>/dev/null || true
        if command -v sloginfo >/dev/null 2>&1; then
            # The writer may exit after the loop's first copy. Drain again now
            # that it is gone; never discard its last bytes or an SD write retry.
            flat_system_offset=$(flat_capture_delta "$flat_system" "$flat_system_offset" "$FLAT_DEST/streams/system.log" "${FLAT_PREFIX}_system.chunk")
            if [ -f "$flat_system" ] && [ "$(wc -c < "$flat_system")" -gt "$flat_system_offset" ]; then
                flat_log_event "SYSTEM_FOLLOW_DRAIN_PENDING captured_bytes=$flat_system_offset restart=DEFERRED"
                return 0
            fi
            flat_log_event "SYSTEM_FOLLOW_RESTART previous_pid=${flat_slog_pid:-NONE} reason=exit_or_rotation captured_bytes=$flat_system_offset"
            flat_start_slog
        elif [ $((flat_tick % META_EVERY)) = 0 ]; then
            flat_log_event "SYSTEM_FOLLOW_UNAVAILABLE command=sloginfo"
        fi
    elif [ "$flat_follow_stop" = 0 ] && [ -f "$flat_system" ] && [ "$(wc -c < "$flat_system")" -ge "$RAW_LIMIT" ]; then
        # Never truncate an open writer. Its final tail is copied on the next
        # poll before restarting. sloginfo replays its ring; record that boundary.
        flat_log_event "SYSTEM_FOLLOW_ROTATE_REQUEST generation=$flat_follow_generation limit_bytes=$RAW_LIMIT"
        flat_follow_stop=1
        kill -KILL "$flat_slog_pid" 2>/dev/null || true
    fi
}
flat_audio_probes() {
    # These read-only snapshots run separately from both stream persistence and
    # mount/library probes. Stock scheduling/CPU counters require no new hooks.
    flat_probe audio_dio_sched.txt pidin -p dio_manager sched
    flat_probe audio_dio_ttimes.txt pidin -p dio_manager ttimes
    flat_probe audio_integrator_sched.txt pidin -p smartphone_integrator sched
    flat_probe audio_integrator_ttimes.txt pidin -p smartphone_integrator ttimes
    flat_probe audio_driver_sched.txt pidin -p io-audio sched
    flat_probe audio_driver_ttimes.txt pidin -p io-audio ttimes
    flat_probe audio_service_sched.txt pidin -p audio_service sched
    flat_probe audio_service_ttimes.txt pidin -p audio_service ttimes
    flat_probe audio_mirror_ttimes.txt pidin -p carplay-alt111-mirror-display ttimes
    flat_probe audio_rgi_ttimes.txt pidin -p maneuver_render ttimes
    flat_probe audio_java_ttimes.txt pidin -p j9 ttimes
}
flat_stop_worker() {
    # Kill only the exact children published by our own worker. No slay/global
    # process-name cleanup, and no unbounded wait on a blocked QNX resource.
    worker_prefix=$1; worker_pid=$2
    printf '' > "${worker_prefix}_probe.stop"
    if flat_pid_alive "$worker_pid"; then kill -KILL "$worker_pid" 2>/dev/null || true; fi
    for child_file in "$worker_prefix"_probe.child "$worker_prefix"_probe.timer; do
        child_pid=$(cat "$child_file" 2>/dev/null || true)
        if flat_pid_alive "$child_pid"; then kill -KILL "$child_pid" 2>/dev/null || true; fi
    done
}
flat_cleanup_children() {
    flat_stop_worker "${FLAT_PREFIX}_metadata" "${flat_metadata_pid:-}"
    flat_stop_worker "${FLAT_PREFIX}_audio-probe" "${flat_audio_pid:-}"
    if flat_pid_alive "${flat_slog_pid:-}"; then kill -KILL "$flat_slog_pid" 2>/dev/null || true; fi
}
flat_log_event() {
    flat_event="${FLAT_PREFIX}_event"
    printf '%s %s\n' "$(date +%Y%m%d_%H%M%S)" "$*" > "$flat_event"
    flat_plain_append "$flat_event" "$FLAT_DEST/boot.log" 2>/dev/null || true
    rm -f "$flat_event"
}
flat_reserve_destination() {
    # An uncalibrated clock and reused PID can repeat across cold boots. Never
    # append to an existing capture: reserve a fresh SD directory atomically.
    flat_boot_parent=$1; flat_boot_stem=$2; flat_boot_suffix=0
    ensure_dirs "$flat_boot_parent" 2>/dev/null || return 1
    while [ "$flat_boot_suffix" -lt 10000 ]; do
        BOOT_ID=$flat_boot_stem
        [ "$flat_boot_suffix" -eq 0 ] || BOOT_ID="${flat_boot_stem}_$flat_boot_suffix"
        FLAT_DEST="$flat_boot_parent/$BOOT_ID"
        if mkdir "$FLAT_DEST" 2>/dev/null; then return 0; fi
        # Stop on removed/read-only storage rather than spinning indefinitely.
        [ -e "$FLAT_DEST" ] || return 1
        flat_boot_suffix=$((flat_boot_suffix + 1))
    done
    return 1
}
run_flat_plaintext() {
    VOLUME=$1
    if ! flat_reserve_destination "$VOLUME/MMI-Cockpit-Carplay/logs/boots" "boot_$(date +%Y%m%d_%H%M%S)_$$"; then
        return 1
    fi
    FLAT_PREFIX="$ROOT/tmp/altscreen_diag_$$"
    ensure_dirs "$FLAT_DEST/streams" 2>/dev/null || return 1

    # Cursor state is volatile and published on a filesystem with rename. It survives only an
    # observer-process restart in the current boot, so a real reboot starts at
    # offset zero even when the vehicle clock is still 1970 and PIDs repeat.
    # This also avoids periodic cursor metadata writes to the SD card.
    cursor_dir=$(alts_posix_tmp_dir "$ROOT/tmp")
    CURSOR_PREFIX="$cursor_dir/altscreen_diag_cursor"
    if ! ensure_dirs "$cursor_dir"; then
        echo "WARN: diagnostic cursor storage unavailable: $cursor_dir; collection continues"
    fi

    # START/controller wrappers may have emitted a flat /tmp journal before the
    # SD card became writable. Promote those breadcrumbs now, without requiring
    # any volatile directory tree.
    operation_dest="$VOLUME/MMI-Cockpit-Carplay/logs/operations"
    if ensure_dirs "$operation_dest" 2>/dev/null; then
        for operation in "$ROOT/tmp"/altscreen_start_*.log "$ROOT/tmp"/altscreen_operation_*.log; do
            [ -f "$operation" ] || continue
            operation_name=${operation##*/}
            if cp "$operation" "$operation_dest/$operation_name.new" 2>/dev/null &&
               mv "$operation_dest/$operation_name.new" "$operation_dest/$operation_name" 2>/dev/null; then
                rm -f "$operation" 2>/dev/null || true
            else
                rm -f "$operation_dest/$operation_name.new" 2>/dev/null || true
            fi
        done
    fi
    flat_log_event "BOOT_BEGIN storage=FLAT_TMP_PLAINTEXT_SD volume=$VOLUME cursor_scope=VOLATILE_BOOT_POSIX cursor_resume=1 cursor_dir=$cursor_dir audio_diagnostics=V1 probes=INDEPENDENT_ASYNC log_segment_bytes=$LOG_LIMIT retained_segments=4"
    flat_log_event "SD_READY volume=$VOLUME"
    flat_system="${FLAT_PREFIX}_system.raw"; flat_slog_pid=""
    flat_follow_generation=0; flat_follow_stop=0
    flat_audio_pid=""; flat_metadata_pid=""
    trap 'flat_cleanup_children; exit 130' 1 2 15
    flat_hook_offset=$(flat_read_cursor "${CURSOR_PREFIX}_hook.offset")
    flat_dio_offset=$(flat_read_cursor "${CURSOR_PREFIX}_dio.offset")
    flat_entry_offset=$(flat_read_cursor "${CURSOR_PREFIX}_boot-entry.offset")
    flat_mirror_offset=$(flat_read_cursor "${CURSOR_PREFIX}_mirror.offset")
    flat_mirror_autostart_offset=$(flat_read_cursor "${CURSOR_PREFIX}_mirror-autostart.offset")
    flat_controller_offset=$(flat_read_cursor "${CURSOR_PREFIX}_controller.offset")
    flat_wheel_log_offset=$(flat_read_cursor "${CURSOR_PREFIX}_wheel-log.offset")
    flat_carplay_hook_offset=$(flat_read_cursor "${CURSOR_PREFIX}_carplay-hook.offset")
    flat_carplay_trace_offset=$(flat_read_cursor "${CURSOR_PREFIX}_carplay-trace.offset")
    flat_rgi_offset=$(flat_read_cursor "${CURSOR_PREFIX}_rgi.offset")
    flat_rgi_native_offset=$(flat_read_cursor "${CURSOR_PREFIX}_rgi-native.offset")
    flat_supervisor_offset=$(flat_read_cursor "${CURSOR_PREFIX}_supervisor.offset")
    flat_oem_geometry_offset=$(flat_read_cursor "${CURSOR_PREFIX}_oem-geometry.offset")
    flat_oem_api_offset=$(flat_read_cursor "${CURSOR_PREFIX}_oem-api.offset")
    flat_wheel_events_sig=absent
    flat_system_offset=0
    flat_tick=0
    while [ -f "$ENABLED" ]; do
        # Persist the continuous system stream before any optional work. This
        # also copies the final tail of a dead follower before its restart.
        flat_system_offset=$(flat_capture_delta "$flat_system" "$flat_system_offset" "$FLAT_DEST/streams/system.log" "${FLAT_PREFIX}_system.chunk")
        flat_follow_poll
        flat_hook_offset=$(flat_capture_delta "$(select_hook_source)" "$flat_hook_offset" "$FLAT_DEST/streams/hook_tmp.log" "${FLAT_PREFIX}_hook.chunk" "${CURSOR_PREFIX}_hook.offset")
        flat_dio_offset=$(flat_capture_delta "$ROOT/tmp/CinemoDioManager.log" "$flat_dio_offset" "$FLAT_DEST/streams/dio_tmp.log" "${FLAT_PREFIX}_dio.chunk" "${CURSOR_PREFIX}_dio.offset")
        flat_entry_offset=$(flat_capture_delta "$(select_boot_entry_source)" "$flat_entry_offset" "$FLAT_DEST/streams/boot_entry.log" "${FLAT_PREFIX}_entry.chunk" "${CURSOR_PREFIX}_boot-entry.offset")
        flat_mirror_offset=$(flat_capture_delta "$(select_mirror_log_source)" "$flat_mirror_offset" "$FLAT_DEST/streams/mirror.log" "${FLAT_PREFIX}_mirror.chunk" "${CURSOR_PREFIX}_mirror.offset")
        flat_mirror_autostart_offset=$(flat_capture_delta "$(select_mirror_autostart_source)" "$flat_mirror_autostart_offset" "$FLAT_DEST/streams/mirror_autostart.log" "${FLAT_PREFIX}_mirror_autostart.chunk" "${CURSOR_PREFIX}_mirror-autostart.offset")
        flat_controller_offset=$(flat_capture_delta "$(select_controller_log_source)" "$flat_controller_offset" "$FLAT_DEST/streams/mmi-mirror-controller.log" "${FLAT_PREFIX}_controller.chunk" "${CURSOR_PREFIX}_controller.offset")
        flat_wheel_log_offset=$(flat_capture_delta "$(select_wheel_log_source)" "$flat_wheel_log_offset" "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.log" "${FLAT_PREFIX}_wheel_log.chunk" "${CURSOR_PREFIX}_wheel-log.offset")
        flat_carplay_hook_offset=$(flat_capture_delta "$(select_carplay_hook_log_source)" "$flat_carplay_hook_offset" "$FLAT_DEST/streams/carplay_hook.log" "${FLAT_PREFIX}_carplay_hook.chunk" "${CURSOR_PREFIX}_carplay-hook.offset")
        # Synchronous Java crash breadcrumbs (CrashTrace); survives a j9 fault in /tmp.
        flat_carplay_trace_offset=$(flat_capture_delta "$ROOT/tmp/carplay_trace.log" "$flat_carplay_trace_offset" "$FLAT_DEST/streams/carplay_trace.log" "${FLAT_PREFIX}_carplay_trace.chunk" "${CURSOR_PREFIX}_carplay-trace.offset")
        flat_rgi_offset=$(flat_capture_delta "$ROOT/tmp/maneuver_render.log" "$flat_rgi_offset" "$FLAT_DEST/streams/maneuver_render.log" "${FLAT_PREFIX}_rgi.chunk" "${CURSOR_PREFIX}_rgi.offset")
        flat_rgi_native_offset=$(flat_capture_delta "$ROOT/tmp/carplay_rgi_native.log" "$flat_rgi_native_offset" "$FLAT_DEST/streams/carplay_rgi_native.log" "${FLAT_PREFIX}_rgi_native.chunk" "${CURSOR_PREFIX}_rgi-native.offset")
        flat_supervisor_offset=$(flat_capture_delta "$ROOT/tmp/altscreen_stream_supervisor.log" "$flat_supervisor_offset" "$FLAT_DEST/streams/altscreen_stream_supervisor.log" "${FLAT_PREFIX}_supervisor.chunk" "${CURSOR_PREFIX}_supervisor.offset")
        flat_oem_geometry_offset=$(flat_capture_delta "$(select_oem_geometry_history_source)" "$flat_oem_geometry_offset" "$FLAT_DEST/streams/carplay-oem-geometry.log" "${FLAT_PREFIX}_oem_geometry.chunk" "${CURSOR_PREFIX}_oem-geometry.offset")
        flat_oem_api_offset=$(flat_capture_delta "$(select_oem_displaymanager_api_source)" "$flat_oem_api_offset" "$FLAT_DEST/streams/carplay-oem-displaymanager-read-api.log" "${FLAT_PREFIX}_oem_api.chunk" "${CURSOR_PREFIX}_oem-api.offset")
        wheel_events_source=$(select_wheel_events_source)
        wheel_events_sig=$(flat_file_signature "$wheel_events_source")
        if [ "$wheel_events_sig" != "$flat_wheel_events_sig" ]; then
            if [ -f "$wheel_events_source" ]; then
                if cp "$wheel_events_source" "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events.new" 2>/dev/null &&
                   mv "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events.new" "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events" 2>/dev/null; then
                    flat_wheel_events_sig=$wheel_events_sig
                else
                    rm -f "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events.new" 2>/dev/null || true
                fi
            else
                rm -f "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events" "$FLAT_DEST/streams/mmi-mirror-wheel-zoom.events.new" 2>/dev/null || true
                flat_wheel_events_sig=absent
            fi
        fi
        if [ -f "$ROOT/tmp/carplay-oem-geometry.state" ]; then
            cp "$ROOT/tmp/carplay-oem-geometry.state" "$FLAT_DEST/streams/carplay-oem-geometry.state.new" 2>/dev/null &&
                mv "$FLAT_DEST/streams/carplay-oem-geometry.state.new" "$FLAT_DEST/streams/carplay-oem-geometry.state" 2>/dev/null || true
        fi
        if [ -f "$ROOT/tmp/mmi-altscreen-displayable3.state" ]; then
            cp "$ROOT/tmp/mmi-altscreen-displayable3.state" "$FLAT_DEST/streams/mmi-altscreen-displayable3.state.new" 2>/dev/null &&
                mv "$FLAT_DEST/streams/mmi-altscreen-displayable3.state.new" "$FLAT_DEST/streams/mmi-altscreen-displayable3.state" 2>/dev/null || true
        fi
        if [ $((flat_tick % AUDIO_EVERY)) = 0 ]; then
            if flat_pid_alive "$flat_audio_pid"; then
                flat_log_event "AUDIO_PROBE_BUSY pid=$flat_audio_pid stream_recording=CONTINUES"
            else
                [ -z "$flat_audio_pid" ] || wait "$flat_audio_pid" 2>/dev/null || true
                ( FLAT_PREFIX="${FLAT_PREFIX}_audio-probe"; flat_audio_probes ) &
                flat_audio_pid=$!
            fi
        fi
        if [ $((flat_tick % META_EVERY)) = 0 ]; then
            flat_log_event "RECORDER_HEARTBEAT tick=$flat_tick system_bytes=$flat_system_offset follower_pid=${flat_slog_pid:-NONE} follow_generation=$flat_follow_generation audio_probe_pid=${flat_audio_pid:-NONE}"
            if flat_pid_alive "$flat_metadata_pid"; then
                flat_log_event "METADATA_PROBE_BUSY pid=$flat_metadata_pid stream_recording=CONTINUES"
            else
              [ -z "$flat_metadata_pid" ] || wait "$flat_metadata_pid" 2>/dev/null || true
              (
                FLAT_PREFIX="${FLAT_PREFIX}_metadata"
            flat_probe processes.txt pidin arguments
            flat_probe dio_libraries.txt pidin -p dio_manager libs
            flat_probe dio_mappings.txt pidin -p dio_manager mapinfo
            flat_probe integrator_libraries.txt pidin -p smartphone_integrator libs
            flat_probe mounts.txt mount
            flat_probe network.txt netstat -an
            if [ -x "$ROOT/armle/sbin/pfctl" ]; then
                flat_probe pf_rules.txt "$ROOT/armle/sbin/pfctl" -sr
            else
                flat_probe pf_rules.txt pfctl -sr
            fi
            flat_firewall_sources
            flat_state="${FLAT_PREFIX}_state.out"
            {
                date
                ls -la "$ROOT/mnt/app/root/carplay-altscreen/lib"
                ls -la "$ROOT/mnt/app/root/carplay-altscreen/bin/mirror"
                ls -la "$ROOT/mnt/app/root/carplay-altscreen/bin/rgi"
                ls -la "$ROOT/mnt/app/root/carplay-altscreen/state"
                printf 'DISPLAY_STATE_PATH_PERMISSIONS_BEGIN\n'
                id 2>/dev/null || true
                ls -ld "$ROOT/tmp" 2>&1 || true
                for display_state_probe in "$ROOT/tmp/mmi-altscreen-displayable3.state" "$ROOT/tmp/mmi-altscreen-displayable3.state.tmp" "$ROOT/tmp"/mmi-altscreen-displayable3.state.tmp.*; do
                    if [ -e "$display_state_probe" ]; then
                        ls -l "$display_state_probe" 2>&1 || true
                    else
                        printf 'DISPLAY_STATE_PATH path=%s status=ABSENT\n' "$display_state_probe"
                    fi
                done
                printf 'DISPLAY_STATE_PATH_PERMISSIONS_END\n'
                for rgi_marker in altscreen_rgi_supervisor.pid altscreen_rgi_renderer.pid mmi-rgi.disabled altscreen_mirror.stop.requested; do
                    if [ -f "$ROOT/tmp/$rgi_marker" ]; then
                        printf 'RGI_STATE %s=' "$rgi_marker"
                        cat "$ROOT/tmp/$rgi_marker"
                        printf '\n'
                    else
                        printf 'RGI_STATE %s=ABSENT\n' "$rgi_marker"
                    fi
                done
                for rgi_verbose in "$ROOT/tmp/carplay_verbose" "$ROOT/mnt/app/carplay_verbose" "$ROOT/mnt/app/root/carplay-altscreen/state/rgi-debug.enabled"; do
                    if [ -f "$rgi_verbose" ]; then printf 'RGI_LOG_MARKER %s=PRESENT\n' "$rgi_verbose"; else printf 'RGI_LOG_MARKER %s=ABSENT\n' "$rgi_verbose"; fi
                done
                # Stock crash artifacts (j9/dio_manager cores, SI process dumps):
                # names, sizes and times only; the files stay on the head unit.
                printf 'CRASH_DUMPS_BEGIN\n'
                ls -la "$ROOT/mnt/ota/system/core" 2>&1 || true
                ls -la "$ROOT/mnt/ota/system/logs" 2>&1 | tail -n 40 || true
                printf 'CRASH_DUMPS_END\n'
                ls -la "$VOLUME/MMI-Cockpit-Carplay/state"
            } > "$flat_state" 2>&1
            flat_plain_append "$flat_state" "$FLAT_DEST/file_state.txt.log" 2>/dev/null || true
            rm -f "$flat_state"
              ) &
              flat_metadata_pid=$!
            fi
        fi
        flat_tick=$((flat_tick + 1))
        if [ "$ITERATIONS" -gt 0 ] && [ "$flat_tick" -ge "$ITERATIONS" ]; then break; fi
        sleep "$INTERVAL"
    done
    # Give fast snapshots a bounded opportunity to finish. Stream collection
    # never waits for them during normal operation.
    flat_drain=0; flat_drain_interval=1; flat_drain_limit=2
    if [ "${ALTSCREEN_CHAIN_TESTING:-0}" = 1 ]; then flat_drain_interval=0.1; flat_drain_limit=10; fi
    while { flat_pid_alive "$flat_metadata_pid" || flat_pid_alive "$flat_audio_pid"; } && [ "$flat_drain" -lt "$flat_drain_limit" ]; do
        sleep "$flat_drain_interval"; flat_drain=$((flat_drain + 1))
    done
    flat_cleanup_children
    flat_drain=0
    while flat_pid_alive "$flat_slog_pid" && [ "$flat_drain" -lt "$flat_drain_limit" ]; do
        sleep "$flat_drain_interval"; flat_drain=$((flat_drain + 1))
    done
    flat_system_offset=$(flat_capture_delta "$flat_system" "$flat_system_offset" "$FLAT_DEST/streams/system.log" "${FLAT_PREFIX}_system.chunk")
    flat_log_event "DIAGNOSTICS_STOP"
    # Do not unlink scratch still referenced by a stuck resource-manager call.
    if ! flat_pid_alive "$flat_slog_pid" && ! flat_pid_alive "$flat_metadata_pid" && ! flat_pid_alive "$flat_audio_pid"; then
        rm -f "${FLAT_PREFIX}"_* 2>/dev/null || true
    fi
    trap - 1 2 15
    return 0
}

# Flat /tmp contract: diagnostics persist directly to SD and use only
# process-local flat /tmp scratch (altscreen_diag_$$*). No /tmp directory tree
# is created, so early boot logging cannot be blocked by QNX mkdir behavior.
storage_wait=0
while [ -f "$ENABLED" ]; do
    storage_volume=""
    if [ "${ALTSCREEN_CHAIN_TESTING:-0}" = 1 ]; then
        storage_candidate=${ALTSCREEN_CHAIN_VOLUME:-}
        [ ! -d "$storage_candidate/Toolbox" ] || storage_volume=$storage_candidate
    else
        for storage_candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
            if [ -d "$storage_candidate/Toolbox" ] && { [ -d "$storage_candidate/MMI-Cockpit-Carplay/backup/original" ] || [ -d "$storage_candidate/Backup/AltScreenChain/original" ]; }; then
                storage_volume=$storage_candidate
                break
            fi
        done
    fi
    if [ -n "$storage_volume" ]; then
        if [ "${ALTSCREEN_CHAIN_TESTING:-0}" != 1 ]; then
            (exec mount -uw "$storage_volume") >/dev/null 2>&1 &
            storage_mount_pid=$!
            (sleep 5; kill -KILL "$storage_mount_pid" 2>/dev/null || true) &
            storage_timer=$!
            wait "$storage_mount_pid" || true
            kill "$storage_timer" 2>/dev/null || true
            wait "$storage_timer" 2>/dev/null || true
        fi
        if ensure_dirs "$storage_volume/MMI-Cockpit-Carplay/logs/boots" 2>/dev/null; then
            if run_flat_plaintext "$storage_volume"; then exit 0; fi
            if [ $((storage_wait % 30)) -eq 0 ]; then
                echo "CAPTURE_DESTINATION_UNAVAILABLE volume=$storage_volume"
            fi
        fi
    fi
    # No SD is an idle state, not a reason to fill the shared-memory disk.
    if [ $((storage_wait % 30)) -eq 0 ]; then
        boot_entry=$(select_boot_entry_source)
        boot_size=$(wc -c < "$boot_entry" 2>/dev/null) || boot_size=0
        set -- $boot_size
        boot_size=${1:-0}
        case "$boot_size" in ''|*[!0-9]*) boot_size=0 ;; esac
        if [ "$boot_size" -ge 65536 ]; then (printf '' > "$boot_entry") 2>/dev/null || true; fi
        echo "SD_WAIT storage=FLAT_TMP_PLAINTEXT_SD attempt=$storage_wait"
    fi
    storage_wait=$((storage_wait + 1))
    if [ "$ITERATIONS" -gt 0 ] && [ "$storage_wait" -ge "$ITERATIONS" ]; then exit 0; fi
    sleep "$INTERVAL"
done
exit 0
