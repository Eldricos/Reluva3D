#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Writes ../smacker_videos.h from the files make_smacker.py (and FFmpeg) made.

    python3 make_header.py DIR    (the folder holding test2.smk, test2.rgb, ...)
"""
import os
import sys

FILES = [("test2.smk", "kSmackerTest2"), ("test2.rgb", "kSmackerTest2Rgb"), ("test2.pcm", "kSmackerTest2Pcm"),
         ("test4.smk", "kSmackerTest4"), ("test4.rgb", "kSmackerTest4Rgb"), ("test4.pcm", "kSmackerTest4Pcm"),
         ("long.smk", "kSmackerLong")]


def main():
    src = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "smacker_videos.h")
    with open(out, encoding="utf-8") as f:
        head = f.read().split("\n// test2.smk\n")[0]
    s = head
    for name, ident in FILES:
        d = open(os.path.join(src, name), "rb").read()
        s += "\n// %s\ninline constexpr uint8_t %s[%d] = {\n" % (name, ident, len(d))
        for i in range(0, len(d), 16):
            s += "    " + " ".join("0x%02x," % b for b in d[i:i + 16]) + "\n"
        s += "};\n"
    with open(out, "w", encoding="utf-8") as f:
        f.write(s)


if __name__ == "__main__":
    main()
