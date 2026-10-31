#!/usr/bin/env python3
"""Validate the complete embedded clip directly from the QNX ELF's symbols."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(binary: Path, asset=None):
    elf = binary.read_bytes()
    assert elf[:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32"
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
    assert header[2] == 40, "expected ARM architecture"
    assert b"/usr/lib/ldqnx.so.2\0" in elf, "expected QNX loader"
    sections = [struct.unpack_from("<IIIIIIIIII", elf, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings_section = sections[section[6]]
        strings = elf[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", elf, offset)
            end = strings.find(b"\0", name)
            symbols[strings[name:end].decode("ascii", errors="replace")] = (value, index)
    start, section_index = symbols["altscreen_logo_data"]
    end, end_index = symbols["altscreen_logo_end"]
    assert section_index == end_index and end > start
    section = sections[section_index]
    assert section[2] & 2 and not section[2] & 1, "clip must be allocated read-only data"
    offset = section[4] + start - section[3]
    clip = elf[offset:offset + end - start]
    assert clip[:8] == b"ALTLOGO1"
    width, height, fps, frames = struct.unpack_from("<IIII", clip, 8)
    assert (width, height, fps) == (1440, 455, 30) and 0 < frames <= 300
    position = 24
    decoded_hash = hashlib.sha256()
    for i in range(frames):
        compressed_size, = struct.unpack_from("<I", clip, position)
        position += 4
        assert 0 < compressed_size <= width * height * 4
        frame = zlib.decompress(clip[position:position + compressed_size])
        assert len(frame) == width * height * 4, f"invalid frame {i}"
        assert frame[3::4] == b"\xff" * (width * height), f"non-opaque frame {i}"
        decoded_hash.update(frame)
        position += compressed_size
    assert position == len(clip), "clip extent differs from frame records"
    if asset:
        assert clip == Path(asset).read_bytes(), "ELF clip differs from the generated asset"
    for marker in (b"PHASE=STARTUP_LOGO_BEGIN asset=EMBEDDED", b"PHASE=STARTUP_LOGO_END",
                   b"sidecar_abnormal_restart", b"glBlendFuncSeparate"):
        assert marker in elf, marker
    return {"binary_sha256": digest(elf), "binary_bytes": len(elf),
            "asset_sha256": digest(clip), "asset_bytes": len(clip),
            "decoded_frames_sha256": decoded_hash.hexdigest(),
            "width": width, "height": height, "fps": fps, "frames": frames,
            "duration_ms": round(frames * 1000 / fps), "fade_ms": 550,
            "embedding": "ELF_READ_ONLY_SECTION", "frame_alpha": "OPAQUE"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--asset", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.binary, args.asset), indent=2))
    print("EMBEDDED_LOGO_VERIFY=PASS")
