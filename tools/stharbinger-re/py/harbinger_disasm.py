#!/usr/bin/env python3
"""Disassembles part of DEEP.EXE's code object, with fixup targets, string
references and handler names, as used for docs/engines/stharbinger/research.

    python3 harbinger_disasm.py DEEP.EXE START END [NAMES]

START and END are linear addresses in hex (code object at 0x10000, data at
0x50000). NAMES defaults to docs/engines/stharbinger/research/handler-addresses.txt.
Needs the capstone package (pip install capstone).
"""

import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the sibling modules, also under -I
import harbinger_strings as HS


def load_names(path):
    names = {}
    if path and os.path.exists(path):
        for line in open(path):
            parts = line.split()
            if len(parts) >= 2:
                names[int(parts[0], 16)] = parts[1]
    return names


def disassemble(exe, start, end, names, out):
    le = HS.LE(open(exe, "rb").read())
    code = le.objects[0]
    img, base = code["image"], code["base"]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    strings = dict(s for o in le.objects for s in HS.find_strings(o))
    fixups = {s: t for s, t, _ in le.fixups}
    p = start
    while p < end:
        for i in md.disasm(img[p - base:end - base], p):
            note = ""
            for x in range(i.address, i.address + i.size):
                if x in fixups:
                    t = fixups[x]
                    if t in strings:
                        note += "  ; ->%08X %r" % (t, strings[t][:50])
                    else:
                        note += "  ; ->%08X%s" % (t, " " + names[t] if t in names else "")
            if i.mnemonic == "call" or i.mnemonic.startswith("j"):
                try:
                    note += "  ; " + names.get(int(i.op_str, 16), "")
                except ValueError:
                    pass
            if i.address in names:
                out.write("\n%s:\n" % names[i.address])
            out.write("%08X  %-7s %-40s%s\n" % (i.address, i.mnemonic, i.op_str, note))
            p = i.address + i.size
        if p < end:
            out.write("%08X  db %02x\n" % (p, img[p - base]))
            p += 1


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "engines",
                           "stharbinger", "research", "handler-addresses.txt")
    names = load_names(sys.argv[4] if len(sys.argv) > 4 else default)
    disassemble(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), names, sys.stdout)


if __name__ == "__main__":
    main()
