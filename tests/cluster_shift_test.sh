#!/bin/bash
# GEM "Cluster map shift" scripts: each choice writes / removes cluster_shift.cfg.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/altscreen/Toolbox/scripts"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export ALTSCREEN_CHAIN_TESTING=1 ALTSCREEN_CHAIN_ROOT="$T"
CFG="$T/mnt/app/root/hooks/cluster_shift.cfg"
! sh "$S/cluster_shift_left120.sh" >/dev/null || { echo "FAIL: wrote without /mnt/app/root/hooks"; exit 1; }
mkdir -p "$T/mnt/app/root/hooks"
sh "$S/cluster_shift_left120.sh" >/dev/null && [ "$(cat "$CFG")" = "full_dx=-120" ] || { echo "FAIL left120"; exit 1; }
sh "$S/cluster_shift_left240.sh" >/dev/null && [ "$(cat "$CFG")" = "full_dx=-240" ] || { echo "FAIL left240"; exit 1; }
sh "$S/cluster_shift_sporttest.sh" >/dev/null && [ "$(cat "$CFG")" = "small_dx=-476" ] || { echo "FAIL sporttest"; exit 1; }
sh "$S/cluster_shift_off.sh" >/dev/null && [ ! -e "$CFG" ] || { echo "FAIL off"; exit 1; }
! sh "$S/cluster_shift.sh" bogus >/dev/null || { echo "FAIL: bad choice accepted"; exit 1; }
echo "cluster_shift_test: left120/left240/sporttest/off PASS"
