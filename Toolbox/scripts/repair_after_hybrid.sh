#!/bin/sh
PATH=${PATH:+$PATH:}/proc/boot:/armle/bin:/armle/scripts:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/eso/bin:/eso/bin/apps
export PATH
# GEM (MMI-Cockpit-Carplay fork): "REPAIR CONFIG AFTER HYBRID".
#
# The luka + AltScreen hybrid's RESTORE ORIGINAL could bring back a "stock" backup that was
# taken over an earlier install: smartphone_integrator.json then starts CarPlay through
# /mnt/app/root/hooks/carplay_startup.sh, which that same RESTORE deleted, so CarPlay no
# longer starts, and V3.8's INSTALL refuses the leftovers (project/legacy configuration).
#
# This puts the head unit back to stock from files prepared on the SD, and only from one
# known state to another:
#   MMI-Cockpit-Carplay/repair-after-hybrid/smartphone_integrator.json   stock target
#   MMI-Cockpit-Carplay/repair-after-hybrid/dio_manager.json             stock target
#   MMI-Cockpit-Carplay/repair-after-hybrid/expected.txt                 cksum/size lines:
#       live smartphone_integrator.json <sum> <size>   (the broken state it may replace)
#       live dio_manager.json <sum> <size>
#       target smartphone_integrator.json <sum> <size> (the files above)
#       target dio_manager.json <sum> <size>
# A live file that is neither the expected broken state nor already the target stops the
# repair before anything is written. Every file it changes or removes is copied first to
# MMI-Cockpit-Carplay/repair-after-hybrid/before_<n>/. It also removes the hybrid's leftovers
# (/mnt/app/root/hooks files, an unowned /mnt/app/root/carplay-altscreen, carplay_hook.jar,
# the verbose-log marker) and the ALT111 / ALTSCREEN DIAGNOSTICS blocks of startup.sh.
set -u
TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
ROOT=""
if [ "$TESTING" = 1 ]; then
    ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    VOLUME=${ALTSCREEN_CHAIN_VOLUME:-}
    case "$ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "RESULT: FAILED - invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
else
    VOLUME=""
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -d "$candidate/MMI-Cockpit-Carplay/repair-after-hybrid" ]; then VOLUME=$candidate; break; fi
    done
fi
p(){ printf '%s%s' "$ROOT" "$1"; }
SI=$(p /mnt/system/etc/eso/production/smartphone_integrator.json)
DIO=$(p /mnt/system/etc/eso/production/dio_manager.json)
HOOKS=$(p /mnt/app/root/hooks)
RUNTIME=$(p /mnt/app/root/carplay-altscreen)
JAR=$(p /mnt/app/eso/hmi/lsd/jars/carplay_hook.jar)
VERBOSE=$(p /mnt/app/carplay_verbose)
HOOK_FILES="libcarplay_hook.so maneuver_render flag_atlas.rgba carplay_startup.sh carplay_monitor.sh carplay_processes.sh carplay_cleanup.sh cluster_ui.url cluster_fps cluster_shift.cfg cluster_viewarea.cfg"

result(){
    echo "=============================================="
    echo " RESULT: $1"
    shift
    for line in "$@"; do echo " $line"; done
    echo "=============================================="
}
fail(){ result FAILED "$@"; exit 1; }
sum_of(){ set -- $(cksum < "$1" 2>/dev/null); [ $# -ge 2 ] && echo "$1 $2"; }
expected(){ sed -n "s/^$1 $2 //p" "$EXPECTED" | head -n 1; }
mount_rw(){ [ "$TESTING" = 1 ] || mount -uw "$1" >/dev/null 2>&1; }
mount_ro(){ [ "$TESTING" = 1 ] || mount -ur "$1" >/dev/null 2>&1 || true; }

[ -n "$VOLUME" ] || fail "No SD card with MMI-Cockpit-Carplay/repair-after-hybrid." "Nothing was changed."
REPAIR="$VOLUME/MMI-Cockpit-Carplay/repair-after-hybrid"
EXPECTED="$REPAIR/expected.txt"
NEW_SI="$REPAIR/smartphone_integrator.json"
NEW_DIO="$REPAIR/dio_manager.json"
for f in "$EXPECTED" "$NEW_SI" "$NEW_DIO"; do
    [ -s "$f" ] || fail "Missing ${f##*/} in repair-after-hybrid." "Nothing was changed."
done
echo "REPAIR_VOLUME=$VOLUME"

# 1. The prepared targets: intact, stock carplay child, no project entries.
[ "$(sum_of "$NEW_SI")" = "$(expected target smartphone_integrator.json)" ] &&
[ "$(sum_of "$NEW_DIO")" = "$(expected target dio_manager.json)" ] ||
    fail "The repair files on the SD are damaged." "Nothing was changed."
grep -q '"exec":"dio_manager"' "$NEW_SI" && grep -q '"path":"/mnt/app/eso/bin/apps"' "$NEW_SI" ||
    fail "The prepared smartphone_integrator.json does not" "start dio_manager. Nothing was changed."
if grep -Eq 'libcarplay_|carplay-altscreen|/root/hooks|libcp_mirror|libairplax' "$NEW_SI" ||
   grep -Eq '"0x520[0-4]"|"0x415[BCDF]"|"0x4160"|"0x417[01]"' "$NEW_DIO"; then
    fail "The prepared files still hold project entries." "Nothing was changed."
fi

# 2. The live files: the expected broken state, or already repaired.
write_si=0; write_dio=0
live=$(sum_of "$SI")
if [ "$live" = "$(expected target smartphone_integrator.json)" ]; then echo "SI=ALREADY_STOCK"
elif [ "$live" = "$(expected live smartphone_integrator.json)" ]; then write_si=1; echo "SI=EXPECTED_BROKEN $live"
else fail "smartphone_integrator.json on the head unit is" "not the expected state ($live)." "Nothing was changed."
fi
live=$(sum_of "$DIO")
if [ "$live" = "$(expected target dio_manager.json)" ]; then echo "DIO=ALREADY_STOCK"
elif [ "$live" = "$(expected live dio_manager.json)" ]; then write_dio=1; echo "DIO=EXPECTED_BROKEN $live"
else fail "dio_manager.json on the head unit is" "not the expected state ($live)." "Nothing was changed."
fi

# 3. startup.sh without the ALT111 autostart / ALTSCREEN DIAGNOSTICS blocks.
STARTUP=""
for rel in /mnt/system/etc/boot/startup.sh /etc/boot/startup.sh; do
    [ -f "$(p "$rel")" ] && { STARTUP=$(p "$rel"); break; }
done
CLEAN_STARTUP="$REPAIR/startup.clean.$$"
write_startup=0
if [ -n "$STARTUP" ]; then
    awk '
      { key=$0; sub(/\r$/, "", key); t=key; gsub(/^[ \t]+/, "", t); gsub(/[ \t]+$/, "", t)
        if (t ~ /^# BEGIN (ALT111 MIRROR AUTOSTART|ALT111 BASEVIDEO3 AUTOSTART|ALTSCREEN DIAGNOSTICS)$/) { if (block != "") bad=8; block=t; next }
        if (t ~ /^# END (ALT111 MIRROR AUTOSTART|ALT111 BASEVIDEO3 AUTOSTART|ALTSCREEN DIAGNOSTICS)$/) {
            b=t; sub(/^# END /, "# BEGIN ", b); if (block != b) bad=8; block=""; next }
        if (block == "") print }
      END { if (bad) exit bad; if (block != "") exit 9 }
    ' "$STARTUP" > "$CLEAN_STARTUP" || { rm -f "$CLEAN_STARTUP"; fail "startup.sh has unbalanced project blocks." "Nothing was changed."; }
    sh -n "$CLEAN_STARTUP" || { rm -f "$CLEAN_STARTUP"; fail "startup.sh would not parse without the blocks." "Nothing was changed."; }
    cmp -s "$CLEAN_STARTUP" "$STARTUP" || write_startup=1
fi
echo "STARTUP_BLOCKS=$([ "$write_startup" = 1 ] && echo PRESENT || echo NONE)"

# 4. Copy everything that will change or go away.
n=1
while [ -e "$REPAIR/before_$n" ]; do n=$((n + 1)); done
BEFORE="$REPAIR/before_$n"
mkdir -p "$BEFORE" || fail "Cannot write to the SD card." "Nothing was changed."
cp "$SI" "$BEFORE/smartphone_integrator.json" && cp "$DIO" "$BEFORE/dio_manager.json" ||
    fail "Cannot save the current files to the SD." "Nothing was changed."
[ -z "$STARTUP" ] || cp "$STARTUP" "$BEFORE/startup.sh" || fail "Cannot save startup.sh to the SD." "Nothing was changed."
[ ! -f "$JAR" ] || cp "$JAR" "$BEFORE/carplay_hook.jar" || fail "Cannot save carplay_hook.jar." "Nothing was changed."
for name in $HOOK_FILES; do
    [ -f "$HOOKS/$name" ] || continue
    mkdir -p "$BEFORE/hooks" && cp "$HOOKS/$name" "$BEFORE/hooks/$name" || fail "Cannot save $name." "Nothing was changed."
done
remove_runtime=0
if [ -d "$RUNTIME" ] && [ ! -f "$RUNTIME/.mmi-cockpit-carplay-runtime-owner" ]; then
    cp -R "$RUNTIME" "$BEFORE/carplay-altscreen" 2>/dev/null && remove_runtime=1 ||
        echo "WARN: cannot save $RUNTIME; it stays (V3.8 INSTALL may refuse it)"
fi
sync
echo "SAVED=MMI-Cockpit-Carplay/repair-after-hybrid/before_$n"

# 5. /mnt/system: publish each file through a temp copy; on any failure put the saved ones back.
publish(){   # publish <src> <dst>
    tmp="${2%/*}/.${2##*/}.repair.$$"
    cp "$1" "$tmp" && cmp -s "$1" "$tmp" && chmod 644 "$tmp" && mv -f "$tmp" "$2" && cmp -s "$1" "$2" && return 0
    rm -f "$tmp" 2>/dev/null
    return 1
}
rollback(){
    [ "$write_si" = 0 ] || publish "$BEFORE/smartphone_integrator.json" "$SI" || echo "ERROR: SI rollback failed"
    [ "$write_dio" = 0 ] || publish "$BEFORE/dio_manager.json" "$DIO" || echo "ERROR: DIO rollback failed"
    [ "$write_startup" = 0 ] || publish "$BEFORE/startup.sh" "$STARTUP" || echo "ERROR: startup.sh rollback failed"
    sync; mount_ro /mnt/system
}
if [ "$write_si$write_dio$write_startup" != 000 ]; then
    mount_rw /mnt/system || fail "Cannot make /mnt/system writable." "Nothing was changed."
    if { [ "$write_si" = 0 ] || publish "$NEW_SI" "$SI"; } &&
       { [ "$write_dio" = 0 ] || publish "$NEW_DIO" "$DIO"; } &&
       { [ "$write_startup" = 0 ] || publish "$CLEAN_STARTUP" "$STARTUP"; }; then
        [ "$write_startup" = 0 ] || chmod 755 "$STARTUP" 2>/dev/null
        sync; mount_ro /mnt/system
    else
        rollback; rm -f "$CLEAN_STARTUP"
        fail "Writing the system files failed." "The previous files were put back." "Do not reboot; keep the SD and send its logs."
    fi
fi
rm -f "$CLEAN_STARTUP"
echo "SYSTEM_FILES=REPAIRED si=$write_si dio=$write_dio startup=$write_startup"

# 6. /mnt/app leftovers (all saved above).
mount_rw /mnt/app || fail "System files are repaired, but /mnt/app" "is not writable for the leftovers." "Reboot, then run this entry again."
for name in $HOOK_FILES; do rm -f "$HOOKS/$name" 2>/dev/null; done
rmdir "$HOOKS" 2>/dev/null || true
rm -f "$JAR" "$VERBOSE" 2>/dev/null
[ "$remove_runtime" = 0 ] || rm -rf "$RUNTIME" 2>/dev/null
sync; mount_ro /mnt/app
left=""
[ ! -e "$JAR" ] || left="$left carplay_hook.jar"
[ ! -e "$HOOKS/carplay_startup.sh" ] || left="$left hooks"
[ "$remove_runtime" = 0 ] || [ ! -e "$RUNTIME" ] || left="$left carplay-altscreen"
[ -z "$left" ] || echo "WARN: still present:$left"
echo "APP_LEFTOVERS=REMOVED${left:+ except$left}"

# 7. What V3.8's pristine check also looks at, for the log only.
grep -Eq 'ALTSCREEN|ALT111|carplay0.*(49152|65535)' "$(p /mnt/system/etc/pf.conf)" 2>/dev/null &&
    echo "WARN: pf.conf still has AltScreen rules"

result SUCCESS "CarPlay configuration is stock again." \
    "Reboot the head unit, check that CarPlay works," \
    "then run INSTALL WITH RGI." \
    "Saved: repair-after-hybrid/before_$n"
echo "REPAIR_AFTER_HYBRID=PASS before=before_$n"
exit 0
