#!/usr/bin/env python3
"""Render the fork's startup clip: four rings drawn on black while they settle from a slight
zoom-in to their final size, one soft light sheen, hold.

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
ZOOM_FROM, ZOOM_TIME = 1.12, 2.4  # scale at t=0, eased down to 1.0 (no overshoot), then held

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
    cx0 = WIDTH / 2.0
    centers = [cx0 + (i - 1.5) * PITCH for i in range(4)]
    # Bounding box at the largest scale; everything is drawn in logo space (scale 1).
    half_w = (1.5 * PITCH + RADIUS + STROKE) * ZOOM_FROM + 4
    half_h = (RADIUS + STROKE) * ZOOM_FROM + 4
    left, right = int(cx0 - half_w), int(cx0 + half_w) + 1
    top, bottom = int(CENTER_Y - half_h), int(CENTER_Y + half_h) + 1
    bx, by = xs[:, left:right], ys[top:bottom, :]
    circumference = 2.0 * np.pi * RADIUS

    frames = []
    count = int(round(DURATION * FPS))
    for n in range(count):
        t = n / FPS
        scale = 1.0 + (ZOOM_FROM - 1.0) * (1.0 - ease_out_cubic(t / ZOOM_TIME))
        u = (bx - cx0) / scale + cx0                 # screen -> logo space
        v = (by - CENTER_Y) / scale + CENTER_Y
        alpha = np.zeros((bottom - top, right - left), dtype=np.float32)
        for i, cx in enumerate(centers):
            p = ease_out_cubic((t - DRAW_START - i * DRAW_STAGGER) / DRAW_TIME)
            if p <= 0.0:
                continue
            dx, dy = u - cx, v - CENTER_Y
            dist = np.sqrt(dx * dx + dy * dy)
            # Edge distance back in screen pixels keeps a one-pixel antialiased edge at any scale.
            coverage = np.clip((STROKE / 2.0 - np.abs(dist - RADIUS)) * scale + 0.5, 0.0, 1.0)
            if p < 1.0:
                # Arc position clockwise from 12 o'clock, in pixels along the centre line.
                arc = np.mod(np.arctan2(dx, -dy), 2.0 * np.pi) * RADIUS
                coverage = coverage * np.clip((p * circumference - arc) * scale / 1.5 + 0.5, 0.0, 1.0)
            alpha = np.maximum(alpha, coverage)
        shade = np.clip((v - (CENTER_Y - RADIUS - STROKE)) / (2.0 * (RADIUS + STROKE)), 0.0, 1.0)
        shade = np.broadcast_to(shade, alpha.shape)[..., None]
        colour = TOP * (1.0 - shade) + BOTTOM * shade
        s = ease_in_out((t - SHEEN_START) / SHEEN_TIME)
        if 0.0 < s < 1.0:
            lo, hi = centers[0] - RADIUS - STROKE, centers[-1] + RADIUS + STROKE
            x0 = lo - SHEEN_WIDTH + s * (hi - lo + 2 * SHEEN_WIDTH)
            sheen = 0.55 * np.exp(-(((u - x0) - (v - CENTER_Y) * 0.35) / SHEEN_WIDTH) ** 2 * 4.0)
            colour = np.clip(colour + sheen[..., None], 0.0, 1.0)
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
