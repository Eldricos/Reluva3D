#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Runs Doxygen (the Doxyfile at the top) and lists what it found in the
headers that docs/CODING_GUIDE.md's Documentation asks for and is
missing: a public class, structure, function, enum or type alias without
a Doxygen block, and blocks Doxygen can't read (an unknown command, an
HTML tag that is really a <placeholder>: put it in `backticks`).

Undocumented data members aren't listed: the guide leaves a field's
comment a plain // one. Nor are overrides: their documentation is the
base's, which is listed itself if it has none (Doxygen can miss the
match when the two spell a type differently, gfx::Vertex and Vertex).

    python3 tools/check-doxygen.py                every header
    python3 tools/check-doxygen.py <file or folder>...   only those
    python3 tools/check-doxygen.py --changed      only headers changed since
                                                  the branch left main (or
                                                  --changed <base>)
    --github                                      as GitHub warnings, exiting 0
                                                  (CI's Docs job)
    --summary                                     a count per folder, not
                                                  each finding

The pages themselves are written to build-docs/html/. Exits 1 if it
lists anything (but with --github). Needs doxygen on the PATH (Ubuntu:
apt install doxygen).
"""

import collections
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from format import changed_since, git  # noqa: E402

OUTPUT = "build-docs"
WARNING = re.compile(r"^(.*?):(\d+): warning: (.*)$")
# A field: the guide keeps its comment a // one.
VARIABLE = re.compile(r"\(variable\) of .* is not documented")
# An override: documented by its base.
OVERRIDE = re.compile(r"\boverride (?:=0 )?\(function\) of .* is not documented")


def run_doxygen():
    """Doxygen's warnings, as (file relative to the top, line, text)."""
    if not shutil.which("doxygen"):
        sys.exit("doxygen isn't on the PATH (Ubuntu: apt install doxygen)")
    os.makedirs(OUTPUT, exist_ok=True)
    log = os.path.join(OUTPUT, "warnings.txt")
    subprocess.run(["doxygen", "Doxyfile"], check=True)
    top = os.getcwd().replace(os.sep, "/") + "/"
    out = []
    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = WARNING.match(line.rstrip("\n"))
            if not m:
                continue
            path = m.group(1).replace(os.sep, "/")
            if path.startswith(top):
                path = path[len(top):]
            out.append((path, int(m.group(2)), m.group(3)))
    return out


def folder(path):
    """The part of the tree a file counts towards in --summary."""
    parts = path.split("/")
    return "/".join(parts[:3] if parts[1:2] == ["engines"] else parts[:2])


def main():
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    github = "--github" in args
    summary = "--summary" in args
    args = [a for a in args if a not in ("--github", "--summary")]
    only = None
    if "--changed" in args:
        i = args.index("--changed")
        base = args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("-") else None
        del args[i:i + (2 if base else 1)]
        only = {f for f in changed_since(base) if f.endswith(".h")}
    paths = [os.path.relpath(os.path.abspath(a)).replace(os.sep, "/").rstrip("/") for a in args] or None

    # A base class's undocumented member is told once for each class that
    # inherits it.
    found = []
    for path, line, text in sorted(set(run_doxygen())):
        if VARIABLE.search(text) or OVERRIDE.search(text):
            continue
        if only is not None and path not in only:
            continue
        if paths is not None and not any(path == p or path.startswith(p + "/") for p in paths):
            continue
        found.append((path, line, text))

    if summary:
        counts = collections.Counter(folder(path) for path, _, _ in found)
        for name, count in sorted(counts.items()):
            print(f"{count:6}  {name}")
    else:
        for path, line, text in found:
            if github:
                print(f"::warning file={path},line={line}::{text}")
            else:
                print(f"{path}:{line}: {text}")
    print(f"{len(found)} Doxygen findings")
    return 1 if found and not github else 0


if __name__ == "__main__":
    sys.exit(main())
