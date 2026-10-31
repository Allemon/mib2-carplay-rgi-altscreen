#!/bin/sh
# Shared SD write-access preflight for MMI-Cockpit-Carplay.
# Policy: real write probe -> remount RW if needed -> real write probe again.
# mount(8) return codes are never accepted as proof of writability.

altscreen_sd_probe_write() {
    alts_sd_volume=$1
    alts_sd_scope=${2:-SD}
    alts_sd_phase=${3:-CHECK}
    alts_sd_probe="$alts_sd_volume/.altscreen-rw-probe.$$"
    alts_sd_err="/tmp/altscreen_sd_probe_$$.err"
    [ -d "$alts_sd_volume" ] || {
        echo "SD_WRITE_PROBE scope=$alts_sd_scope phase=$alts_sd_phase path=$alts_sd_volume result=FAIL reason=DIR_ABSENT"
        return 1
    }
    rm -f "$alts_sd_probe" "$alts_sd_err" 2>/dev/null || true
    if ( umask 077; printf '%s\n' "altscreen-write-probe" > "$alts_sd_probe" ) 2>"$alts_sd_err"; then
        rm -f "$alts_sd_probe" "$alts_sd_err" 2>/dev/null || true
        echo "SD_WRITE_PROBE scope=$alts_sd_scope phase=$alts_sd_phase path=$alts_sd_volume result=PASS"
        return 0
    fi
    alts_sd_msg=$(sed -n '1p' "$alts_sd_err" 2>/dev/null || true)
    [ -n "$alts_sd_msg" ] || alts_sd_msg=write_failed_without_stderr
    rm -f "$alts_sd_probe" "$alts_sd_err" 2>/dev/null || true
    echo "SD_WRITE_PROBE scope=$alts_sd_scope phase=$alts_sd_phase path=$alts_sd_volume result=FAIL error=$alts_sd_msg"
    return 1
}

altscreen_sd_try_mount_rw() {
    alts_sd_volume=$1
    if [ "${ALTSCREEN_CHAIN_TESTING:-0}" = 1 ]; then
        echo "SD_REMOUNT_RW scope=TEST path=$alts_sd_volume result=SKIP"
        return 0
    fi
    echo "SD_REMOUNT_RW path=$alts_sd_volume method=direct"
    mount -uw "$alts_sd_volume" >/dev/null 2>&1 || true
    if command -v on >/dev/null 2>&1 && [ -x /bin/mount ]; then
        echo "SD_REMOUNT_RW path=$alts_sd_volume method=mmx_fallback"
        on -f mmx /bin/mount -uw "$alts_sd_volume" >/dev/null 2>&1 || true
    fi
    return 0
}

altscreen_sd_ensure_writable() {
    alts_sd_volume=$1
    alts_sd_scope=${2:-SD}
    if altscreen_sd_probe_write "$alts_sd_volume" "$alts_sd_scope" INITIAL; then
        echo "SD_WRITE=PASS scope=$alts_sd_scope volume=$alts_sd_volume already_writable=1"
        return 0
    fi
    altscreen_sd_try_mount_rw "$alts_sd_volume"
    if altscreen_sd_probe_write "$alts_sd_volume" "$alts_sd_scope" AFTER_REMOUNT; then
        echo "SD_WRITE=PASS scope=$alts_sd_scope volume=$alts_sd_volume after_remount=1"
        return 0
    fi
    echo "SD_WRITE=FAILED scope=$alts_sd_scope volume=$alts_sd_volume"
    return 1
}
