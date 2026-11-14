#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Asset extractor for Star Trek: Deep Space Nine - Harbinger (DOS, 1996).

Pure standard library, Python 3.8+. Works on Windows and Linux.

    python3 harbinger_extract.py extract OUT_DIR DISC1_ROOT [DISC2_ROOT]
    python3 harbinger_extract.py probe-raw OUT_DIR SOUND/AIRLOCKC.RAW
    python3 harbinger_extract.py map-palettes OUT_DIR DISC1_ROOT [DISC2_ROOT]

`extract` options:
    --jobs N          worker processes (default: CPU count)
    --limit N         convert at most N files of each kind (quick test run)
    --only KINDS      comma list of: archive,cmp,rle,raw,pal,map
    --raw-format F    auto | u8 | s8 | s16 | s16be (default auto: decided per file)
    --raw-rate HZ     sample rate for .RAW / dialogue audio (default 22050)

What it does (formats as reverse engineered so far):
    *.CAT/*.LST/*.ALL archives  -> unpacked (DIALOGUE -> .wav, PHONEMES -> .bin + transcript)
    *.CMP backgrounds           -> .png  (u32 length + PKWARE DCL "implode" stream)
    *.RLE animations/sprites    -> .png per frame (RGBA, transparent background)
    *.RAW sound effects/music   -> .wav  (16-bit signed LE, 22050 Hz confirmed by ear on
                                          AIRLOCKC.RAW; auto-detected per file)
    *.PAL palettes              -> .png swatch (6-bit VGA -> 8-bit)
    MAPS/*.DAT                  -> .png station deck maps (raw 8-bit images, width auto-detected;
                                   palettes picked by eye, see MAP_PALETTES / --map-palette)
    MZ executables in archives  -> identified (MZ / LE / PE ...) in the report

Everything uncertain is written to OUT_DIR/report.txt and the CSV files next to it,
so the guesses can be checked against the real data.
"""

import argparse
import collections
import concurrent.futures
import csv
import math
import os
import re
import struct
import sys
import wave
import zlib
from pathlib import Path


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------

def u16(d, o):
    return d[o] | (d[o + 1] << 8)


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def hexs(d, n=16):
    return " ".join("%02X" % b for b in d[:n])


def _png_chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, width, height, pixels, color_type, palette=None):
    """pixels: bytes of height rows; 1 byte/pixel for types 0 and 3, 4 for type 6 (RGBA)."""
    bpp = 4 if color_type == 6 else 1
    stride = width * bpp
    raw = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
    if palette is not None:
        png += _png_chunk(b"PLTE", bytes(palette))
    png += _png_chunk(b"IDAT", zlib.compress(raw, 6))
    png += _png_chunk(b"IEND", b"")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(png)


_SIGNED_TO_UNSIGNED = bytes(((i + 128) & 0xFF) for i in range(256))


def write_wav(path, samples, rate, fmt="u8"):
    """WAV 8-bit PCM is unsigned, so signed input is flipped; s16 is written as-is."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    width = 1
    if fmt == "s8":
        samples = samples.translate(_SIGNED_TO_UNSIGNED)
    elif fmt in ("s16", "s16be"):
        width = 2
        samples = samples[:len(samples) & ~1]
        if fmt == "s16be":
            samples = bytes(b for i in range(0, len(samples), 2) for b in (samples[i + 1], samples[i]))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(samples)


def vga_palette(pal):
    """768-byte palette -> 8-bit RGB. Values 0..63 are 6-bit VGA DAC values."""
    pal = bytes(pal[:768]).ljust(768, b"\x00")
    if max(pal) <= 63:
        return bytes((v << 2) | (v >> 4) for v in pal), True
    return pal, False


GREY_PALETTE = bytes(v for i in range(256) for v in (i, i, i))


def false_colour_palette():
    """Distinct colours per byte value, for map grids. 0 and 1 stay dark."""
    out = bytearray()
    for i in range(256):
        if i in (0, 1):
            out += bytes((16 * i, 16 * i, 16 * i))
            continue
        h = (i * 0.618033988749895) % 1.0
        r, g, b = _hsv(h, 0.65, 0.95)
        out += bytes((r, g, b))
    return bytes(out)


def _hsv(h, s, v):
    i = int(h * 6)
    f = h * 6 - i
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i % 6]
    return int(r * 255), int(g * 255), int(b * 255)


# --------------------------------------------------------------------------
# PKWARE Data Compression Library ("implode") decoder.
# Port of Mark Adler's blast.c (zlib contrib/blast), table-driven for speed.
# --------------------------------------------------------------------------

class DCLError(Exception):
    pass


_DCL_LITLEN = bytes([
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
    44, 173])
_DCL_LENLEN = bytes([2, 35, 36, 53, 38, 23])
_DCL_DISTLEN = bytes([2, 20, 53, 230, 247, 151, 248])
_DCL_BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
_DCL_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)


def _dcl_table(rep, nsym):
    lengths = []
    for b in rep:
        lengths.extend([b & 15] * ((b >> 4) + 1))
    if len(lengths) != nsym:
        raise DCLError("code table has %d symbols, expected %d" % (len(lengths), nsym))
    maxbits = max(lengths)
    if sum(1 << (maxbits - l) for l in lengths if l) != 1 << maxbits:
        raise DCLError("incomplete code table")
    table = [None] * (1 << maxbits)
    code = 0
    for l in range(1, maxbits + 1):
        for sym, sl in enumerate(lengths):
            if sl != l:
                continue
            # Codes are read MSB first and stored inverted in an LSB-first stream.
            pattern = 0
            for i in range(l):
                pattern |= (((code >> (l - 1 - i)) & 1) ^ 1) << i
            for hi in range(1 << (maxbits - l)):
                table[pattern | (hi << l)] = (sym, l)
            code += 1
        code <<= 1
    return table, (1 << maxbits) - 1


_DCL_TABLES = None


def _dcl_tables():
    global _DCL_TABLES
    if _DCL_TABLES is None:
        _DCL_TABLES = (_dcl_table(_DCL_LITLEN, 256), _dcl_table(_DCL_LENLEN, 16),
                       _dcl_table(_DCL_DISTLEN, 64))
    return _DCL_TABLES


def dcl_decompress(data, allow_truncated=False):
    """Returns (output, truncated). Raises DCLError on corrupt input."""
    n = len(data)
    if n < 2:
        raise DCLError("stream too short")
    lit, dict_bits = data[0], data[1]
    if lit > 1:
        raise DCLError("bad literal mode %d" % lit)
    if not 4 <= dict_bits <= 6:
        raise DCLError("bad dictionary size %d" % dict_bits)
    (lit_t, lit_m), (len_t, len_m), (dist_t, dist_m) = _dcl_tables()
    data = bytes(data) + b"\x00" * 8          # padding so lookahead never runs dry
    real_bits = n * 8
    pos, bitbuf, bitcnt = 2, 0, 0
    out = bytearray()
    while True:
        while bitcnt < 32:
            bitbuf |= data[pos] << bitcnt
            pos += 1
            bitcnt += 8
        if pos * 8 - bitcnt > real_bits:
            if allow_truncated:
                return bytes(out), True
            raise DCLError("unexpected end of stream after %d output bytes" % len(out))
        if not bitbuf & 1:
            if lit == 0:
                out.append((bitbuf >> 1) & 0xFF)
                bitbuf >>= 9
                bitcnt -= 9
            else:
                sym, l = lit_t[(bitbuf >> 1) & lit_m]
                out.append(sym)
                bitbuf >>= 1 + l
                bitcnt -= 1 + l
            continue
        sym, l = len_t[(bitbuf >> 1) & len_m]
        bitbuf >>= 1 + l
        bitcnt -= 1 + l
        e = _DCL_EXTRA[sym]
        length = _DCL_BASE[sym] + (bitbuf & ((1 << e) - 1))
        bitbuf >>= e
        bitcnt -= e
        if length == 519:
            if pos * 8 - bitcnt > real_bits:
                if allow_truncated:
                    return bytes(out), True
                raise DCLError("end code read past end of stream")
            return bytes(out), False
        s = 2 if length == 2 else dict_bits
        dsym, l = dist_t[bitbuf & dist_m]
        bitbuf >>= l
        dist = (dsym << s) + (bitbuf & ((1 << s) - 1)) + 1
        bitbuf >>= s
        bitcnt -= l + s
        start = len(out) - dist
        if start < 0:
            if allow_truncated and pos * 8 - bitcnt > real_bits:
                return bytes(out), True
            raise DCLError("distance %d too far back at output %d" % (dist, len(out)))
        if dist >= length:
            out += out[start:start + length]
        else:
            out += (out[start:] * (length // dist + 1))[:length]


# --------------------------------------------------------------------------
# Archives: NAME.CAT (text: count, names), NAME.LST (u32 offsets), NAME.ALL (data)
# --------------------------------------------------------------------------

def sibling(path, ext):
    """Case-insensitive lookup of the same stem with another extension."""
    want = (path.stem + ext).upper()
    for p in path.parent.iterdir():
        if p.name.upper() == want:
            return p
    return None


def read_archive(lst_path):
    all_path = sibling(lst_path, ".ALL")
    cat_path = sibling(lst_path, ".CAT")
    issues = []
    raw = lst_path.read_bytes()
    if len(raw) % 4:
        issues.append("LST size %d is not a multiple of 4" % len(raw))
    offsets = list(struct.unpack("<%dI" % (len(raw) // 4), raw[:len(raw) & ~3]))
    size = all_path.stat().st_size
    if offsets and offsets[-1] == size:
        bounds = offsets
    else:
        issues.append("last offset %s != ALL size %d (no end sentinel?)"
                      % (offsets[-1] if offsets else None, size))
        bounds = offsets + [size]
    if any(b > a for a, b in zip(bounds[1:], bounds)):
        issues.append("offsets are not increasing")
    count = len(bounds) - 1
    names = None
    if cat_path:
        lines = [l.strip() for l in cat_path.read_bytes().decode("latin-1").split("\n")]
        lines = [l for l in lines if l and l != "\x1a"]
        try:
            declared = int(lines[0])
        except (ValueError, IndexError):
            declared = None
            issues.append("CAT has no count line")
        names = lines[1:] if declared is not None else lines
        if declared is not None and declared != count:
            issues.append("CAT count %d != %d LST entries" % (declared, count))
        if len(names) != count:
            issues.append("CAT lists %d names for %d entries" % (len(names), count))
    entries = []
    for i in range(count):
        name = names[i] if names and i < len(names) else "%s_%03d.bin" % (lst_path.stem, i)
        entries.append((name, bounds[i], bounds[i + 1]))
    return all_path, entries, issues


def has_part(path, part):
    return any(p.upper() == part for p in path.parts)


def phoneme_words(clip):
    """Rough transcript: NUL-delimited words inside a phoneme clip."""
    words = re.findall(rb"(?<=\x00)([A-Za-z][A-Za-z'\-]*)(?=\x00)", clip)
    return " ".join(w.decode("ascii") for w in words)


def identify_exe(d):
    if d[:2] not in (b"MZ", b"ZM"):
        return None
    kind = "MZ (16-bit DOS / stub)"
    if len(d) >= 0x40:
        off = u32(d, 0x3C)
        sig = d[off:off + 2] if 0 < off < len(d) - 2 else b""
        kind = {b"PE": "PE (32-bit Windows)", b"LE": "LE (32-bit DOS, DOS/4GW style)",
                b"LX": "LX (32-bit OS/2 or DOS extender)", b"NE": "NE (16-bit Windows)"}.get(sig, kind)
    if not kind.startswith(("LE", "LX", "PE", "NE")):
        le = find_le_header(d)
        if le is not None:
            kind = "MZ stub + LE at 0x%X (32-bit DOS, bound to extender)" % le
    if b"DOS/4G" in d:
        kind += ", mentions DOS/4G"
    if b"WATCOM" in d.upper():
        kind += ", Watcom runtime"
    return kind


def find_le_header(d):
    """LE header: 'LE', byte/word order 0, format level 0, CPU 1..4, OS 1..4."""
    for m in re.finditer(b"LE\x00\x00\x00\x00\x00\x00", d):
        o = m.start()
        if len(d) >= o + 12 and 1 <= u16(d, o + 8) <= 4 and 1 <= u16(d, o + 10) <= 4:
            return o
    return None


# --------------------------------------------------------------------------
# CMP backgrounds
# --------------------------------------------------------------------------

_NUM_RE = re.compile(r"^(.*?)(\d+)$")


def choose_palette(stem, names):
    """Heuristic palette match for previews. The real mapping lives in the game data."""
    s = stem.upper()
    if s in names:
        return s
    m = _NUM_RE.match(s)
    if m:
        prefix, num = m.group(1), int(m.group(2))
        best = None
        for name in names:
            m2 = _NUM_RE.match(name)
            if m2 and m2.group(1) == prefix and int(m2.group(2)) <= num:
                if best is None or int(m2.group(2)) > best[0]:
                    best = (int(m2.group(2)), name)
        if best:
            return best[1]
    best_len, best_name = 0, None
    for name in sorted(names):
        l = len(os.path.commonprefix([s, name]))
        if l > best_len:
            best_len, best_name = l, name
    return best_name if best_len >= 3 else None


def cmp_dimensions(size):
    for w, h in ((640, 480), (640, 400), (320, 200), (640, 350)):
        if size == w * h:
            return w, h, 0
        if size == w * h + 768:
            return w, h, 768
    if size % 640 == 0:
        return 640, size // 640, 0
    return None


def job_cmp(job):
    src, outbase, pal, palname = job["src"], job["out"], job["pal"], job["palname"]
    d = Path(src).read_bytes()
    res = {"kind": "cmp", "file": src, "size": len(d), "palette": palname or "grey"}
    try:
        declared = u32(d, 0)
        res["header_u32"] = declared
        res["header_matches_size"] = declared == len(d) - 4
        pixels, _ = dcl_decompress(d[4:])
    except (DCLError, struct.error, IndexError) as e:
        res["error"] = str(e)
        return res
    res["out_size"] = len(pixels)
    res["first16"] = hexs(pixels)
    dims = cmp_dimensions(len(pixels))
    if dims is None:
        Path(outbase + ".bin").parent.mkdir(parents=True, exist_ok=True)
        Path(outbase + ".bin").write_bytes(pixels)
        res["error"] = "unknown dimensions; raw output written to .bin"
        return res
    w, h, tail = dims
    palette = vga_palette(pixels[w * h:])[0] if tail else (pal or GREY_PALETTE)
    if tail:
        res["palette"] = "embedded"
    write_png(outbase + ".png", w, h, pixels[:w * h], 3, palette)
    res["dims"] = "%dx%d" % (w, h)
    return res


# --------------------------------------------------------------------------
# RLE animations
#   file:  8 bytes magic, u16 frame count
#   frame: 25-byte header: +0 u8 (key colour?), +1 u16 width, +3 u16 height,
#          +5 u16 x, +7 u16 y, +9..+20 unknown, +21 u32 data length
#   data:  top two bits select the opcode, low six bits are the count n:
#          00 literal n bytes, 01 new row + skip n, 10 run of n x next byte,
#          11 skip n. Confirmed on POWGLF, CHDRK1, UP040A1, ANTI01, DR101R3X, EXPLSMM1.
# --------------------------------------------------------------------------

def parse_rle(d):
    frames, off, issues = [], 10, []
    while off + 25 <= len(d):
        hdr = d[off:off + 25]
        size = u32(hdr, 21)
        if off + 25 + size > len(d):
            issues.append("frame at 0x%X claims %d bytes, past end of file" % (off, size))
            break
        frames.append((off, hdr, d[off + 25:off + 25 + size]))
        off += 25 + size
    return u16(d, 8), frames, len(d) - off, issues


def decode_rle_rows(buf):
    """Opcodes (6-bit counts): 00-3F literal n bytes, 40-7F start new row then skip n,
    80-BF run of n x next byte, C0-FF skip n transparent pixels."""
    rows, cur, x, p, n, warnings = [], None, 0, 0, len(buf), 0
    while p < n:
        op = buf[p]
        p += 1
        cnt = op & 0x3F
        kind = op >> 6
        if kind == 1:
            cur = []
            rows.append(cur)
            x = cnt
            continue
        if cur is None:
            cur = []
            rows.append(cur)
        if kind == 3:
            x += cnt
        elif kind == 2:
            if p >= n:
                warnings += 1
                break
            cur.append((x, bytes([buf[p]]) * cnt))
            p += 1
            x += cnt
        else:
            seg = buf[p:p + cnt]
            if len(seg) < cnt:
                warnings += 1
            cur.append((x, seg))
            p += cnt
            x += len(seg)
    width = max((sx + len(s) for row in rows for sx, s in row), default=0)
    left = min((sx for row in rows for sx, s in row if s), default=0)
    return rows, width, left, warnings


def decode_rle_frame(hdr, body):
    w, h = u16(hdr, 1), u16(hdr, 3)
    rows, width, left, warnings = decode_rle_rows(body)
    while len(rows) > h and not rows[-1]:      # trailing row marker ends the frame
        rows.pop()
    return rows, width, left, warnings, len(rows) == h and width <= w


def job_rle(job):
    src, outbase, pal, palname = job["src"], job["out"], job["pal"], job["palname"]
    d = Path(src).read_bytes()
    res = {"kind": "rle", "file": src, "size": len(d), "palette": palname or "grey", "frames": []}
    try:
        res["magic"] = hexs(d, 8)
        count, frames, trailing, issues = parse_rle(d)
    except (struct.error, IndexError) as e:
        res["error"] = str(e)
        return res
    res.update(count_field=count, frames_found=len(frames), trailing=trailing, issues=issues)
    rgba = [bytes(pal[i * 3:i * 3 + 3]) + b"\xff" for i in range(256)]
    for i, (off, hdr, body) in enumerate(frames):
        w, h, x, y = (u16(hdr, k) for k in (1, 3, 5, 7))
        rows, width, left, warnings, fits = decode_rle_frame(hdr, body)
        info = {"frame": i, "offset": off, "header": hexs(hdr, 25), "key": hdr[0],
                "w": w, "h": h, "x": x, "y": y, "rows": len(rows), "max_x": width,
                "left": left, "warnings": warnings, "fits": fits}
        res["frames"].append(info)
        if not (w and h) or w > 640 or h > 480:
            continue
        # Draw into the header-sized canvas; anything that does not fit is clipped.
        img = bytearray(w * 4 * h)
        for ry, row in enumerate(rows[:h]):
            base = ry * w * 4
            for sx, seg in row:
                seg = seg[:max(0, w - sx)]
                o = base + sx * 4
                img[o:o + len(seg) * 4] = b"".join(rgba[v] for v in seg)
        name = outbase + (".png" if len(frames) == 1 else "_%03d.png" % i)
        write_png(name, w, h, bytes(img), 6)
    return res


# --------------------------------------------------------------------------
# RAW audio
# --------------------------------------------------------------------------

def _entropy(d):
    n = len(d) or 1
    return -sum(c / n * math.log2(c / n) for c in (d.count(bytes([v])) for v in range(256)) if c)


def detect_raw_format(d):
    """16-bit LE audio has a much more predictable high byte (odd offsets) than low byte."""
    lo, hi = _entropy(d[0::2]), _entropy(d[1::2])
    # A byte plane that never changes is the zero low byte of 8-bit audio stored as
    # 16-bit (WHISPER1/2.RAW). Otherwise the high byte is the predictable plane.
    # (Waveform smoothness is no help: clicks and noise bursts such as TICK.RAW
    # flip sign almost every sample in either byte order.)
    if lo < 0.1 or (hi >= 0.1 and lo - hi > 1.0):
        return "s16"
    if hi < 0.1 or hi - lo > 1.0:
        return "s16be"
    near_mid = sum(1 for b in d[:65536] if 0x60 <= b < 0xA0)
    near_edge = sum(1 for b in d[:65536] if b < 0x20 or b >= 0xE0)
    return "u8" if near_mid >= near_edge else "s8"


def raw_stats(d):
    n = len(d) or 1
    hist = [d.count(bytes([v])) for v in range(256)]
    entropy = -sum(c / n * math.log2(c / n) for c in hist if c)
    near_mid = sum(hist[0x70:0x90]) / n
    near_edge = (sum(hist[:0x10]) + sum(hist[0xF0:])) / n
    odd = d[1::2]
    odd_edge = (sum(odd.count(bytes([v])) for v in list(range(0x08)) + list(range(0xF8, 0x100)))
                / (len(odd) or 1))
    if entropy > 7.6 and odd_edge < 0.5:
        guess = "high entropy: compressed/ADPCM or encrypted?"
    elif odd_edge > 0.6 and near_edge < 0.6 and len(d) % 2 == 0:
        guess = "16-bit signed little-endian?"
    elif near_mid >= near_edge:
        guess = "8-bit unsigned"
    else:
        guess = "8-bit signed"
    return {"entropy": round(entropy, 2), "near_0x80": round(near_mid, 3), "auto": detect_raw_format(d),
            "near_0x00_0xFF": round(near_edge, 3), "odd_bytes_near_0": round(odd_edge, 3),
            "guess": guess, "first16": hexs(d)}


def job_raw(job):
    d = Path(job["src"]).read_bytes()
    res = {"kind": "raw", "file": job["src"], "size": len(d)}
    res.update(raw_stats(d))
    fmt = res["auto"] if job["fmt"] == "auto" else job["fmt"]
    res["written_as"] = fmt
    write_wav(job["out"] + ".wav", d, job["rate"], fmt)
    return res


# --------------------------------------------------------------------------
# Palettes and map grids
# --------------------------------------------------------------------------

def job_pal(job):
    d = Path(job["src"]).read_bytes()
    pal, six = vga_palette(d)
    img = bytes((y // 16) * 16 + x // 16 for y in range(256) for x in range(256))
    write_png(job["out"] + ".png", 256, 256, img, 3, pal)
    return {"kind": "pal", "file": job["src"], "size": len(d), "six_bit": six}


def guess_width(d, lo=32, hi=2048, samples=4000):
    n = len(d)
    hi = min(hi, n // 4)
    edges = [i for i in range(n - 1) if d[i] != d[i + 1]]
    if not edges or hi <= lo:
        return None, []
    step = max(1, len(edges) // samples)
    sample = edges[::step]
    scores = []
    for w in range(lo, hi + 1):
        lim = n - w - 1
        t = m = 0
        for i in sample:
            if i < lim:
                t += 1
                if d[i + w] == d[i] and d[i + w + 1] == d[i + 1]:
                    m += 1
        scores.append((m / t if t else 0.0, w))
    top = max(s for s, _ in scores)
    width = min(w for s, w in scores if s >= 0.97 * top)
    return width, sorted(scores, reverse=True)[:5]


def job_map(job):
    d = Path(job["src"]).read_bytes()
    width, top = guess_width(d)
    res = {"kind": "map", "file": job["src"], "size": len(d), "width": width,
           "top_widths": ["%d (%.2f)" % (w, s) for s, w in top],
           "values": ", ".join("%02X:%d" % kv for kv in collections.Counter(d).most_common(12))}
    if width:
        h = len(d) // width
        res["height"], res["remainder"] = h, len(d) - width * h
        res["palette"] = job.get("palname") or "false colour"
        write_png(job["out"] + ".png", width, h, d[:width * h], 3, job.get("pal") or false_colour_palette())
    return res


# Palettes picked by eye from the map-palettes contact sheets. The maps seem to use colour
# slots shared by a family of palettes (Ops uses different slots from the rest of the
# station), so any member of the family works. Override with --map-palette MAP=PAL.
MAP_PALETTES = {"OPS": "OPS01", "HABITAT": "CCRUN05", "LOWER": "CCRUN05", "UPPER": "CCRUN05"}


JOB_FUNCS = {"cmp": job_cmp, "rle": job_rle, "raw": job_raw, "pal": job_pal, "map": job_map}


def run_job(job):
    try:
        return JOB_FUNCS[job["kind"]](job)
    except Exception as e:  # keep going; one bad file should not stop a 2-CD run
        return {"kind": job["kind"], "file": job["src"], "error": "%s: %s" % (type(e).__name__, e)}


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------

class Extractor:
    def __init__(self, out, discs, args):
        self.out = Path(out)
        self.discs = [Path(d) for d in discs]
        self.args = args
        self.only = set(args.only.split(",")) if args.only else None
        self.report = collections.defaultdict(list)
        self.roots = []          # (label, root dir, output dir)
        self.map_palettes = dict(MAP_PALETTES)
        for item in args.map_palette:
            m, _, pal = item.upper().partition("=")
            self.map_palettes[m] = pal

    def wants(self, kind):
        return self.only is None or kind in self.only

    def log(self, msg):
        print(msg, flush=True)

    def run(self):
        for i, disc in enumerate(self.discs, 1):
            label = "disc%d" % i
            self.roots.append((label, disc, self.out / label))
        if self.wants("archive"):
            for label, disc, outdir in list(self.roots):
                self.unpack_archives(label, disc, outdir, depth=0)
        self.convert()
        self.write_report()

    # ---- archives --------------------------------------------------------

    def unpack_archives(self, label, root, outdir, depth):
        for lst in sorted(p for p in root.rglob("*") if p.suffix.upper() == ".LST"):
            if not sibling(lst, ".ALL"):
                continue
            rel = lst.relative_to(root)
            try:
                all_path, entries, issues = read_archive(lst)
            except OSError as e:
                self.report["archive"].append({"file": str(lst), "error": str(e)})
                continue
            info = {"file": "%s:%s" % (label, rel), "entries": len(entries), "issues": issues}
            self.report["archive"].append(info)
            dest = outdir / rel.parent / rel.stem
            data = all_path.read_bytes()
            if has_part(rel, "DIALOGUE"):
                self.unpack_dialogue(entries, data, dest, info)
            elif has_part(rel, "PHONEMES"):
                self.unpack_phonemes(entries, data, dest, info)
            else:
                for name, a, b in entries:
                    target = dest / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data[a:b])
                    kind = identify_exe(data[a:b])
                    if kind:
                        self.report["exe"].append({"file": "%s:%s/%s" % (label, rel.parent / rel.stem, name),
                                                   "size": b - a, "type": kind})
                self.roots.append((label, dest, dest))
                if depth < 3:
                    self.unpack_archives(label, dest, dest, depth + 1)
            self.log("archive %-45s %4d entries%s" % (info["file"], len(entries),
                                                      "  [%s]" % "; ".join(issues) if issues else ""))

    def unpack_dialogue(self, entries, data, dest, info):
        empty = 0
        for i, (_, a, b) in enumerate(entries):
            clip = data[a:b]
            if not clip:
                empty += 1
                continue
            fmt = detect_raw_format(clip) if self.args.raw_format == "auto" else self.args.raw_format
            write_wav(dest / ("clip_%03d.wav" % i), clip, self.args.raw_rate, fmt)
            if i < 2:
                st = raw_stats(clip)
                st.update(kind="dialogue", file="%s#%d" % (info["file"], i), size=len(clip))
                self.report["raw"].append(st)
        info["empty_clips"] = empty

    def unpack_phonemes(self, entries, data, dest, info):
        dest.mkdir(parents=True, exist_ok=True)
        lines = []
        for i, (_, a, b) in enumerate(entries):
            clip = data[a:b]
            if clip:
                (dest / ("clip_%03d.bin" % i)).write_bytes(clip)
            lines.append("%03d  %s" % (i, phoneme_words(clip) if clip else "(empty)"))
        (dest / "transcript.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # ---- per-file conversion ---------------------------------------------

    def convert(self):
        files = []
        for label, root, outdir in self.roots:
            for p in sorted(root.rglob("*")):
                if p.is_file():
                    files.append((label, root, outdir, p))
        palettes = {}
        for _, _, _, p in files:
            if p.suffix.upper() == ".PAL" and p.stat().st_size == 768:
                palettes.setdefault(p.stem.upper(), vga_palette(p.read_bytes())[0])
        jobs, counts = [], collections.Counter()
        seen = set()
        for label, root, outdir, p in files:
            ext = p.suffix.upper()
            kind = {".CMP": "cmp", ".RLE": "rle", ".RAW": "raw", ".PAL": "pal"}.get(ext)
            if ext == ".DAT" and p.parent.name.upper() == "MAPS":
                kind = "map"
            if not kind or not self.wants(kind):
                continue
            outbase = outdir / p.relative_to(root).with_suffix("")
            if str(outbase).upper() in seen:
                continue
            seen.add(str(outbase).upper())
            if self.args.limit and counts[kind] >= self.args.limit:
                continue
            counts[kind] += 1
            job = {"kind": kind, "src": str(p), "out": str(outbase),
                   "rate": self.args.raw_rate, "fmt": self.args.raw_format}
            if kind in ("cmp", "rle"):
                name = choose_palette(p.stem, palettes)
                job["pal"], job["palname"] = palettes.get(name, GREY_PALETTE), name
            elif kind == "map":
                name = self.map_palettes.get(p.stem.upper())
                if name in palettes:
                    job["pal"], job["palname"] = palettes[name], name
            jobs.append(job)
        self.log("converting: " + ", ".join("%d %s" % (n, k) for k, n in sorted(counts.items())))
        jobs.sort(key=lambda j: j["kind"] != "cmp")        # slowest first
        workers = self.args.jobs or os.cpu_count() or 1
        done = 0
        if workers == 1:
            results = map(run_job, jobs)
        else:
            pool = concurrent.futures.ProcessPoolExecutor(max_workers=workers)
            results = pool.map(run_job, jobs, chunksize=4)
        for res in results:
            self.report[res["kind"]].append(res)
            done += 1
            if "error" in res:
                self.log("  ! %s: %s" % (res["file"], res["error"]))
            if done % 200 == 0:
                self.log("  %d/%d" % (done, len(jobs)))
        if workers != 1:
            pool.shutdown()

    # ---- report ----------------------------------------------------------

    def write_report(self):
        self.out.mkdir(parents=True, exist_ok=True)
        r, lines = self.report, []
        add = lines.append

        add("== Archives")
        for a in r["archive"]:
            add("  %-50s %s entries %s%s" % (a["file"], a.get("entries"),
                "OK" if not a.get("issues") else "; ".join(a["issues"]),
                ", %d empty clips" % a["empty_clips"] if a.get("empty_clips") else ""))
        add("\n== Executables found inside archives")
        for e in r["exe"] or [{"file": "(none)", "size": "", "type": ""}]:
            add("  %-50s %8s  %s" % (e["file"], e["size"], e["type"]))

        cmp_ok = [c for c in r["cmp"] if "dims" in c]
        add("\n== CMP backgrounds: %d converted, %d failed" % (len(cmp_ok), len(r["cmp"]) - len(cmp_ok)))
        add("  header u32 == file size - 4: %d of %d"
            % (sum(1 for c in r["cmp"] if c.get("header_matches_size")), len(r["cmp"])))
        add("  decompressed sizes: " + ", ".join(
            "%s x%d" % kv for kv in collections.Counter(c.get("out_size") for c in r["cmp"]).most_common(8)))
        for c in [c for c in r["cmp"] if "error" in c][:15]:
            add("  FAIL %s: %s" % (c["file"], c["error"]))

        rle = r["rle"]
        add("\n== RLE animations: %d files, %d frames" % (len(rle), sum(len(x.get("frames", [])) for x in rle)))
        add("  frames chain exactly to end of file: %d of %d" % (sum(1 for x in rle if x.get("trailing") == 0), len(rle)))
        add("  frame-count field matches frames found: %d of %d"
            % (sum(1 for x in rle if x.get("count_field") == x.get("frames_found")), len(rle)))
        frames = [f for x in rle for f in x.get("frames", [])]
        add("  frames whose rows fit the header size exactly: %d of %d" % (sum(1 for f in frames if f["fits"]), len(frames)))
        add("  frames with truncated data: %d" % sum(1 for f in frames if f["warnings"]))
        add("  distinct 8-byte magics: " + "; ".join(
            "%s x%d" % kv for kv in collections.Counter(x.get("magic") for x in rle).most_common(5)))
        for x in [x for x in rle if "error" in x or x.get("issues")][:15]:
            add("  ISSUE %s: %s" % (x["file"], x.get("error") or "; ".join(x["issues"])))

        add("\n== RAW audio (sound effects + first clips of each dialogue archive)")
        add("  auto-detected formats: " + ", ".join("%s x%d" % kv for kv in collections.Counter(x["auto"] for x in r["raw"]).most_common()))
        add("  odd-sized files detected as 16-bit: %d" % sum(1 for x in r["raw"] if x["auto"].startswith("s16") and x.get("size", 0) % 2))
        for x in r["raw"][:12]:
            add("  %-45s ent=%-5s mid=%-6s edge=%-6s odd16=%-6s %s | %s" % (
                os.path.basename(x["file"]), x["entropy"], x["near_0x80"], x["near_0x00_0xFF"],
                x["odd_bytes_near_0"], x["auto"], x["first16"]))

        add("\n== Map grids")
        for m in r["map"]:
            add("  %s: %d bytes, width %s, height %s, remainder %s; other candidates %s"
                % (m["file"], m["size"], m.get("width"), m.get("height"), m.get("remainder"), m.get("top_widths")))
            add("     palette: %s" % m.get("palette"))
            add("     most common values: %s" % m.get("values"))

        add("\n== Palettes: %d, all 6-bit VGA: %s"
            % (len(r["pal"]), all(p.get("six_bit") for p in r["pal"])))

        errors = [x for k in ("cmp", "rle", "raw", "pal", "map") for x in r[k] if "error" in x]
        add("\n== Errors: %d" % len(errors))
        for x in errors[:40]:
            add("  %s: %s" % (x["file"], x["error"]))

        (self.out / "report.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

        with open(self.out / "cmp.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["file", "size", "header_u32", "out_size", "dims", "palette", "first16", "error"])
            for c in r["cmp"]:
                w.writerow([c["file"], c.get("size"), c.get("header_u32"), c.get("out_size"),
                            c.get("dims"), c.get("palette"), c.get("first16"), c.get("error")])
        with open(self.out / "rle_frames.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["file", "frame", "offset", "key", "w", "h", "x", "y", "decoded_rows", "decoded_max_x",
                        "leftmost_x", "fits", "warnings", "header25"])
            for x in r["rle"]:
                for fr in x.get("frames", []):
                    w.writerow([x["file"], fr["frame"], fr["offset"], fr["key"], fr["w"], fr["h"], fr["x"], fr["y"],
                                fr["rows"], fr["max_x"], fr["left"], fr["fits"], fr["warnings"], fr["header"]])
        with open(self.out / "raw.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["file", "size", "auto", "entropy", "near_0x80", "near_0x00_0xFF", "odd_bytes_near_0", "first16"])
            for x in r["raw"]:
                w.writerow([x["file"], x.get("size"), x.get("auto"), x.get("entropy"), x.get("near_0x80"),
                            x.get("near_0x00_0xFF"), x.get("odd_bytes_near_0"), x.get("first16")])
        self.log("\n" + "\n".join(lines[:60]))
        self.log("\nFull report: %s" % (self.out / "report.txt"))


def map_palette_sheets(out, discs, tile=200, cols=8):
    """One contact sheet per map: the map rendered with every palette on the discs."""
    out = Path(out)
    pals, maps = {}, {}
    for disc in discs:
        for p in sorted(Path(disc).rglob("*")):
            if p.suffix.upper() == ".PAL" and p.stat().st_size == 768:
                pals.setdefault(p.stem.upper(), vga_palette(p.read_bytes())[0])
            elif p.suffix.upper() == ".DAT" and p.parent.name.upper() == "MAPS":
                maps.setdefault(p.stem.upper(), p.read_bytes())
    names = sorted(pals)
    for mname, d in sorted(maps.items()):
        w, _ = guess_width(d)
        h = len(d) // w
        step = max(1, -(-max(w, h) // tile))                 # nearest-neighbour downscale
        tw, th = -(-w // step), -(-h // step)
        small = bytes(d[y * w + x] for y in range(0, h, step) for x in range(0, w, step))
        rows = -(-len(names) // cols)
        sw, sh = cols * (tw + 4), rows * (th + 4)
        sheet = bytearray(sw * sh * 3)
        index = []
        for i, pname in enumerate(names):
            pal = pals[pname]
            ox, oy = (i % cols) * (tw + 4), (i // cols) * (th + 4)
            for y in range(th):
                o = ((oy + y) * sw + ox) * 3
                sheet[o:o + tw * 3] = b"".join(pal[v * 3:v * 3 + 3] for v in small[y * tw:(y + 1) * tw])
            index.append("%3d  row %d col %d  %s" % (i + 1, i // cols + 1, i % cols + 1, pname))
        # write RGB as PNG colour type 2
        out.mkdir(parents=True, exist_ok=True)
        raw = b"".join(b"\x00" + bytes(sheet[y * sw * 3:(y + 1) * sw * 3]) for y in range(sh))
        png = b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0))
        png += _png_chunk(b"IDAT", zlib.compress(raw, 6)) + _png_chunk(b"IEND", b"")
        (out / ("%s_palettes.png" % mname)).write_bytes(png)
        (out / ("%s_palettes.txt" % mname)).write_text("\n".join(index) + "\n")
        print("%s: %dx%d, %d palettes -> %s" % (mname, w, h, len(names), out / ("%s_palettes.png" % mname)))


def probe_raw(out, path):
    d = Path(path).read_bytes()
    out = Path(out)
    st = raw_stats(d)
    print("%s: %d bytes" % (path, len(d)))
    for k, v in st.items():
        print("  %-18s %s" % (k, v))
    stem = Path(path).stem
    for fmt in ("u8", "s8", "s16"):
        for rate in (11025, 22050):
            if fmt == "s16" and len(d) % 2:
                continue
            p = out / ("%s_%s_%d.wav" % (stem, fmt, rate))
            write_wav(p, d, rate, fmt)
            print("  wrote", p)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    ex = sub.add_parser("extract", help="extract and convert everything")
    ex.add_argument("out")
    ex.add_argument("discs", nargs="+", help="disc roots, disc 1 first")
    ex.add_argument("--jobs", type=int, default=0)
    ex.add_argument("--limit", type=int, default=0)
    ex.add_argument("--only", default="")
    ex.add_argument("--raw-format", choices=("auto", "u8", "s8", "s16", "s16be"), default="auto")
    ex.add_argument("--raw-rate", type=int, default=22050)
    ex.add_argument("--map-palette", action="append", default=[], metavar="MAP=PAL",
                    help="palette for a deck map, e.g. --map-palette OPS=OPS03 (repeatable)")
    pr = sub.add_parser("probe-raw", help="write one RAW file in several formats to compare by ear")
    pr.add_argument("out")
    pr.add_argument("file")
    mp = sub.add_parser("map-palettes", help="render each MAPS/*.DAT with every palette (contact sheets)")
    mp.add_argument("out")
    mp.add_argument("discs", nargs="+")
    args = ap.parse_args(argv)
    if args.cmd == "map-palettes":
        map_palette_sheets(args.out, args.discs)
    elif args.cmd == "probe-raw":
        probe_raw(args.out, args.file)
    else:
        Extractor(args.out, args.discs, args).run()


if __name__ == "__main__":
    main()
