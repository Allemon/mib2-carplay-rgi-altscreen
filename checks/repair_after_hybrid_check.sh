#!/bin/bash
# Host check of Toolbox/scripts/repair_after_hybrid.sh on synthetic files (run by scripts/run_checks.sh).
set -u
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
S="$PROJECT_DIR/Toolbox/scripts/repair_after_hybrid.sh"
T=$(mktemp -d /tmp/mmi-repair-check.XXXXXX)
trap 'rm -rf "$T"' EXIT
export ALTSCREEN_CHAIN_TESTING=1 ALTSCREEN_CHAIN_ROOT="$T" ALTSCREEN_CHAIN_VOLUME="$T/sd"
fails=0
check(){ if eval "$2"; then :; else echo "FAIL: $1"; fails=$((fails + 1)); fi; }
P="$T/mnt/system/etc/eso/production"; R="$T/sd/MMI-Cockpit-Carplay/repair-after-hybrid"
mkdir -p "$P" "$T/mnt/system/etc/boot" "$T/mnt/app/root/hooks" "$T/mnt/app/root/carplay-altscreen/lib" "$T/mnt/app/eso/hmi/lsd/jars" "$R"
printf '{"carplay":{"exec":"carplay_startup.sh","path":"/mnt/app/root/hooks","envs":["LD_PRELOAD=/mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so"]}}\n' > "$P/smartphone_integrator.json"
printf '"MessagesSentByAccessory":["0x5000", "0x5200"],\n' > "$P/dio_manager.json"
printf '#!/bin/sh\n# BEGIN ALT111 BASEVIDEO3 AUTOSTART\necho a\n# END ALT111 BASEVIDEO3 AUTOSTART\necho keep\n# BEGIN ALTSCREEN DIAGNOSTICS\necho d\n# END ALTSCREEN DIAGNOSTICS\n' > "$T/mnt/system/etc/boot/startup.sh"
echo x > "$T/mnt/app/root/hooks/libcarplay_hook.so"; echo x > "$T/mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so"; echo j > "$T/mnt/app/eso/hmi/lsd/jars/carplay_hook.jar"
printf '{"carplay":{"exec":"dio_manager","path":"/mnt/app/eso/bin/apps","envs":[]}}\n' > "$R/smartphone_integrator.json"
printf '"MessagesSentByAccessory":["0x5000"],\n' > "$R/dio_manager.json"
s(){ set -- $(cksum < "$1"); echo "$1 $2"; }
{ echo "live smartphone_integrator.json $(s "$P/smartphone_integrator.json")"; echo "live dio_manager.json $(s "$P/dio_manager.json")"
  echo "target smartphone_integrator.json $(s "$R/smartphone_integrator.json")"; echo "target dio_manager.json $(s "$R/dio_manager.json")"; } > "$R/expected.txt"

out=$(sh "$S" 2>&1); rc=$?
check "repair passes" '[ $rc = 0 ] && printf "%s" "$out" | grep -q REPAIR_AFTER_HYBRID=PASS'
check "SI is the target" 'cmp -s "$P/smartphone_integrator.json" "$R/smartphone_integrator.json"'
check "dio is the target" 'cmp -s "$P/dio_manager.json" "$R/dio_manager.json"'
check "startup keeps the rest" '[ "$(cat "$T/mnt/system/etc/boot/startup.sh")" = "$(printf "#!/bin/sh\necho keep")" ]'
check "leftovers gone" '[ ! -e "$T/mnt/app/root/hooks" ] && [ ! -e "$T/mnt/app/root/carplay-altscreen" ] && [ ! -e "$T/mnt/app/eso/hmi/lsd/jars/carplay_hook.jar" ]'
check "leftovers saved" '[ -f "$R/before_1/hooks/libcarplay_hook.so" ] && [ -f "$R/before_1/carplay_hook.jar" ] && [ -f "$R/before_1/startup.sh" ]'
out=$(sh "$S" 2>&1); rc=$?
check "second run is a no-op pass" '[ $rc = 0 ] && printf "%s" "$out" | grep -q "SI=ALREADY_STOCK"'
echo '{}' > "$P/dio_manager.json"; cp "$P/smartphone_integrator.json" "$T/si.before"
out=$(sh "$S" 2>&1); rc=$?
check "unknown state refused" '[ $rc = 1 ] && printf "%s" "$out" | grep -q "not the expected state"'
echo damaged >> "$R/dio_manager.json"
out=$(sh "$S" 2>&1); rc=$?
check "damaged repair files refused" '[ $rc = 1 ] && printf "%s" "$out" | grep -q "damaged"'
[ "$fails" = 0 ] || { echo "REPAIR_CHECK=FAIL ($fails)"; exit 1; }
echo "REPAIR_CHECK=PASS"
