#!/bin/bash
# Host check of the fork's GEM scripts in AltScreen's testing mode
# (ALTSCREEN_CHAIN_TESTING=1, device root under /tmp). Run by scripts/run_checks.sh.
set -u
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
S="$PROJECT_DIR/Toolbox/scripts"
ROOT=$(mktemp -d /tmp/mmi-gem-check.XXXXXX)
trap 'rm -rf "$ROOT"' EXIT
export ALTSCREEN_CHAIN_TESTING=1 ALTSCREEN_CHAIN_ROOT="$ROOT" ALTSCREEN_CHAIN_VOLUME="$ROOT/sd"
STATE="$ROOT/mnt/app/root/carplay-altscreen/state"
fails=0

run() {   # run <expected rc> <expected output line> <script> [arg]
    local want_rc=$1 want=$2; shift 2
    out=$(sh "$@" 2>&1); rc=$?
    if [ "$rc" != "$want_rc" ] || ! printf '%s\n' "$out" | grep -Fq -- "$want"; then
        echo "FAIL: $* -> rc=$rc (want $want_rc), missing '$want'"; printf '%s\n' "$out" | sed 's/^/    /'
        fails=$((fails + 1))
    fi
}
expect_file() {   # expect_file <path> <content or "-" for absent>
    if [ "$2" = - ]; then
        [ ! -e "$1" ] || { echo "FAIL: $1 should be absent"; fails=$((fails + 1)); }
    else
        [ "$(cat "$1" 2>/dev/null)" = "$2" ] || { echo "FAIL: $1 = '$(cat "$1" 2>/dev/null)', want '$2'"; fails=$((fails + 1)); }
    fi
}

# Not installed: refuse and change nothing.
run 1 "CLUSTER_AREA=REFUSED requested=w1140 reason=RUNTIME_NOT_INSTALLED" "$S/cluster_area.sh" w1140
run 1 "CLUSTER_SHIFT=REFUSED requested=sporttest reason=RUNTIME_NOT_INSTALLED" "$S/cluster_shift.sh" sporttest
run 2 "usage" "$S/cluster_area.sh" w999
run 2 "usage" "$S/cluster_shift.sh" left

# Installed runtime (what INSTALL leaves behind).
mkdir -p "$STATE"
echo 'owner=MMI-Cockpit-Carplay' > "$ROOT/mnt/app/root/carplay-altscreen/.mmi-cockpit-carplay-runtime-owner"

run 0 "CLUSTER_AREA=w1140 width=1140" "$S/cluster_area.sh" w1140
expect_file "$STATE/cluster-view-width.txt" 1140
run 0 "CLUSTER_AREA=w1080 width=1080" "$S/cluster_area_w1080.sh"
expect_file "$STATE/cluster-view-width.txt" 1080
run 0 "CLUSTER_AREA=full width=1440" "$S/cluster_area_full.sh"
expect_file "$STATE/cluster-view-width.txt" -

# The card profile needs the 300..1140 safe area.
: > "$STATE/card.enabled"
run 1 "CLUSTER_AREA=REFUSED requested=w1080 reason=CARD_NEEDS_1140" "$S/cluster_area.sh" w1080
expect_file "$STATE/cluster-view-width.txt" -
run 0 "CLUSTER_AREA=w1200 width=1200" "$S/cluster_area_w1200.sh"
expect_file "$STATE/cluster-view-width.txt" 1200
rm -f "$STATE/card.enabled"

run 0 "CLUSTER_SHIFT=sporttest small_dx=-476" "$S/cluster_shift_sporttest.sh"
expect_file "$STATE/cluster-shift.txt" "small_dx=-476"
run 0 "CLUSTER_SHIFT=off none" "$S/cluster_shift_off.sh"
expect_file "$STATE/cluster-shift.txt" -

run 0 "CLUSTER_SAFE_BOTTOM=400 bottom=400" "$S/cluster_safe_bottom_400.sh"
expect_file "$STATE/cluster-safe-bottom.txt" 400
run 0 "CLUSTER_SAFE_BOTTOM=normal bottom=450" "$S/cluster_safe_bottom_normal.sh"
expect_file "$STATE/cluster-safe-bottom.txt" -
run 2 "usage" "$S/cluster_safe_bottom.sh" 300

run 0 "CARD_SMALL=hide applies=NEXT_VIEW" "$S/card_small_hide.sh"
expect_file "$STATE/card-small-hide" hide
run 0 "CARD_SMALL=keep applies=NEXT_VIEW" "$S/card_small_keep.sh"
expect_file "$STATE/card-small-hide" -

run 0 "RGI_BACKING=off applies=LIVE" "$S/rgi_backing_off.sh"
expect_file "$STATE/rgi-backing-off" off
run 0 "RGI_BACKING=on applies=LIVE" "$S/rgi_backing_on.sh"
expect_file "$STATE/rgi-backing-off" -
run 2 "usage" "$S/rgi_backing.sh" maybe

run 0 "VERBOSE_LOGS=on" "$S/rgi_verbose_on.sh"
expect_file "$ROOT/mnt/app/carplay_verbose" ""
run 0 "VERBOSE_LOGS=off" "$S/rgi_verbose_off.sh"
expect_file "$ROOT/mnt/app/carplay_verbose" -

# H.264 dump: no card, no ring, then a ring and the map display's last RUN line.
run 1 "H264_DUMP=FAILED reason=NO_SD" "$S/dump_cluster_h264.sh"
mkdir -p "$ROOT/sd/Toolbox"
run 1 "H264_DUMP=FAILED reason=NO_RING" "$S/dump_cluster_h264.sh"
mkdir -p "$ROOT/dev/shmem" "$ROOT/tmp"
printf 'ring-bytes' > "$ROOT/dev/shmem/carplay111_h264"
printf 'x\ndirect111: PHASE=RUN present_fps=29.80\n' > "$ROOT/tmp/altscreen_mirror.log"
run 0 "H264_DUMP=OK file=MMI-Cockpit-Carplay/logs/h264/cluster_h264_1.bin bytes=10" "$S/dump_cluster_h264.sh"
run 0 "cluster_h264_2.bin" "$S/dump_cluster_h264.sh"
expect_file "$ROOT/sd/MMI-Cockpit-Carplay/logs/h264/cluster_h264_1.mirror.txt" "direct111: PHASE=RUN present_fps=29.80"

# Wrappers fall back to the SD copy when run from elsewhere (as GEM may).
mkdir -p "$ROOT/elsewhere"; cp "$S/cluster_shift_off.sh" "$ROOT/elsewhere/"
grep -q '/Toolbox/scripts/cluster_shift.sh' "$ROOT/elsewhere/cluster_shift_off.sh" || { echo "FAIL: wrapper has no SD fallback"; fails=$((fails + 1)); }

[ "$fails" = 0 ] || { echo "GEM_SCRIPTS_CHECK=FAIL ($fails)"; exit 1; }
echo "GEM_SCRIPTS_CHECK=PASS"
