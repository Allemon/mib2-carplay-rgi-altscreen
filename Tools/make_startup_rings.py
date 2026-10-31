#!/usr/bin/env python3
"""Render the fork's startup clip: four rings drawn on black, one soft light sheen, hold.

Writes the map display's embedded clip format directly (see
Toolbox/carplay_alt_screen/mirror_display/tools/build_startup_logo.py and src/startup_logo.cpp):
b"ALTLOGO1" + <u32 width, height, fps, frames> + per frame <u32 length> + zlib(RGBA 1440x455).
The map display shows it 1:1 on the cluster plane, then fades it into the CarPlay map (550 ms).

  python3 Tools/make_startup_rings.py Toolbox/carplay_alt_screen/mirror_display/assets/logo_frames.z
  python3 Tools/make_startup_rings.py out.z --preview rings.mp4      # also an H.264 preview (ffmpeg)
"""

import argparse
import struct
import subprocess
import zlib

import numpy as np

WIDTH, HEIGHT, FPS = 1440, 455, 30
MAGIC = b"ALTLOGO1"
DURATION = 3.0

CENTER_Y = 228            # where the upstream logo sits: visible on Classic and Sport
RADIUS = 52.0             # ring centre line
STROKE = 8.5              # ring thickness
PITCH = 74.0              # distance between ring centres (rings overlap like the badge)
DRAW_START, DRAW_STAGGER, DRAW_TIME = 0.25, 0.15, 0.85
SHEEN_START, SHEEN_TIME, SHEEN_WIDTH = 1.45, 0.75, 70.0

# Brushed-silver vertical gradient (top light, bottom darker), linear RGB 0..1.
TOP = np.array([0.93, 0.94, 0.95])
BOTTOM = np.array([0.55, 0.57, 0.60])


def ease_out_cubic(t):
    t = np.clip(t, 0.0, 1.0)
    return 1.0 - (1.0 - t) ** 3


def ease_in_out(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def render():
    xs = np.arange(WIDTH, dtype=np.float32)[None, :] + 0.5
    ys = np.arange(HEIGHT, dtype=np.float32)[:, None] + 0.5
    centers = [WIDTH / 2.0 + (i - 1.5) * PITCH for i in range(4)]
    left = int(centers[0] - RADIUS - STROKE) - 4
    right = int(centers[-1] + RADIUS + STROKE) + 4
    top = int(CENTER_Y - RADIUS - STROKE) - 4
    bottom = int(CENTER_Y + RADIUS + STROKE) + 4
    bx, by = xs[:, left:right], ys[top:bottom, :]

    rings = []
    for cx in centers:
        dx, dy = bx - cx, by - CENTER_Y
        dist = np.sqrt(dx * dx + dy * dy)
        coverage = np.clip(STROKE / 2.0 + 0.5 - np.abs(dist - RADIUS), 0.0, 1.0)
        # Arc position clockwise from 12 o'clock, in pixels along the centre line.
        angle = np.mod(np.arctan2(dx, -dy), 2.0 * np.pi)
        rings.append((coverage, angle * RADIUS))
    circumference = 2.0 * np.pi * RADIUS

    shade = (by - (CENTER_Y - RADIUS - STROKE)) / (2.0 * (RADIUS + STROKE))
    shade = np.clip(shade, 0.0, 1.0)[..., None]
    base = TOP * (1.0 - shade) + BOTTOM * shade                      # (h, 1, 3)

    frames = []
    count = int(round(DURATION * FPS))
    for n in range(count):
        t = n / FPS
        alpha = np.zeros((bottom - top, right - left), dtype=np.float32)
        for i, (coverage, arc) in enumerate(rings):
            p = ease_out_cubic((t - DRAW_START - i * DRAW_STAGGER) / DRAW_TIME)
            if p <= 0.0:
                continue
            drawn = np.clip((p * circumference - arc) / 1.5 + 0.5, 0.0, 1.0) if p < 1.0 else 1.0
            alpha = np.maximum(alpha, coverage * drawn)
        s = ease_in_out((t - SHEEN_START) / SHEEN_TIME)
        sheen = 0.0
        if 0.0 < s < 1.0:
            x0 = left - SHEEN_WIDTH + s * (right - left + 2 * SHEEN_WIDTH)
            sheen = 0.55 * np.exp(-(((bx - x0) - (by - CENTER_Y) * 0.35) / SHEEN_WIDTH) ** 2 * 4.0)
            sheen = sheen[..., None]
        colour = np.clip(base + sheen, 0.0, 1.0)
        rgb = colour * alpha[..., None]
        frame = np.zeros((HEIGHT, WIDTH, 4), dtype=np.uint8)
        frame[..., 3] = 255
        frame[top:bottom, left:right, :3] = np.round(rgb * 255.0).astype(np.uint8)
        frames.append(frame.tobytes())
    return frames


def write_clip(frames, path):
    with open(path, "wb") as out:
        out.write(MAGIC + struct.pack("<IIII", WIDTH, HEIGHT, FPS, len(frames)))
        for frame in frames:
            packed = zlib.compress(frame, 6)
            out.write(struct.pack("<I", len(packed)))
            out.write(packed)


def write_preview(frames, path):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba",
                    "-s", f"{WIDTH}x{HEIGHT}", "-r", str(FPS), "-i", "-",
                    "-vf", "pad=iw:ih+1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "16", path],
                   input=b"".join(frames), check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--preview")
    args = parser.parse_args()
    clip = render()
    write_clip(clip, args.output)
    if args.preview:
        write_preview(clip, args.preview)
    print(f"frames={len(clip)} fps={FPS} size={WIDTH}x{HEIGHT}")
