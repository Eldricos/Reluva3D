#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Collect small samples of Star Trek: Birth of the Federation's data files.

Reads (never writes) an install folder, and optionally the CD, and writes a
folder of samples for studying the formats (docs/engines/botf/README.md):

- report.txt: size, MD5 of the first 5000 bytes (for detection tables), and
  the archive type of each file found; for zip archives, the full listing of
  entries (names, sizes, compression), which contains no game content
- <file>.head.bin: the first bytes of each file
- <file>.tail.bin: the last bytes of each archive, where a zip keeps its
  directory

Only standard-library Python 3.8+ is needed:

    python3 botf_sample.py "C:\\Program Files\\MicroProse\\Trek" --cd D:\\ -o botf-samples
"""

import argparse
import hashlib
import os
import struct
import sys
import zipfile
from pathlib import Path

DETECTION_HASH_BYTES = 5000  # same as kDetectionHashBytes in common/detection.h
HEAD_BYTES = 8192
TAIL_BYTES = 4096
AVI_HEAD_BYTES = 4096

ARCHIVES = ["stbof.res", "alt.res", "english.snd", "sfx.snd", "music.snd"]
OTHERS = ["trek.exe", "trek.icd", "stbof.ini", "intro.wav", "lang.dat", "os.dat"]


def find(folder, name, want_dir=False):
    """Case-insensitive lookup of `name` (may contain one subfolder) in `folder`."""
    if folder is None:
        return None
    current = Path(folder)
    for part in name.split("/"):
        if not current.is_dir():
            return None
        matches = [p for p in current.iterdir() if p.name.lower() == part.lower()]
        if not matches:
            return None
        current = matches[0]
    return current if (current.is_dir() if want_dir else current.is_file()) else None


def read_range(path, offset, count):
    with open(path, "rb") as f:
        f.seek(offset)
        return f.read(count)


def describe_magic(head):
    if head[:4] == b"PK\x03\x04":
        return "zip (local file header)"
    if head[:4] == b"RIFF":
        return "RIFF " + head[8:12].decode("ascii", "replace")
    if head[:2] == b"MZ":
        return "DOS/Windows executable"
    if len(head) >= 16:
        return "unknown; first dwords " + " ".join(
            f"{v:08x}" for v in struct.unpack_from("<4I", head))
    return "unknown"


def avi_codecs(head):
    """Stream types and FourCCs from each 'strh' chunk in the AVI header."""
    found = []
    pos = head.find(b"strh")
    while pos >= 0 and pos + 16 <= len(head):
        kind = head[pos + 8:pos + 12].decode("ascii", "replace")
        handler = head[pos + 12:pos + 16].decode("ascii", "replace")
        found.append(f"{kind}/{handler!r}")
        pos = head.find(b"strh", pos + 4)
    pos = head.find(b"strf")
    while pos >= 0 and pos + 28 <= len(head):
        # For a video stream, strf is a BITMAPINFOHEADER: biCompression at +16.
        size = struct.unpack_from("<I", head, pos + 4)[0]
        if size >= 40:
            width, height = struct.unpack_from("<ii", head, pos + 12)
            bits = struct.unpack_from("<H", head, pos + 22)[0]
            comp = head[pos + 24:pos + 28].decode("ascii", "replace")
            found.append(f"video {width}x{height} {bits}bpp {comp!r}")
        pos = head.find(b"strf", pos + 4)
    return ", ".join(found) if found else "no stream headers in the first bytes"


def sample(path, label, out, report, tail=False, head_bytes=HEAD_BYTES, root=None):
    size = path.stat().st_size
    head = read_range(path, 0, head_bytes)
    detect = hashlib.md5(read_range(path, 0, DETECTION_HASH_BYTES)).hexdigest()
    safe = label.replace("/", "_")
    (out / f"{safe}.head.bin").write_bytes(head)
    report.append(f"== {label}")
    report.append(f"   name on disk: {path.relative_to(root) if root else path.name}")
    report.append(f"   size: {size:,}")
    report.append(f"   md5 of first {DETECTION_HASH_BYTES} bytes: {detect}")
    report.append(f"   type: {describe_magic(head)}")
    if tail and size > len(head):
        start = max(0, size - TAIL_BYTES)
        (out / f"{safe}.tail.bin").write_bytes(read_range(path, start, size - start))
        report.append(f"   tail: last {size - start} bytes from offset {start:,}")
    return head


def list_zip(path, report):
    try:
        with zipfile.ZipFile(path) as z:
            infos = z.infolist()
            methods = {}
            for i in infos:
                methods[i.compress_type] = methods.get(i.compress_type, 0) + 1
            report.append(f"   zip entries: {len(infos)}; compression methods "
                          f"{dict(sorted(methods.items()))} (0 stored, 8 deflate)")
            exts = {}
            for i in infos:
                ext = os.path.splitext(i.filename)[1].lower() or "(none)"
                exts[ext] = exts.get(ext, 0) + 1
            report.append("   by extension: " + ", ".join(
                f"{e} {n}" for e, n in sorted(exts.items(), key=lambda x: -x[1])))
            report.append("   entries (name, size, compressed size, method, crc):")
            for i in infos:
                report.append(f"     {i.filename}\t{i.file_size}\t{i.compress_size}"
                              f"\t{i.compress_type}\t{i.CRC:08x}")
    except zipfile.BadZipFile as e:
        report.append(f"   not readable as zip: {e}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("install", help="the game's install folder")
    parser.add_argument("--cd", help="the CD (or a copy of it), for music.snd and the movies")
    parser.add_argument("-o", "--out", default="botf-samples", help="output folder")
    args = parser.parse_args()

    if not Path(args.install).is_dir():
        sys.exit(f"not a folder: {args.install}")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = []

    folders = [("install", args.install), ("cd", args.cd)]
    for where, folder in folders:
        if folder is None:
            continue
        names = ARCHIVES + OTHERS
        if where == "cd":
            names = names + ["english/stbof.res", "english/english.snd", "german/stbof.res"]
        for name in names:
            path = find(folder, name)
            if path is None:
                continue
            label = f"{where}/{name}"
            is_archive = name.endswith((".res", ".snd"))
            head = sample(path, label, out, report, tail=is_archive, root=Path(folder))
            if head[:4] == b"PK\x03\x04" or (is_archive and name.endswith(".res")):
                list_zip(path, report)

        # One AVI is enough to identify the codec; record every AVI's streams.
        lang = find(folder, "english", want_dir=True) if where == "cd" else None
        avi_folder = lang if lang is not None else Path(folder)
        first = True
        for path in sorted(avi_folder.iterdir()):
            if path.suffix.lower() != ".avi" or not path.is_file():
                continue
            label = f"{where}/{avi_folder.name if lang else ''}/{path.name}".replace("//", "/")
            if first:
                head = sample(path, label, out, report, head_bytes=AVI_HEAD_BYTES)
                first = False
            else:
                head = read_range(path, 0, AVI_HEAD_BYTES)
                report.append(f"== {label}")
                report.append(f"   size: {path.stat().st_size:,}")
            report.append(f"   streams: {avi_codecs(head)}")

    (out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"Wrote {len(list(out.iterdir()))} files to {out.resolve()}")
    print("Please share the folder (or zip it up). It holds only the first and last "
          "few KB of each file and the archives' listings.")


if __name__ == "__main__":
    main()
