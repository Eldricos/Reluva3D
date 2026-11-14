#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""A range of DEEP.EXE as data: each relocated dword (a pointer) with what it
points to (a string, when it is one), every other byte as it is. For reading
tables of pointers, such as jump tables and the video table.

    python3 -I cvtab.py DEEP.EXE START END      (hex addresses: code at 0x10000, data at 0x50000)
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the sibling modules, also under -I
import harbinger_strings as HS


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    le = HS.LE(open(sys.argv[1], "rb").read())
    start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
    strings = dict(s for o in le.objects for s in HS.find_strings(o))
    fixups = {s: t for s, t, _ in le.fixups}
    obj = next((o for o in le.objects if o["base"] <= start < o["base"] + len(o["image"])), None)
    if obj is None:
        sys.exit("%X is in no object" % start)
    p = start
    while p < end:
        if p in fixups:
            t = fixups[p]
            print("%08X dd %08X %r" % (p, t, strings.get(t, "")[:40]))
            p += 4
        else:
            print("%08X db %02X" % (p, obj["image"][p - obj["base"]]))
            p += 1


if __name__ == "__main__":
    main()
