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
A=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/a.bin"); B=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/b.bin"); C=$(python3 "$ROOT/tools/h264_ring_analyze.py" "$T/c.bin")
echo "$A" | grep -q "0.50 new pictures per packet" || { echo "FAIL two-slice"; echo "$A"; exit 1; }
echo "$B" | grep -q "1.00 new pictures per packet" && echo "$B" | grep -q "non-reference (nal_ref_idc=0): 0" || { echo "FAIL one-slice"; exit 1; }
echo "$C" | grep -q "half non-reference" || { echo "FAIL b-frames"; echo "$C"; exit 1; }
echo "h264_ring_analyze_test: two-slice pictures, one per packet, non-reference B-frames PASS"
