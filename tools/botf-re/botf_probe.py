#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Check the layouts worked out from botf_sample.py's samples against whole files.

Reads (never writes) Star Trek: Birth of the Federation's install folder,
and optionally the CD, and writes a text report about:

- alt.res: reads its directory, decompresses every file
  (docs/engines/botf/formats/alt-res.md), checks the sizes and tries to identify
  the 16-bit check value
- sfx.snd, english.snd, music.snd: every directory entry, with the first
  bytes of each sound (docs/engines/botf/formats/snd.md)
- music.tbl from stbof.res: its first lines

The report holds no images or sounds, only headers, sizes and offsets.
Only standard-library Python 3.8+ is needed:

    python3 botf_probe.py "C:\\Program Files\\MicroProse\\Trek" --cd D:\\ -o botf-probe.txt

Decompressing alt.res takes up to a few minutes.
"""

import argparse
import re
import struct
import sys
import time
import zlib
import zipfile
from pathlib import Path


def find(folder, name):
    """Case-insensitive lookup of `name` in `folder`."""
    if folder is None or not Path(folder).is_dir():
        return None
    for p in Path(folder).iterdir():
        if p.name.lower() == name.lower() and p.is_file():
            return p
    return None


def tga_header(data, off):
    """(width, height, bits, descriptor, size, type, id length, colour map length)
    if a TGA header plausibly starts at `off`; size is None for RLE types."""
    if off + 18 > len(data):
        return None
    id_len, cmap_type, img_type = data[off], data[off + 1], data[off + 2]
    if cmap_type > 1 or img_type not in (1, 2, 3, 9, 10, 11):
        return None
    cmap_first, cmap_len, cmap_bits = struct.unpack_from("<HHB", data, off + 3)
    if cmap_type == 0 and (cmap_len or cmap_bits):
        return None
    if cmap_type == 1 and (cmap_len == 0 or cmap_bits not in (15, 16, 24, 32)):
        return None
    x0, y0, w, h, bits, desc = struct.unpack_from("<HHHHBB", data, off + 8)
    if not (8 <= w <= 4096 and 8 <= h <= 4096 and bits in (8, 15, 16, 24, 32)):
        return None
    if x0 > 4096 or y0 > 4096 or desc & 0xC0:
        return None
    cmap_size = cmap_len * ((cmap_bits + 7) // 8) if cmap_type else 0
    size = None
    if img_type in (1, 2, 3):
        size = 18 + id_len + cmap_size + w * h * ((bits + 7) // 8)
    return w, h, bits, desc, size, img_type, id_len, cmap_len


def describe_tga(h):
    w, hgt, bits, desc, size, img_type, id_len, cmap_len = h
    return (f"TGA type {img_type} {w}x{hgt} {bits}-bit, descriptor {desc:#04x}, "
            f"id length {id_len}, colour map {cmap_len}, "
            + (f"{size:,} bytes" if size else "RLE"))


class Lzss:
    """alt.res's LZSS decoder, with its state kept between calls so the
    stream can be read file by file (see decode_files)."""

    def __init__(self, src, pos):
        self.src = src
        self.pos = pos
        self.flags = 0
        self.reset_window()

    def reset_window(self):
        self.window = bytearray(4096)
        self.r = 1  # where writing starts, docs/engines/botf/formats/alt-res.md

    def new_flags(self):
        self.flags = 0

    def read(self, count, out):
        """Append `count` bytes to `out`; False if the input ran out first."""
        src, window = self.src, self.window
        want = len(out) + count
        while len(out) < want:
            self.flags >>= 1
            if not self.flags & 0x100:
                if self.pos >= len(src):
                    return False
                self.flags = src[self.pos] | 0xFF00
                self.pos += 1
            if self.flags & 1:
                if self.pos >= len(src):
                    return False
                c = src[self.pos]
                self.pos += 1
                out.append(c)
                window[self.r] = c
                self.r = (self.r + 1) & 0xFFF
            else:
                if self.pos + 1 >= len(src):
                    return False
                a, b = src[self.pos], src[self.pos + 1]
                self.pos += 2
                at = ((a & 0x0F) << 8) | b
                for k in range(((a >> 4) & 0x0F) + 2):
                    c = window[(at + k) & 0xFFF]
                    out.append(c)
                    window[self.r] = c
                    self.r = (self.r + 1) & 0xFFF
        return True


def checksums(data):
    """Candidate 16-bit checks of `data`, by name."""
    out = {
        "sum of bytes": sum(data) & 0xFFFF,
        "xor of 16-bit words": 0,
        "sum of 16-bit words": 0,
        "crc32 low 16": zlib.crc32(data) & 0xFFFF,
        "crc32 high 16": zlib.crc32(data) >> 16,
    }
    words = memoryview(bytes(data[:len(data) & ~1])).cast("H")
    x = 0
    for w in words:
        x ^= w
    out["xor of 16-bit words"] = x
    out["sum of 16-bit words"] = sum(words) & 0xFFFF
    return out


def crc16s(data):
    """The common table-free CRC-16s; slow in Python, so used on small inputs."""
    def crc(poly, init, reflect, xorout):
        c = init
        if reflect:
            for b in data:
                c ^= b
                for _ in range(8):
                    c = (c >> 1) ^ poly if c & 1 else c >> 1
        else:
            for b in data:
                c ^= b << 8
                for _ in range(8):
                    c = ((c << 1) ^ poly) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        return c ^ xorout
    return {
        "crc16/arc": crc(0xA001, 0, True, 0),
        "crc16/modbus": crc(0xA001, 0xFFFF, True, 0),
        "crc16/xmodem": crc(0x1021, 0, False, 0),
        "crc16/ccitt-false": crc(0x1021, 0xFFFF, False, 0),
        "crc16/kermit": crc(0x8408, 0, True, 0),
        "crc16/x25": crc(0x8408, 0xFFFF, True, 0xFFFF),
    }


def probe_alt(path, report):
    data = path.read_bytes()
    report.append(f"== {path.name}: {len(data):,} bytes")
    dir_off, count = struct.unpack_from("<II", data, 16)
    report.append(f"   directory at {dir_off:,}, {count} entries")
    entries = []
    for n in range(count):
        rec = data[dir_off + 40 * n:dir_off + 40 * (n + 1)]
        name = rec[:16].split(b"\0")[0].decode("ascii", "replace")
        entries.append((name,) + struct.unpack_from("<6I", rec, 16))
    rest = data[dir_off + 40 * count:]
    report.append(f"   after the directory: {len(rest):,} bytes, "
                  f"{sum(1 for b in rest if b)} nonzero")

    t = time.time()
    problems = 0
    matches = {}
    for n, (name, off, index, usize, csize, flag, check) in enumerate(entries):
        lz = Lzss(data, off)
        out = bytearray()
        complete = lz.read(usize, out)
        used = lz.pos - off
        h = tga_header(out, 0)
        footer = out.find(b"TRUEVISION-XFILE")
        notes = []
        if index != n:
            notes.append(f"index {index}")
        if not complete or len(out) != usize:
            notes.append(f"decoded {len(out):,}")
        if used != csize:
            notes.append(f"used {used:,} compressed bytes")
        if footer >= 0:
            ext, dev = struct.unpack_from("<II", out, footer - 8)
            notes.append(f"footer at +{footer - 8:,} (extension {ext}, developer {dev})")
        problems += bool(index != n or len(out) != usize or used != csize)
        report.append(f"   {n:3} {name:16} offset {off:>11,} csize {csize:>9,} usize {usize:>9,} "
                      f"flag {flag} check {check:#06x}  "
                      + (describe_tga(h) if h else f"no TGA header: {out[:18].hex(' ')}")
                      + ("; " + "; ".join(notes) if notes else ""))
        # Which candidate check gives `check`? Compressed and decoded bytes.
        cands = {}
        for what, buf in (("compressed", data[off:off + csize]), ("decoded", out)):
            for k, v in checksums(buf).items():
                cands[f"{k} of {what}"] = v
            if n < 3:
                for k, v in crc16s(buf).items():
                    cands[f"{k} of {what}"] = v
        for k, v in cands.items():
            if v == check:
                matches[k] = matches.get(k, 0) + 1
    report.append(f"   entries that don't decode to their sizes: {problems}; "
                  f"{time.time() - t:.0f} s")
    report.append("   checks matching the stored value (name: entries; CRC-16s tried on "
                  "the first 3 only): "
                  + (", ".join(f"{k}: {v}" for k, v in sorted(matches.items())) or "none"))


def ima_header_ok(b, channels):
    return all(len(b) >= 4 * (c + 1) and b[4 * c + 2] <= 88 and b[4 * c + 3] == 0
               for c in range(channels))


def wav_fmt(b):
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return None
    pos = 12
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack_from("<I", b, pos + 4)[0]
        if cid == b"fmt ":
            tag, ch, rate, _, align, bits = struct.unpack_from("<HHIIHH", b, pos + 8)
            return f"WAV tag {tag:#x}, {ch} ch, {rate} Hz, {bits}-bit, block {align}"
        pos += 8 + size + (size & 1)
    return "WAV without fmt in its first bytes"


def probe_snd_entries(f, entries, report, file_size):
    flags = {}
    for n, (off, size, third, flag) in enumerate(entries):
        flags[flag] = flags.get(flag, 0) + 1
        f.seek(off)
        head = f.read(64)
        what = wav_fmt(head)
        if what is None:
            ch = 2 if flag == 3 else 1
            what = (f"IMA header ok ({ch} ch)" if ima_header_ok(head, ch)
                    else "unknown") + f": {head[:12].hex(' ')}"
        inside = off + size <= file_size
        report.append(f"   {n:5}: offset {off:>11,} size {size:>9,} third {third:>10,} "
                      f"({third:#010x}) flag {flag}{'' if inside else ' PAST END'}  {what}")
    report.append(f"   entries by flag: {dict(sorted(flags.items()))}")


def probe_snd(path, report):
    size = path.stat().st_size
    report.append(f"== {path.name}: {size:,} bytes")
    with open(path, "rb") as f:
        first = f.read(0x80)
        if path.name.lower() == "music.snd":
            pairs = [struct.unpack_from("<II", first, 8 * k) for k in range(16)]
            report.append("   header (count, offset): " +
                          ", ".join(f"({c}, {o:,})" for c, o in pairs))
            for t, (c, o) in enumerate(pairs):
                if not 0 < o < size or c > 1000:
                    report.append(f"   piece {t}: not a (count, offset) pair")
                    continue
                f.seek(o)
                entries = [struct.unpack("<4I", f.read(16)) for _ in range(c)]
                report.append(f"   piece {t}: {c} entries at {o:,}")
                probe_snd_entries(f, entries, report, size)
            end = max(o + 16 * c for c, o in pairs if 0 < o < size)
            f.seek(end)
            rest = f.read()
            report.append(f"   after the last list ({end:,}): {len(rest):,} bytes: "
                          f"{rest[:64].hex(' ')}")
        else:
            dir_off = struct.unpack_from("<I", first, 0)[0]
            count = (size - dir_off) // 16
            report.append(f"   directory at {dir_off:,}: {count} entries, "
                          f"{(size - dir_off) % 16} bytes left over")
            f.seek(dir_off)
            raw = f.read()
            entries = [struct.unpack_from("<4I", raw, 16 * k) for k in range(count)]
            probe_snd_entries(f, entries, report, size)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("install", help="the game's install folder")
    parser.add_argument("--cd", help="the CD (or a copy of it), for music.snd")
    parser.add_argument("-o", "--out", default="botf-probe.txt", help="report file")
    parser.add_argument("--only", nargs="+", metavar="FILE",
                        help="probe only these files, e.g. --only alt.res")
    args = parser.parse_args()
    report = []

    def first(name):
        return find(args.install, name) or find(args.cd, name)

    for name, probe in [("alt.res", probe_alt), ("sfx.snd", probe_snd),
                        ("english.snd", probe_snd), ("music.snd", probe_snd)]:
        if args.only and name not in [n.lower() for n in args.only]:
            continue
        path = first(name)
        if path is None:
            report.append(f"== {name}: not found")
            continue
        print(f"probing {path} ...", flush=True)
        try:
            probe(path, report)
        except Exception as e:  # keep going: a partial report still helps
            report.append(f"   failed: {type(e).__name__}: {e}")

    res = first("stbof.res")
    if res is not None and (not args.only or "music.tbl" in [n.lower() for n in args.only]):
        try:
            with zipfile.ZipFile(res) as z:
                name = next(n for n in z.namelist() if n.lower() == "music.tbl")
                data = z.read(name)
                printable = sum(1 for b in data if 32 <= b < 127 or b in (9, 10, 13))
                if printable > 0.95 * len(data):
                    report.append(f"== stbof.res/{name}: first lines")
                    report.extend("   " + line
                                  for line in data.decode("latin-1").splitlines()[:60])
                else:
                    report.append(f"== stbof.res/{name}: {len(data):,} bytes, binary; first 256:")
                    report.extend(f"   {data[k:k + 16].hex(' ')}" for k in range(0, 256, 16))
        except (StopIteration, zipfile.BadZipFile) as e:
            report.append(f"== stbof.res music.tbl: not read ({e})")

    Path(args.out).write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
