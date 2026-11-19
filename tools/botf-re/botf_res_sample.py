#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Collect samples of one kind of file from Birth of the Federation's stbof.res.

Reads (never writes) stbof.res from the install folder and writes a folder
with, for working out a format (docs/engines/botf/formats/):

- report.txt: every matching file's size and its first bytes, as hex and
  text, and as 16-bit and 32-bit values
- <name>.head.bin: the first bytes of each matching file
- the files named with --whole, complete

Only standard-library Python 3.8+ is needed. For the text tables:

    python3 botf_res_sample.py "C:\\Program Files\\MicroProse\\Trek" --ext tdb \\
        --whole treaty.tdb minor6.tdb cdiplomt.tdb -o botf-tdb

For the data tables, all of them whole:

    python3 botf_res_sample.py "C:\\Program Files\\MicroProse\\Trek" \\
        --ext sst bst rst tec pst est bin wtf --all-whole -o botf-data

For the window layouts (about 470 KB), with the menus they name:

    python3 botf_res_sample.py "C:\\Program Files\\MicroProse\\Trek" \\
        --ext wdf mnu --all-whole -o botf-wdf
"""

import argparse
import struct
import sys
import zipfile
from pathlib import Path


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
    parser.add_argument("--ext", required=True, nargs="+", help="the extensions to sample, e.g. tdb")
    parser.add_argument("--whole", nargs="*", default=[], help="files to copy complete")
    parser.add_argument("--all-whole", action="store_true", help="copy every matching file complete")
    parser.add_argument("--head", type=int, default=2048, help="bytes to keep of every file")
    parser.add_argument("--show", type=int, default=256, help="bytes to show in the report")
    parser.add_argument("-o", "--out", default="botf-sample", help="output folder")
    args = parser.parse_args()

    res = find(args.install, "stbof.res")
    if res is None:
        sys.exit(f"no stbof.res in {args.install}")
    exts = tuple("." + e.lower().lstrip(".") for e in args.ext)
    whole = {w.lower() for w in args.whole}
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = []
    with zipfile.ZipFile(res) as z:
        infos = sorted((i for i in z.infolist() if i.filename.lower().endswith(exts)),
                       key=lambda i: i.filename.lower())
        for info in infos:
            data = z.read(info)
            name = info.filename.lower()
            (out / f"{name}.head.bin").write_bytes(data[:args.head])
            if args.all_whole or name in whole:
                (out / name).write_bytes(data)
                whole.discard(name)
            head = data[:args.show]
            report.append(f"== {info.filename}: {len(data):,} bytes")
            for k in range(0, len(head), 16):
                row = head[k:k + 16]
                text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
                report.append(f"   {k:04x}  {row.hex(' '):47}  {text}")
            n16 = len(head) // 2
            report.append("   u16: " + " ".join(str(v) for v in struct.unpack_from(f"<{n16}H", head)))
            n32 = len(head) // 4
            report.append("   u32: " + " ".join(str(v) for v in struct.unpack_from(f"<{n32}I", head)))
    for missing in sorted(whole):
        report.append(f"== {missing}: not in stbof.res")
    (out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"{len(infos)} {' '.join(exts)} files; wrote {len(list(out.iterdir()))} files to {out.resolve()}")
    print("Please zip the folder and share it.")


if __name__ == "__main__":
    main()
