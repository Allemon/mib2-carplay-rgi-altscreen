#!/bin/bash
# tools/h264_ring_analyze.py against synthetic AltScreen rings built from x264 streams:
# two slices per picture in separate packets, one picture per packet, and B-frames.
# Needs ffmpeg + x264 (skipped otherwise).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v ffmpeg >/dev/null && command -v x264 >/dev/null || { echo "h264_ring_analyze_test: SKIP (no ffmpeg/x264)"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
ffmpeg -loglevel error -y -f lavfi -i testsrc=size=640x240:rate=30 -t 3 -pix_fmt yuv420p "$T/raw.y4m"
x264 --quiet --slices 2 --bframes 0 --keyint 60 -o "$T/a.264" "$T/raw.y4m" 2>/dev/null
x264 --quiet --slices 1 --bframes 0 --keyint 60 -o "$T/b.264" "$T/raw.y4m" 2>/dev/null
x264 --quiet --slices 1 --bframes 2 --b-pyramid none --keyint 60 -o "$T/c.264" "$T/raw.y4m" 2>/dev/null
python3 "$ROOT/tests/h264_ring_fixture.py" "$T/a.264" "$T/a.bin" per_slice >/dev/null
python3 "$ROOT/tests/h264_ring_fixture.py" "$T/b.264" "$T/b.bin" per_picture >/dev/null
python3 "$ROOT/tests/h264_ring_fixture.py" "$T/c.264" "$T/c.bin" per_picture >/dev/null
python3 "$ROOT/tests/h264_ring_fixture.py" "$T/b.264" "$T/d.bin" no_sps >/dev/null
A=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/a.bin"); B=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/b.bin"); C=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/c.bin")
echo "$A" | grep -q "0.50 new pictures per packet" || { echo "FAIL two-slice"; echo "$A"; exit 1; }
echo "$B" | grep -q "1.00 new pictures per packet" && echo "$B" | grep -q "non-reference (nal_ref_idc=0): 0" || { echo "FAIL one-slice"; exit 1; }
echo "$C" | grep -q "half non-reference" || { echo "FAIL b-frames"; echo "$C"; exit 1; }
# Without an SPS the frame_num width must be inferred as the real one (consecutive steps).
FN=$(echo "$B" | sed -n "s/^SPS: .*'log2_max_frame_num': \([0-9]*\).*/\1/p")
D=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/d.bin")
echo "$D" | grep -q "inferred from slice headers: {'log2_max_frame_num': $FN," \
    && echo "$D" | grep -Eq "frame_num steps between packets: \{1: [0-9]+(, [0-9]+: 1)?\}" \
    || { echo "FAIL no-sps (real log2_max_frame_num=$FN)"; echo "$D"; exit 1; }
# 30 fps patches: each stock binary -> patched, idempotent, anything else refused.
P="$ROOT/tools/patch_altscreen_fps.py"
A="$ROOT/altscreen/Toolbox/carplay_alt_screen"
for pair in "universal/libcarplay_altscreen.so 4" "mirror_display/release/carplay-alt111-mirror-display 24"; do
    set -- $pair; L="$A/$1"; changed=$2
    [ -s "$L" ] || continue
    [ "$(python3 "$P" --check "$L")" = stock ] || { echo "FAIL patch: $1 is not stock"; exit 1; }
    python3 "$P" "$L" "$T/p" >/dev/null && [ "$(python3 "$P" --check "$T/p")" = patched ] \
        && [ "$(cmp -l "$L" "$T/p" | wc -l | tr -d ' ')" -le "$changed" ] \
        && python3 "$P" "$T/p" "$T/p2" >/dev/null && cmp -s "$T/p" "$T/p2" \
        || { echo "FAIL patch: $1"; exit 1; }
done
! python3 "$P" "$T/d.bin" "$T/x" 2>/dev/null || { echo "FAIL patch accepted a foreign file"; exit 1; }
echo "h264_ring_analyze_test: two-slice pictures, one per packet, non-reference B-frames, no-SPS inference, 30 fps patches PASS"
