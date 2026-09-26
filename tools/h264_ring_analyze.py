#!/usr/bin/env python3
"""Analyse an AltScreen /carplay111_h264 ring dump (Toolbox/scripts/dump_cluster_h264.sh).

Ring layout (libcarplay_altscreen.so write_h264_record_locked): a 0x44-byte header, then a
4 MiB data area of records {u32 magic 0x48323634, u32 seq, u32 len, u32 flags} + len payload
bytes (a wrap marker has flags 0x80000000).  Each payload is one AirPlay screen packet as
AltScreen fed it to the stock decoder: AVCC (4-byte big-endian NAL lengths).

The question it answers: are the ~30 packets/s the iPhone sends 30 pictures, or 15 pictures
split over two packets (slices of one picture), or 30 pictures of which half are
non-reference / fields?

  python3 tools/h264_ring_analyze.py cluster_h264_1.bin [--list N]
"""
import argparse
import collections
import struct
import sys

MAGIC = 0x48323634
HEADER = 0x44
DATA = 0x400000
NAL_NAMES = {1: "slice", 5: "IDR", 6: "SEI", 7: "SPS", 8: "PPS", 9: "AUD", 12: "filler"}
SLICE_TYPES = {0: "P", 1: "B", 2: "I", 3: "SP", 4: "SI"}


class Bits:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u(self, n):
        v = 0
        for _ in range(n):
            if self.p >= len(self.d) * 8:
                raise EOFError
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
            if zeros > 31:
                raise ValueError("bad exp-golomb")
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)


def rbsp(nal):
    out = bytearray()
    zeros = 0
    for b in nal[1:]:
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def parse_sps(nal):
    b = Bits(rbsp(nal))
    profile = b.u(8)
    b.u(16)                       # constraint flags + level
    b.ue()                        # sps id
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma = b.ue()
        if chroma == 3:
            b.u(1)
        b.ue(); b.ue(); b.u(1)
        if b.u(1):                # seq_scaling_matrix_present
            for i in range(8 if chroma != 3 else 12):
                if b.u(1):
                    last = nxt = 8
                    for _ in range(16 if i < 6 else 64):
                        if nxt:
                            nxt = (last + b.se() + 256) % 256
                        last = nxt or last
    sps = {"profile": profile, "log2_max_frame_num": b.ue() + 4}
    poc = b.ue()
    sps["poc_type"] = poc
    if poc == 0:
        sps["log2_max_poc_lsb"] = b.ue() + 4
    elif poc == 1:
        b.u(1); b.se(); b.se()
        for _ in range(b.ue()):
            b.se()
    sps["max_ref_frames"] = b.ue()
    b.u(1)
    sps["width_mbs"] = b.ue() + 1
    sps["height_map_units"] = b.ue() + 1
    sps["frame_mbs_only"] = b.u(1)
    return sps


def parse_slice(nal, sps):
    b = Bits(rbsp(nal))
    s = {"first_mb": b.ue(), "slice_type": b.ue() % 5, "pps": b.ue()}
    if sps:
        s["frame_num"] = b.u(sps["log2_max_frame_num"])
        if not sps["frame_mbs_only"]:
            s["field"] = b.u(1)
            if s["field"]:
                s["bottom"] = b.u(1)
        if (nal[0] & 0x1f) == 5:
            b.ue()                # idr_pic_id
        if sps["poc_type"] == 0:
            s["poc_lsb"] = b.u(sps["log2_max_poc_lsb"])
    return s


def nals_of(payload):
    """AVCC with 4-byte lengths; fall back to Annex-B start codes."""
    out, i = [], 0
    while i + 4 <= len(payload):
        n = struct.unpack(">I", payload[i:i + 4])[0]
        if n == 0 or i + 4 + n > len(payload):
            break
        out.append(payload[i + 4:i + 4 + n])
        i += 4 + n
    if out and i == len(payload):
        return out
    parts, j = [], 0
    while True:
        k = payload.find(b"\x00\x00\x01", j)
        if k < 0:
            break
        start = k + 3
        nxt = payload.find(b"\x00\x00\x01", start)
        end = len(payload) if nxt < 0 else (nxt - 1 if payload[nxt - 1] == 0 else nxt)
        parts.append(payload[start:end])
        j = start
    return parts


def records(blob):
    data = blob[HEADER:HEADER + DATA]
    found, pos = {}, 0
    magic = struct.pack("<I", MAGIC)
    while True:
        pos = data.find(magic, pos)
        if pos < 0 or pos + 16 > len(data):
            break
        _, seq, ln, flags = struct.unpack_from("<IIII", data, pos)
        if flags == 0x80000000 or ln == 0 or pos + 16 + ln > len(data) or ln > 0x3ffff0:
            pos += 4
            continue
        found.setdefault(seq, data[pos + 16:pos + 16 + ln])
        pos += 16 + ln
    return [found[s] for s in sorted(found)], sorted(found)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--list", type=int, default=40, help="packets to print in detail")
    a = ap.parse_args()
    blob = open(a.dump, "rb").read()
    if len(blob) < HEADER:
        sys.exit("file too small")
    wr, seq, total, recs, oversize, wraps = struct.unpack_from("<IIIIII", blob, 0x18)
    print(f"ring: write_off={wr} last_seq={seq} bytes_total={total} records={recs} "
          f"oversize_drops={oversize} wraps={wraps}")
    payloads, seqs = records(blob)
    if not payloads:
        sys.exit("no records found")
    gaps = sum(1 for x, y in zip(seqs, seqs[1:]) if y != x + 1)
    print(f"packets: {len(payloads)} (seq {seqs[0]}..{seqs[-1]}, {gaps} gaps)")

    sps = None
    for p in payloads:
        for n in nals_of(p):
            if n and (n[0] & 0x1f) == 7:
                try:
                    sps = parse_sps(n)
                except (EOFError, ValueError):
                    pass
    print("SPS:", sps if sps else "not in ring (frame_num/poc unavailable)")

    per_type = collections.Counter()
    new_pic = slices_total = nonref_pkts = 0
    pics_per_pkt = collections.Counter()
    rows = []
    for idx, p in enumerate(payloads):
        kinds, sl = [], []
        for n in nals_of(p):
            if not n:
                continue
            t, ref = n[0] & 0x1f, (n[0] >> 5) & 3
            per_type[t] += 1
            kinds.append(NAL_NAMES.get(t, str(t)))
            if t in (1, 5):
                try:
                    s = parse_slice(n, sps)
                except (EOFError, ValueError):
                    continue
                s["ref"] = ref
                sl.append(s)
        slices_total += len(sl)
        starts = sum(1 for s in sl if s["first_mb"] == 0)
        pics_per_pkt[starts] += 1
        new_pic += starts
        if sl and all(s["ref"] == 0 for s in sl):
            nonref_pkts += 1
        if idx < a.list:
            desc = ", ".join(
                f"{SLICE_TYPES.get(s['slice_type'], '?')} mb0={s['first_mb']} ref={s['ref']}"
                + (f" fn={s['frame_num']}" if "frame_num" in s else "")
                + (f" poc={s['poc_lsb']}" if "poc_lsb" in s else "")
                + (" field" if s.get("field") else "")
                for s in sl)
            rows.append(f"  #{seqs[idx]:>6} {len(p):>7}B  [{' '.join(kinds)}]  {desc}")
    print("NAL types:", {NAL_NAMES.get(k, k): v for k, v in sorted(per_type.items())})
    print(f"slices: {slices_total}  pictures started (first_mb==0): {new_pic}  "
          f"pictures per packet: {dict(sorted(pics_per_pkt.items()))}")
    print(f"packets whose slices are all non-reference (nal_ref_idc=0): {nonref_pkts}")
    ratio = new_pic / len(payloads) if payloads else 0
    print(f"=> {ratio:.2f} new pictures per packet")
    if ratio < 0.6:
        print("   one picture spans ~2 packets: the iPhone sends ~15 fps; the decoder is right.")
    elif nonref_pkts > len(payloads) * 0.4:
        print("   ~1 picture per packet, about half non-reference: 30 fps sent, a decoder/"
              "renderer that skips non-reference pictures halves it.")
    else:
        print("   ~1 picture per packet: the iPhone sends ~30 fps; the loss is after decode input.")
    if rows:
        print(f"first {len(rows)} packets:")
        print("\n".join(rows))


if __name__ == "__main__":
    main()
