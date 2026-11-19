#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""String and cross-reference map for DEEP.EXE (Star Trek: DS9 Harbinger, DOS/4GW).

    python3 harbinger_strings.py DEEP.EXE OUT_DIR

DEEP.EXE is the DOS/4GW extender followed by the game itself (a small MZ stub plus a
32-bit LE executable). This tool reads the LE header, loads both objects (code at
0x10000, data at 0x50000, the addresses IDA and Ghidra use), and walks the fixup table:
every 32-bit pointer the loader relocates. That tells exactly which code (or which data
table) refers to each string.

Writes:
    strings.txt    every string with its address, category and referencing addresses
    functions.txt  code addresses named after "Func_Name: message" debug strings
    tables.txt     arrays of string pointers in the data object (asset name lists etc.)
    files.txt      file names, extensions and path fragments the engine uses
    strings.csv    the same information for a spreadsheet
"""

import collections
import csv
import re
import struct
import sys
from pathlib import Path


class LE:
    def __init__(self, data):
        self.d = d = data
        self.base_mz, self.le = self.find_le(d)
        I = lambda o: struct.unpack_from("<I", d, self.le + o)[0]
        self.page_size = I(0x28)
        self.npages = I(0x14)
        self.last_page = I(0x2C)
        self.entry = (I(0x18), I(0x1C))
        data_pages = self.base_mz + I(0x80)              # relative to the program's own MZ stub
        ot = self.le + I(0x40)
        self.objects = []
        for i in range(I(0x44)):
            vsize, base, flags, first, count, _ = struct.unpack_from("<6I", d, ot + 24 * i)
            self.objects.append({"n": i + 1, "vsize": vsize, "base": base, "flags": flags,
                                 "first": first, "count": count,
                                 "kind": "code" if flags & 4 else "data"})
        pmt = self.le + I(0x48)
        self.page_file = []
        for i in range(self.npages):
            num = (d[pmt + 4 * i] << 16) | (d[pmt + 4 * i + 1] << 8) | d[pmt + 4 * i + 2]
            self.page_file.append(data_pages + (num - 1) * self.page_size)
        for o in self.objects:
            img = bytearray(o["vsize"])
            for k in range(o["count"]):
                pg = o["first"] - 1 + k
                size = self.last_page if pg == self.npages - 1 else self.page_size
                chunk = d[self.page_file[pg]:self.page_file[pg] + size]
                img[k * self.page_size:k * self.page_size + len(chunk)] = chunk[:max(0, o["vsize"] - k * self.page_size)]
            o["image"] = bytes(img)
        self.fixups = self.read_fixups(I(0x68), I(0x6C))

    @staticmethod
    def find_le(d):
        for m in re.finditer(b"LE\x00\x00\x00\x00\x00\x00", d):
            le = m.start()
            if 1 <= struct.unpack_from("<H", d, le + 8)[0] <= 4:
                mz = d.rfind(b"MZ", 0, le)
                while mz > 0 and struct.unpack_from("<I", d, mz + 0x3C)[0] != le - mz:
                    mz = d.rfind(b"MZ", 0, mz)
                return mz, le
        raise SystemExit("no LE header found")

    def read_fixups(self, fpt_off, frt_off):
        d, fpt, frt = self.d, self.le + fpt_off, self.le + frt_off
        offs = struct.unpack_from("<%dI" % (self.npages + 1), d, fpt)
        page_obj = {}
        for o in self.objects:
            for k in range(o["count"]):
                page_obj[o["first"] - 1 + k] = (o, k)
        out = []                                       # (source VA, target VA, source type)
        for pg in range(self.npages):
            obj, k = page_obj[pg]
            p, end = frt + offs[pg], frt + offs[pg + 1]
            while p < end:
                src, fl = d[p], d[p + 1]
                p += 2
                if src & 0x20:
                    cnt = d[p]
                    p += 1
                else:
                    srcoff = struct.unpack_from("<h", d, p)[0]
                    p += 2
                if fl & 3:
                    raise SystemExit("import/entry fixups are not handled (flags %#x)" % fl)
                tobj = struct.unpack_from("<H", d, p)[0] if fl & 0x40 else d[p]
                p += 2 if fl & 0x40 else 1
                toff = 0
                if src & 0xF != 2:
                    toff = struct.unpack_from("<I" if fl & 0x10 else "<H", d, p)[0]
                    p += 4 if fl & 0x10 else 2
                srcs = [srcoff] if not src & 0x20 else \
                    [struct.unpack_from("<h", d, p + 2 * i)[0] for i in range(cnt)]
                if src & 0x20:
                    p += 2 * cnt
                tva = self.objects[tobj - 1]["base"] + toff
                for so in srcs:
                    out.append((obj["base"] + k * self.page_size + so, tva, src & 0xF))
        return out

    def obj_at(self, va):
        for o in self.objects:
            if o["base"] <= va < o["base"] + o["vsize"]:
                return o
        return None


def find_strings(obj, minlen=4):
    out = []
    for m in re.finditer(rb"[\x20-\x7e\t\r\n]{%d,}\x00" % minlen, obj["image"]):
        s = m.group()[:-1].decode("latin-1")
        if sum(c.isalnum() for c in s) >= max(3, len(s) // 3):
            out.append((obj["base"] + m.start(), s))
    return out


FILE_RE = re.compile(r"(?i)(^|[\\/])[\w\-]+\.(cmp|rle|raw|pal|smk|dat|all|lst|cat|mus|vce|ini|dig|mgd|sav|cfg|txt|ani|pcx|fnt|exe)$|^\.\w{2,3}$|\\")
FUNC_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]{2,}):\s")
# Other ways the debug messages name the function they come from.
NAME_RES = [FUNC_RE,
            re.compile(r"^\s*In ([A-Z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)\b"),              # "In Load_Dialogues: ..."
            re.compile(r"^\s*([A-Z][a-z]+(?: [A-Z][a-z]+)+) / (cc[A-Za-z]+)"),         # "Execute Scene Directive / ccSUB"
            re.compile(r"^\s*([A-Z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)$")]                  # "Load_Directive_List"


def function_name(s):
    for rx in NAME_RES:
        m = rx.match(s)
        if m:
            return m.group(1).replace(" ", "_") + ("/" + m.group(2) if rx.groups == 2 else "")
    return None


def category(s):
    if FILE_RE.search(s.strip()):
        return "file"
    if function_name(s):
        return "debug"
    if "%" in s and re.search(r"%[-0-9.]*[dsxufcl]", s):
        return "format"
    if re.search(r"(?i)watcom|copyright|dos/4g|run-time|stack overflow|floating", s):
        return "runtime"
    return "text"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    exe, out = Path(sys.argv[1]), Path(sys.argv[2])
    le = LE(exe.read_bytes())
    out.mkdir(parents=True, exist_ok=True)

    refs = collections.defaultdict(list)               # target VA -> [source VA]
    for s, t, _ in le.fixups:
        refs[t].append(s)
    strings = []
    for o in le.objects:
        strings += find_strings(o)
    by_va = dict(strings)

    # Function names from "Name: message" strings referenced by code.
    funcs = collections.defaultdict(set)
    for va, s in strings:
        name = function_name(s)
        if name:
            for src in refs.get(va, []):
                if le.obj_at(src)["kind"] == "code":
                    funcs[name].add(src)

    # Arrays of pointers to strings inside the data object.
    data_ptrs = sorted((s, t) for s, t, _ in le.fixups
                       if le.obj_at(s)["kind"] == "data" and t in by_va)
    tables, run = [], []
    for s, t in data_ptrs:
        if run and s - run[-1][0] in (4, 8, 12, 16) and (not run[1:] or s - run[-1][0] == run[1][0] - run[0][0]):
            run.append((s, t))
        else:
            if len(run) >= 3:
                tables.append(run)
            run = [(s, t)]
    if len(run) >= 3:
        tables.append(run)

    def where(vas):
        return " ".join("%s:%08X" % ("C" if le.obj_at(v)["kind"] == "code" else "D", v) for v in sorted(vas)[:12]) + \
               (" ..." if len(vas) > 12 else "")

    lines = ["# DEEP.EXE strings: address  category  [referenced from: C=code D=data]  text", ""]
    rows = []
    for va, s in strings:
        cat = category(s)
        r = refs.get(va, [])
        lines.append("%08X  %-7s %-40s %r" % (va, cat, where(r) if r else "(no direct reference)", s))
        rows.append([("%08X" % va), cat, len(r), " ".join("%08X" % v for v in sorted(r)), s])
    (out / "strings.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    with open(out / "strings.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["address", "category", "ref_count", "referenced_from", "text"])
        w.writerows(rows)

    fl = ["# Code addresses inside functions, named from their own debug messages", ""]
    for name in sorted(funcs, key=lambda n: min(funcs[n])):
        fl.append("%-40s %s" % (name, " ".join("%08X" % v for v in sorted(funcs[name]))))
    (out / "functions.txt").write_text("\n".join(fl) + "\n", encoding="utf-8")

    tl = ["# Arrays of string pointers in the data object (address, stride, entries)", ""]
    for t in tables:
        stride = t[1][0] - t[0][0]
        code_refs = where(refs.get(t[0][0], []))
        tl.append("table at %08X, stride %d, %d entries, used by %s" % (t[0][0], stride, len(t), code_refs or "(no direct reference)"))
        for s, tv in t:
            tl.append("    %08X -> %08X %r" % (s, tv, by_va[tv]))
        tl.append("")
    (out / "tables.txt").write_text("\n".join(tl) + "\n", encoding="utf-8")

    files = [(va, s) for va, s in strings if category(s) == "file"]
    (out / "files.txt").write_text("\n".join("%08X  %-36s %r" % (va, where(refs.get(va, [])) or "-", s)
                                             for va, s in files) + "\n", encoding="utf-8")

    counts = collections.Counter(category(s) for _, s in strings)
    print("program at file offset %#x, LE header at %#x, entry point %08X" %
          (le.base_mz, le.le, le.objects[le.entry[0] - 1]["base"] + le.entry[1]))
    for o in le.objects:
        print("  object %d (%s): %08X-%08X" % (o["n"], o["kind"], o["base"], o["base"] + o["vsize"]))
    print("%d fixups, %d strings (%s)" % (len(le.fixups), len(strings),
          ", ".join("%s %d" % kv for kv in counts.most_common())))
    print("%d named functions, %d string tables -> %s" % (len(funcs), len(tables), out))


if __name__ == "__main__":
    main()
