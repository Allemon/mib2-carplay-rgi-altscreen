#!/usr/bin/env python3
"""Render the fork's startup clip, one of two styles:

  glow (default)  each ring is drawn by arc pieces that spin around it while they grow from a
                  dot into the closed ring, with faint ghost arcs and a bright white bloom, as the
                  whole badge zooms out to its final size; the bloom then settles to a soft halo
                  around crisp white rings and holds
  draw            four silver rings drawn on black while they settle from a slight zoom-in to
                  their final size, one soft light sheen, hold

Writes the map display's embedded clip format directly (see
Toolbox/carplay_alt_screen/mirror_display/tools/build_startup_logo.py and src/startup_logo.cpp):
b"ALTLOGO1" + <u32 width, height, fps, frames> + per frame <u32 length> + zlib(RGBA 1440x455).
The map display shows it 1:1 on the cluster plane, then fades it into the CarPlay map (550 ms).

  python3 Tools/make_startup_rings.py Toolbox/carplay_alt_screen/mirror_display/assets/logo_frames.z
  python3 Tools/make_startup_rings.py out.z --preview rings.mp4      # also an H.264 preview (ffmpeg)
  python3 Tools/make_startup_rings.py out.z --style draw
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


# ---- glow style ----------------------------------------------------------------------------
GLOW_DURATION = 3.2
GLOW_STROKE = 6.0          # thinner white line, like the reference
SEGMENTS = 3               # arc pieces per ring
FLY_START, FLY_SPREAD, FLY_TIME = 0.05, 0.45, 1.25   # first piece, last start offset, sweep time
FLY_SPIN = 3.6                                       # rad a piece spins clockwise into place
GLOW_ZOOM_FROM, GLOW_ZOOM_TIME = 1.55, 2.2
BLOOM_PEAK_T, BLOOM_PEAK, BLOOM_REST = 1.15, 1.7, 0.45  # bloom gain: peaks as pieces lock, settles
GHOST_GAIN, GHOST_FADE = 0.28, 1.7                   # faint trailing arcs, gone by GHOST_FADE s


def box_blur(img, radius, passes=3):
    """Separable box blur repeated 3x (close to a Gaussian), edges clamped."""
    out = img
    for _ in range(passes):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            c = np.cumsum(np.pad(out, pad, mode="edge"), axis=axis, dtype=np.float32)
            n = out.shape[axis]
            hi = np.take(c, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
            lo = np.take(c, np.arange(0, n), axis=axis)
            out = (hi - lo) / (2 * radius + 1)
    return out


def render_glow():
    rng = np.random.default_rng(7)   # fixed seed: the same clip on every build
    cx0 = WIDTH / 2.0
    centers = [cx0 + (i - 1.5) * PITCH for i in range(4)]
    left, right = int(cx0 - 420), int(cx0 + 420)
    top, bottom = 0, HEIGHT
    xs = np.arange(left, right, dtype=np.float32)[None, :] + 0.5
    ys = np.arange(top, bottom, dtype=np.float32)[:, None] + 0.5
    seg_arc = 2.0 * np.pi / SEGMENTS

    pieces = []   # (ring centre x, arc start angle, start offset dx, dy, spin, start time)
    for i, cx in enumerate(centers):
        base = rng.uniform(0.0, 2.0 * np.pi)
        for k in range(SEGMENTS):
            # The pieces stay on their ring and spin into place (no flight).
            pieces.append((cx, base + k * seg_arc, 0.0, 0.0,
                           -FLY_SPIN * rng.uniform(0.75, 1.0),
                           FLY_START + rng.uniform(0.0, FLY_SPREAD)))

    def arc_alpha(u, v, cx, cy, a0, length, radius, stroke, scale):
        dx, dy = u - cx, v - cy
        dist = np.sqrt(dx * dx + dy * dy)
        band = np.clip((stroke / 2.0 - np.abs(dist - radius)) * scale + 0.5, 0.0, 1.0)
        if length >= 2.0 * np.pi - 1e-6:
            return band
        rel = np.mod(np.arctan2(dx, -dy) - a0, 2.0 * np.pi)   # angle into the arc, clockwise
        edge = np.minimum(rel, length - rel) * radius * scale  # px from the nearer arc end
        return band * np.clip(edge / 1.2 + 0.5, 0.0, 1.0) * (rel <= length)

    frames = []
    count = int(round(GLOW_DURATION * FPS))
    for n in range(count):
        t = n / FPS
        scale = 1.0 + (GLOW_ZOOM_FROM - 1.0) * (1.0 - ease_out_cubic(t / GLOW_ZOOM_TIME))
        u = (xs - cx0) / scale + cx0
        v = (ys - CENTER_Y) / scale + CENTER_Y
        line = np.zeros((bottom - top, right - left), dtype=np.float32)
        ghost = np.zeros_like(line)
        locked = [all(t >= q[5] + FLY_TIME for q in pieces if q[0] == cx) for cx in centers]
        for cx, done in zip(centers, locked):
            if done:   # every piece home: one seamless ring, no joints
                line = np.maximum(line, arc_alpha(u, v, cx, CENTER_Y, 0.0, 2.0 * np.pi,
                                                  RADIUS, GLOW_STROKE, scale))
        for cx, a0, ox, oy, spin, t0 in pieces:
            p = (t - t0) / FLY_TIME
            if p <= 0.0 or locked[centers.index(cx)]:
                continue
            e = ease_out_cubic(p)
            k = 1.0 - e                                   # 1 far away .. 0 locked in place
            length = seg_arc * (0.04 + 0.96 * e)          # a dot growing into its third
            appear = np.clip(p * 6.0, 0.0, 1.0)           # pieces fade in as they start
            line = np.maximum(line, appear * arc_alpha(
                u, v, cx + ox * k, CENTER_Y + oy * k, a0 + spin * k, length,
                RADIUS, GLOW_STROKE, scale))
            if t < GHOST_FADE and k > 0.02:
                # A thin ghost a little outside, lagging behind the piece.
                kk = min(1.0, k * 1.35)
                ghost = np.maximum(ghost, arc_alpha(
                    u, v, cx, CENTER_Y, a0 + spin * kk + 0.35, length * 0.7,
                    RADIUS + 5.0, 1.6, scale) * (1.0 - t / GHOST_FADE))
        # Bloom gain: rises with the flight, peaks as the pieces lock, settles to a soft halo.
        if t < BLOOM_PEAK_T:
            gain = BLOOM_PEAK * ease_in_out(t / BLOOM_PEAK_T)
        else:
            gain = BLOOM_REST + (BLOOM_PEAK - BLOOM_REST) * (1.0 - ease_out_cubic((t - BLOOM_PEAK_T) / 1.2))
        bloom = 0.8 * box_blur(line, 4) + 0.6 * box_blur(line, 14)
        value = np.clip(line + GHOST_GAIN * ghost + gain * bloom, 0.0, 1.0)
        frame = np.zeros((HEIGHT, WIDTH, 4), dtype=np.uint8)
        frame[..., 3] = 255
        frame[top:bottom, left:right, :3] = np.round(value * 255.0).astype(np.uint8)[..., None]
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
    parser.add_argument("--style", choices=("glow", "draw"), default="glow")
    args = parser.parse_args()
    clip = render_glow() if args.style == "glow" else render()
    write_clip(clip, args.output)
    if args.preview:
        write_preview(clip, args.preview)
    print(f"frames={len(clip)} fps={FPS} size={WIDTH}x{HEIGHT}")
