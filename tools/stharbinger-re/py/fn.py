#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Which function of DEEP.EXE each address is in, from the list of function
starts and the handler names in docs/engines/stharbinger/research/.

    python3 -I fn.py ADDRESS [ADDRESS...]      (hex, e.g. 22E91)
"""

import bisect
import os
import sys

RESEARCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "engines",
                        "stharbinger", "research")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    starts = sorted(int(l.split()[0], 16) for l in open(os.path.join(RESEARCH, "function-starts.txt")) if l.strip())
    names = {}
    for line in open(os.path.join(RESEARCH, "handler-addresses.txt")):
        parts = line.split()
        if len(parts) >= 2:
            names[int(parts[0], 16)] = parts[1]
    for arg in sys.argv[1:]:
        a = int(arg, 16)
        i = bisect.bisect_right(starts, a) - 1
        if i < 0:
            print("%X before the first function" % a)
            continue
        nxt = starts[i + 1] if i + 1 < len(starts) else 0
        print("%X in %X %s (next %X)" % (a, starts[i], names.get(starts[i], ""), nxt))


if __name__ == "__main__":
    main()
