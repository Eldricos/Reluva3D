#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Collect samples of Star Trek: Birth of the Federation's fonts (.fnt).

Reads (never writes) stbof.res from the install folder and writes a folder
with, for working out the font format (docs/engines/botf/formats/):

- report.txt: every .fnt file's size and its first 256 bytes, also as
  16-bit and 32-bit values
- <name>.head.bin: the first 2 KB of every font
- generic9.fnt and human15.fnt whole: two small fonts to study end to end

Only standard-library Python 3.8+ is needed:

    python3 botf_fonts.py "C:\\Program Files\\MicroProse\\Trek" -o botf-fonts
"""

import argparse
import struct
import sys
import zipfile
from pathlib import Path

WHOLE = ["generic9.fnt", "human15.fnt"]
HEAD_BYTES = 2048


def find(folder, name):
    """Case-insensitive lookup of `name` in `folder` and its ENGLISH folder."""
    for sub in ("", "english"):
        base = Path(folder)
        if sub:
            base = next((p for p in base.iterdir() if p.is_dir() and p.name.lower() == sub), None)
            if base is None:
                continue
        for p in base.iterdir():
            if p.is_file() and p.name.lower() == name:
                return p
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("install", help="the game's install folder")
    parser.add_argument("-o", "--out", default="botf-fonts", help="output folder")
    args = parser.parse_args()

    res = find(args.install, "stbof.res")
    if res is None:
        sys.exit(f"no stbof.res in {args.install}")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = []
    with zipfile.ZipFile(res) as z:
        fonts = sorted((i for i in z.infolist() if i.filename.lower().endswith(".fnt")),
                       key=lambda i: i.filename.lower())
        for info in fonts:
            data = z.read(info)
            name = info.filename.lower()
            (out / f"{name}.head.bin").write_bytes(data[:HEAD_BYTES])
            if name in WHOLE:
                (out / name).write_bytes(data)
            head = data[:256]
            report.append(f"== {info.filename}: {len(data):,} bytes")
            for k in range(0, len(head), 16):
                row = head[k:k + 16]
                text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
                report.append(f"   {k:04x}  {row.hex(' '):47}  {text}")
            n16 = len(head) // 2
            report.append("   u16: " + " ".join(str(v) for v in struct.unpack_from(f"<{n16}H", head)))
            n32 = len(head) // 4
            report.append("   u32: " + " ".join(str(v) for v in struct.unpack_from(f"<{n32}I", head)))
    (out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"Wrote {len(list(out.iterdir()))} files to {out.resolve()}")
    print("Please zip the folder and share it.")


if __name__ == "__main__":
    main()
