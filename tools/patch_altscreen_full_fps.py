#!/usr/bin/env python3
"""Let AltScreen publish every decoded cluster frame instead of every second one.

libcarplay_altscreen.so's CScreenRender::render interposer hands each frame the stock
decoder renders for stream 111 to p111_frame_tap_write_window(), which reads the window
back (screen_read_window, NV12) into /carplay111_decoded for the mirror sidecar.  That
function starts with

    14b74  ldr  r0, [r9, #0x40]     ; requests
    14b78  tst  r0, #1
    14b7c  add  r1, r0, #1
    14b80  str  r1, [r9, #0x40]
    14b84  bne  0x14efc             ; odd request -> return "ok" without a readback

so the iPhone's 30 fps reach the cluster as 15.  Car logs show the readback costs
p50 3 ms / p95 3-7 ms / max 15 ms (FRAME_LINEARIZER_PROGRESS requests=5999 readbacks=3000),
so doing all of them fits the 33 ms frame budget.  The patch turns that bne into a nop.

  tools/patch_altscreen_full_fps.py <in.so> <out.so>
  tools/patch_altscreen_full_fps.py --check <file.so>   # prints stock|patched, rc 1 if unknown
"""
import hashlib
import sys

STOCK_SHA256 = "2f5ba1a4f3dd2496539dcdff7bafc7a60a24bae5acb96a7ba6bb34cd323c9cfb"
OFFSET = 0x14B84                          # file offset == vaddr (first PT_LOAD at 0)
STOCK = bytes.fromhex("dc00001a")         # bne 0x14efc
CONTEXT = bytes.fromhex("010010e3" "011080e2" "401089e5")  # tst r0,#1; add r1,r0,#1; str r1,[r9,#0x40]
NOP = bytes.fromhex("00f020e3")           # nop (ARMv7)


def state(blob):
    if blob[OFFSET - len(CONTEXT):OFFSET] != CONTEXT:
        return "unknown"
    word = blob[OFFSET:OFFSET + 4]
    if word == STOCK and hashlib.sha256(blob).hexdigest() == STOCK_SHA256:
        return "stock"
    if word == NOP and hashlib.sha256(blob[:OFFSET] + STOCK + blob[OFFSET + 4:]).hexdigest() == STOCK_SHA256:
        return "patched"
    return "unknown"


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        s = state(open(argv[2], "rb").read())
        print(s)
        return 0 if s != "unknown" else 1
    if len(argv) != 3:
        print("usage: patch_altscreen_full_fps.py <in.so> <out.so> | --check <file.so>", file=sys.stderr)
        return 2
    blob = bytearray(open(argv[1], "rb").read())
    s = state(blob)
    if s == "unknown":
        print(f"ERROR: {argv[1]} is not the AltScreen build this patch was made for "
              f"(sha256 {hashlib.sha256(blob).hexdigest()})", file=sys.stderr)
        return 1
    blob[OFFSET:OFFSET + 4] = NOP
    with open(argv[2], "wb") as f:
        f.write(blob)
    print(f"{argv[2]}: frame tap publishes every frame ({'already patched' if s == 'patched' else 'patched'})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
