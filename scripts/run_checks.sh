#!/bin/bash
# Host checks for the fork (no head unit, no QNX toolchain needed):
#   - every Toolbox script parses as sh, every GEM entry names a shipped script
#   - cluster_shift.h parser (checks/cluster_shift_check.cpp)
#   - the fork's GEM scripts in AltScreen's testing mode (checks/gem_scripts_check.sh)
#   - the universal preload sources compile for ARMv7 with -Werror (clang, no link)
set -uo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"
TMP=$(mktemp -d /tmp/mmi-checks.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

ok=1
for f in Toolbox/scripts/*.sh Toolbox/carplay_alt_screen/mirror_display/release/*.sh; do
    sh -n "$f" || { echo "  syntax: $f"; ok=0; }
done
for s in $(grep -o '/eso/hmi/engdefs/scripts/mqb/[A-Za-z0-9_]*\.sh' Toolbox/GEM/mqb-carplayAltScreen.esd | sort -u); do
    [ -f "Toolbox/scripts/${s##*/}" ] || { echo "  GEM names a missing script: ${s##*/}"; ok=0; }
done
[ "$ok" = 1 ] && pass "scripts parse, GEM entries resolve" || fail "scripts / GEM"

if c++ -std=c++98 -Wall -Wextra -Werror -I Toolbox/carplay_alt_screen/mirror_display/src \
        checks/cluster_shift_check.cpp -o "$TMP/cluster_shift_check" &&
   "$TMP/cluster_shift_check" "$TMP/cluster-shift.txt"; then
    pass "cluster_shift.h"
else
    fail "cluster_shift.h"
fi

bash checks/gem_scripts_check.sh && pass "GEM scripts" || fail "GEM scripts"
bash checks/repair_after_hybrid_check.sh && pass "repair after hybrid" || fail "repair after hybrid"
mkdir -p "$TMP/defaults-state"
if sh Toolbox/scripts/fork_defaults.sh Toolbox/carplay_alt_screen/fork-defaults.txt "$TMP/defaults-state" |
   grep -q '^FORK_DEFAULTS=DONE applied=[1-9][0-9]* skipped=0$'; then
    pass "fork-defaults.txt applies cleanly"
else
    fail "fork-defaults.txt has invalid lines"
fi

if command -v clang >/dev/null 2>&1; then
    ok=1
    CF=(-target armv7-linux-gnueabi -march=armv7-a -marm -mfloat-abi=softfp -fPIC -O2 -ffreestanding
        -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -Werror
        -isystem "$(clang -print-file-name=include)"
        -I Toolbox/carplay_alt_screen/src -I Toolbox/carplay_alt_screen/src/qnxshim)
    # The universal build compiles these (src/build_qnx_arm.sh); p1404_airplay.c is
    # included by p1404_airplay_fullchain.c.
    for c in altscreen_core altscreen_paths altscreen_profile altscreen_state altscreen_state_private \
             private111_direct_tap p1404_private111 p1404_private111_backend p1404_firewall \
             p1404_cockpit_native p1404_setup_merge p1404_resolve p1404_iap2 p1404_observe \
             p1404_airplay_fullchain aug22_dynamic_reloc aug22_dynamic_diag altscreen_hook; do
        clang "${CF[@]}" -c "Toolbox/carplay_alt_screen/src/$c.c" -o "$TMP/$c.o" || { echo "  compile: $c.c"; ok=0; }
    done
    [ "$ok" = 1 ] && pass "preload compiles for ARMv7 (-Werror)" || fail "preload compile"
else
    echo "SKIP preload compile (no clang)"
fi

[ "$fails" = 0 ] || { echo "CHECKS=FAIL ($fails)"; exit 1; }
echo "CHECKS=PASS"
