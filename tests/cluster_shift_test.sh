#!/bin/bash
# GEM cluster scripts: "Cluster map area" writes / removes cluster_viewarea.cfg (view area
# from the left edge, safe area centred on x=720), "Cluster map shift" cluster_shift.cfg.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/altscreen/Toolbox/scripts"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export ALTSCREEN_CHAIN_TESTING=1 ALTSCREEN_CHAIN_ROOT="$T"
H="$T/mnt/app/root/hooks"
! sh "$S/cluster_area_w1140.sh" >/dev/null || { echo "FAIL: wrote without /mnt/app/root/hooks"; exit 1; }
mkdir -p "$H"
expect_area() {
    sh "$S/cluster_area_$1.sh" >/dev/null
    [ "$(cat "$H/cluster_viewarea.cfg")" = "$(printf 'view 0 0 %s 542\nsafe %s 0 %s 542' "$2" "$3" "$4")" ] \
        || { echo "FAIL area $1: $(cat "$H/cluster_viewarea.cfg")"; exit 1; }
}
expect_area w1200 1200 240 960
expect_area w1140 1140 300 840
expect_area w1080 1080 360 720
sh "$S/cluster_area_full.sh" >/dev/null && [ ! -e "$H/cluster_viewarea.cfg" ] || { echo "FAIL area full"; exit 1; }
sh "$S/cluster_shift_sporttest.sh" >/dev/null && [ "$(cat "$H/cluster_shift.cfg")" = "small_dx=-476" ] || { echo "FAIL sporttest"; exit 1; }
sh "$S/cluster_shift_off.sh" >/dev/null && [ ! -e "$H/cluster_shift.cfg" ] || { echo "FAIL shift off"; exit 1; }
! sh "$S/cluster_area.sh" bogus >/dev/null || { echo "FAIL: bad area accepted"; exit 1; }
! sh "$S/cluster_shift.sh" left120 >/dev/null || { echo "FAIL: removed shift accepted"; exit 1; }
echo "cluster_shift_test: map area 1200/1140/1080/full, Sport test/off PASS"
