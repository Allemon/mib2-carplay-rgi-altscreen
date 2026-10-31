#!/usr/bin/env python3
"""Prepare MMI-Cockpit-Carplay/repair-after-hybrid/ for the GEM entry REPAIR CONFIG AFTER HYBRID.

  python3 Tools/prepare_repair_after_hybrid.py --broken <hybrid card>/MMI-Cockpit-Carplay/backup/ORIGINAL/files \\
      --clean <older card or copy>/MMI-Cockpit-Carplay/backup/ORIGINAL/files --out <SD>/MMI-Cockpit-Carplay/repair-after-hybrid

--broken  the backup the hybrid's RESTORE ORIGINAL wrote back: what the head unit has now
--clean   an older backup taken before the hybrid (an AltScreen-only install is fine: its own
          preload entry is removed here); it must start CarPlay with the stock dio_manager

The target files are the clean backup minus AltScreen preload entries; anything else that looks
like a project change stops the script. expected.txt pins the broken and the target files by
POSIX cksum/size, so the head-unit script only ever moves between those two states.
"""
import argparse
import os
import re
import subprocess
import sys

SI = "_mnt_system_etc_eso_production_smartphone_integrator.json"
DIO = "_mnt_system_etc_eso_production_dio_manager.json"
PRELOAD = re.compile(rb',\s*"(LD_PRELOAD|CARPLAY_PRELOAD_EXTRA)=/mnt/app/root/carplay-altscreen/lib/libcarplay_[A-Za-z0-9_]*\.so"')
PROJECT_SI = re.compile(rb'libcarplay_|carplay-altscreen|/root/hooks|libcp_mirror|libairplax')
PROJECT_DIO = re.compile(rb'"0x520[0-4]"|"0x415[BCDF]"|"0x4160"|"0x417[01]"')


def cksum(path):
    out = subprocess.run(["cksum"], stdin=open(path, "rb"), capture_output=True, check=True).stdout.split()
    return f"{out[0].decode()} {out[1].decode()}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--broken", required=True)
    ap.add_argument("--clean", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    si = open(os.path.join(a.clean, SI), "rb").read()
    dio = open(os.path.join(a.clean, DIO), "rb").read()
    si, removed = PRELOAD.subn(b"", si)
    if PROJECT_SI.search(si):
        sys.exit("clean smartphone_integrator.json still has project entries after removing "
                 f"{removed} AltScreen preload entries; use an older backup")
    if not re.search(rb'"exec"\s*:\s*"dio_manager"', si) or b"/mnt/app/eso/bin/apps" not in si:
        sys.exit("clean smartphone_integrator.json does not start the stock dio_manager")
    if PROJECT_DIO.search(dio):
        sys.exit("clean dio_manager.json has project message IDs; use an older backup")

    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, "smartphone_integrator.json"), "wb").write(si)
    open(os.path.join(a.out, "dio_manager.json"), "wb").write(dio)
    lines = [
        f"live smartphone_integrator.json {cksum(os.path.join(a.broken, SI))}",
        f"live dio_manager.json {cksum(os.path.join(a.broken, DIO))}",
        f"target smartphone_integrator.json {cksum(os.path.join(a.out, 'smartphone_integrator.json'))}",
        f"target dio_manager.json {cksum(os.path.join(a.out, 'dio_manager.json'))}",
    ]
    open(os.path.join(a.out, "expected.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"removed {removed} AltScreen preload entries -> {a.out}")


if __name__ == "__main__":
    main()
