#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""The values one argument slot of one command takes across every scene
script, with how often each (type, value) occurs: for working out what an
argument means.

    python3 -I raw.py CC.ALL COMMAND,SLOT [COMMAND,SLOT...]      (hex, e.g. 84,1)

CC.ALL is the scene scripts' archive, unpacked from disc 1's SSE/SSE.ALL;
its scripts are walked one after another, so no CC.LST is needed.
"""

import os
import collections
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the sibling modules, also under -I
from harbinger_script import NAMES, read_list


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    scenes, p = [], 0
    while p < len(data):
        try:
            p, directives = read_list(data, p)
        except Exception as e:  # the end of the archive, or damage
            print("stopped at %#x: %s" % (p, e))
            break
        scenes.append(directives)
    print("%d scripts" % len(scenes))
    for arg in sys.argv[2:]:
        op, slot = (int(x, 16) for x in arg.split(","))
        count = collections.Counter()
        for directives in scenes:
            for _, o, args in directives:
                if o == op:
                    for s, t, raw in args:
                        if s == slot:
                            count[(t, int.from_bytes(raw, "little"))] += 1
        print(NAMES.get(op), slot, sorted(count.items()))


if __name__ == "__main__":
    main()
