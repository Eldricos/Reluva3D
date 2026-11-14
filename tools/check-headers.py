#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Checks that every source file starts with Reluva's license lines.

    python3 tools/check-headers.py [--fix]

Every source file in the repository (as git lists it), but for
third_party/ and the data files below, starts with

    SPDX-License-Identifier: GPL-3.0-or-later
    Copyright (C) <year> The Reluva contributors

in its own comments (// for C and C++, # for Python, shell and CMake,
; for QVM assembly and .def files), after a #! line if it has one: the
copyright line and the pointer to the license that LICENSE's "How to
Apply These Terms" asks of each file (docs/CODING_GUIDE.md). It prints
each file without them and exits with 1 if there is any.

--fix adds them where they're missing, with this year.
"""

import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"
# @YEAR@ in cmake/engine-template/, which NewEngine.cmake fills in.
COPYRIGHT = re.compile(r"Copyright \(C\) (\d{4}(-\d{4})?|@YEAR@) The Reluva contributors")

COMMENT = {}
for ext in ("c", "cc", "cpp", "h", "hpp", "glsl", "hlsl", "vert", "frag", "comp", "metal", "java", "js"):
    COMMENT["." + ext] = "//"
for ext in ("py", "sh", "cmake", "ps1"):
    COMMENT["." + ext] = "#"
for ext in ("asm", "def"):
    COMMENT["." + ext] = ";"

# Not source, though their names say so: data that tools write.
DATA = {
    # A new engine's test record, written by reluva-tests --update; no
    # record file carries license lines.
    "cmake/engine-template/engine-record.txt.in",
}


def comment_of(path):
    """The comment marker for a file, or None if it isn't source."""
    name = os.path.basename(path)
    if name.endswith(".in"):
        name = name[:-3]
    if name == "CMakeLists.txt":
        return "#"
    ext = os.path.splitext(name)[1]
    if ext in COMMENT:
        return COMMENT[ext]
    if not ext:
        with open(os.path.join(ROOT, path), "rb") as f:
            if f.read(2) == b"#!":
                return "#"
    return None


def source_files():
    names = subprocess.run(["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True).stdout
    for path in names.split("\n"):
        if not path or path.startswith("third_party/") or path in DATA:
            continue
        if not os.path.isfile(os.path.join(ROOT, path)):
            continue
        marker = comment_of(path)
        if marker:
            yield path, marker


def has_header(lines):
    head = lines[:6]
    spdx = next((i for i, l in enumerate(head) if SPDX in l), None)
    return spdx is not None and spdx + 1 < len(lines) and COPYRIGHT.search(lines[spdx + 1]) is not None


def add_header(path, marker, lines):
    year = datetime.date.today().year
    header = [f"{marker} {SPDX}", f"{marker} Copyright (C) {year} The Reluva contributors"]
    at = 1 if lines and lines[0].startswith("#!") else 0
    rest = lines[at:]
    if rest and rest[0].strip():
        header.append("")
    new = lines[:at] + header + rest
    with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(new))


def main(argv):
    fix = "--fix" in argv
    missing = []
    for path, marker in sorted(source_files()):
        with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace", newline="") as f:
            lines = f.read().split("\n")
        if has_header(lines):
            continue
        missing.append(path)
        if fix:
            add_header(path, marker, lines)
    for path in missing:
        print(f"{path}: {'license lines added' if fix else 'no license lines'}")
    print(f"{len(missing)} file{'' if len(missing) == 1 else 's'} {'fixed' if fix else 'without license lines'}")
    return 0 if fix or not missing else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
