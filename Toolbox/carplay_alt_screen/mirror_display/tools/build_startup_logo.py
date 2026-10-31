#!/usr/bin/env python3
"""Convert the supplied MP4 into bounded, independently compressed display frames.

The car has no FFmpeg dependency.  Only the top 455 lines of the 1440x542
source are visible on its displayable 3 plane, so the clipped bottom is omitted.
The kept 1440x455 frame is shown 1:1 on the whole plane (no scaling).
"""

import argparse
import hashlib
import struct
import subprocess
import zlib
from pathlib import Path


WIDTH = 1440
HEIGHT = 455
FPS = 30
FRAME_BYTES = WIDTH * HEIGHT * 4
MAGIC = b"ALTLOGO1"


def build(source: Path, output: Path) -> None:
    command = [
        "ffmpeg", "-v", "error", "-i", str(source),
        # Convert before cropping: on yuv420p input FFmpeg rounds an odd crop
        # height (455) down to the chroma grid (454).
        "-vf", f"fps={FPS},format=rgba,crop={WIDTH}:{HEIGHT}:0:0",
        "-pix_fmt", "rgba", "-f", "rawvideo", "pipe:1",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    count = 0
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with temporary.open("wb") as target:
            target.write(MAGIC + struct.pack("<IIII", WIDTH, HEIGHT, FPS, 0))
            while True:
                frame = process.stdout.read(FRAME_BYTES)
                if not frame:
                    break
                while len(frame) < FRAME_BYTES:
                    chunk = process.stdout.read(FRAME_BYTES - len(frame))
                    if not chunk:
                        raise ValueError("FFmpeg returned an incomplete frame")
                    frame += chunk
                compressed = zlib.compress(frame, 6)
                if len(compressed) > FRAME_BYTES:
                    raise ValueError("compressed frame exceeds the runtime bound")
                target.write(struct.pack("<I", len(compressed)))
                target.write(compressed)
                count += 1
                if count > 300:
                    raise ValueError("startup video exceeds the 10 second frame limit")
            target.seek(20)
            target.write(struct.pack("<I", count))
    except BaseException:
        process.kill()
        temporary.unlink(missing_ok=True)
        raise
    finally:
        process.stdout.close()
        return_code = process.wait()
    if return_code or not count:
        temporary.unlink(missing_ok=True)
        raise RuntimeError(f"FFmpeg failed or produced no frames (rc={return_code})")
    temporary.replace(output)
    print(f"frames={count} fps={FPS} size={output.stat().st_size} "
          f"source_sha256={hashlib.sha256(source.read_bytes()).hexdigest()} "
          f"asset_sha256={hashlib.sha256(output.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.source, args.output)
