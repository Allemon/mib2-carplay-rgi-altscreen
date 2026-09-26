#!/usr/bin/env python3
"""Binary patches that get AltScreen's CarPlay cluster video from 15 to ~30 fps.

libcarplay_altscreen.so (preload in dio_manager)
    Its CScreenRender::render interposer hands every frame the stock decoder renders for
    stream 111 to p111_frame_tap_write_window(), which reads the window back
    (screen_read_window, NV12) into /carplay111_decoded.  That function starts with

        14b78  tst  r0, #1          ; r0 = requests
        14b84  bne  0x14efc         ; odd request -> return "ok" without a readback

    so the iPhone's 30 fps (car dumps: frame_num +1 per packet) left as 15.  The readback
    costs p50 3 ms / p95 3-7 ms (FRAME_LINEARIZER_PROGRESS), so the bne becomes a nop.

carplay-alt111-mirror-display (sidecar that draws /carplay111_decoded on displayable 3)
    Its run loop is: read_frame(); if none -> usleep(20000); else present, then sleep
    until 33.3 ms after the loop started.  With 30 fps arriving with network jitter a frame
    that lands just after a check waits up to 20 ms, and the fixed 33.3 ms period cannot
    catch up after a late frame, so the cluster showed ~22 of 30 fps (RUN present_fps).
        102b14  usleep(20000) when no new frame      -> usleep(4000)
        102a6c  33.3 ms minimum loop period          -> 16.7 ms (one 60 Hz vsync)
        102b64  DECODED_SOURCE_STALL per missed poll -> not logged (floods at 4 ms polls;
        102be8  DECODED_SOURCE_RECOVERED per frame      the 250-miss STALL line stays)

  tools/patch_altscreen_fps.py <in> <out>      # patches whichever of the two files <in> is
  tools/patch_altscreen_fps.py --check <file>  # prints stock|patched, rc 1 if unknown
"""
import hashlib
import sys

NOP = "00f020e3"
TARGETS = {
    "libcarplay_altscreen.so": {
        "sha256": "2f5ba1a4f3dd2496539dcdff7bafc7a60a24bae5acb96a7ba6bb34cd323c9cfb",
        # file offset == vaddr (first PT_LOAD at 0); (offset, stock, patched) as LE words
        "patches": [(0x14B84, "dc00001a", NOP)],
    },
    "carplay-alt111-mirror-display": {
        "sha256": "a47eadd0091aa5e68ae26ced8fb73cfb041170d7470a7782086544703e05b490",
        # file offset == vaddr - 0x100000
        "patches": [
            (0x2B14, "200e04e3", "a00f00e3"),   # movw r0, #20000     -> #4000
            (0x2A6C, "341208e3", "191104e3"),   # movw r1, #33332     -> #16665
            (0x2A78, "820c62e2", "410c62e2"),   # rsb r0, r2, #33280  -> #16640
            (0x2A80, "350080e2", "1b0080e2"),   # add r0, r0, #53     -> #27
            (0x2B64, "b7faffeb", NOP),          # bl fprintf (STALL)
            (0x2BE8, "96faffeb", NOP),          # bl fprintf (RECOVERED)
        ],
    },
}


def apply(blob, patches, column):
    out = bytearray(blob)
    for p in patches:
        out[p[0]:p[0] + 4] = bytes.fromhex(p[column])
    return bytes(out)


def state(blob):
    """(target name, 'stock'|'patched') or (None, 'unknown')."""
    for name, t in TARGETS.items():
        for column, label in ((1, "stock"), (2, "patched")):
            if all(blob[p[0]:p[0] + 4] == bytes.fromhex(p[column]) for p in t["patches"]):
                stock = blob if column == 1 else apply(blob, t["patches"], 1)
                if hashlib.sha256(stock).hexdigest() == t["sha256"]:
                    return name, label
    return None, "unknown"


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        _, s = state(open(argv[2], "rb").read())
        print(s)
        return 0 if s != "unknown" else 1
    if len(argv) != 3:
        print("usage: patch_altscreen_fps.py <in> <out> | --check <file>", file=sys.stderr)
        return 2
    blob = open(argv[1], "rb").read()
    name, s = state(blob)
    if name is None:
        print(f"ERROR: {argv[1]} is not an AltScreen build these patches were made for "
              f"(sha256 {hashlib.sha256(blob).hexdigest()})", file=sys.stderr)
        return 1
    with open(argv[2], "wb") as f:
        f.write(apply(blob, TARGETS[name]["patches"], 2))
    print(f"{argv[2]}: {name} {'already patched' if s == 'patched' else 'patched'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
