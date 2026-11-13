#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Checks Reluva's C++ files with clang-tidy, for the rules in
docs/CODING_GUIDE.md that clang-format can't see (.clang-tidy: naming,
nullptr, override, const-correctness...).

    python3 tools/tidy.py                    every file
    python3 tools/tidy.py --changed          only files changed since they
                                             left main (or --changed <base>)
    python3 tools/tidy.py <file or folder>...   only those
    python3 tools/tidy.py --fix --checks=-*,readability-isolate-declaration src/engines/clue
                                             apply one check's fixes

Prints each finding and a count per check; exits 1 if there was any.

clang-tidy reads how each file is compiled from a compile database. This
makes one in build-tidy/ (or --build <folder>), configured with every
engine enabled, newworlds and other disabled ones included, so no code is
left unchecked; options after --cmake go to that CMake configure. Only
Reluva's own files are checked, not SDL's or ImGui's, which the database
lists too. Needs clang-tidy 18 (and its run-clang-tidy, and for --fix
clang-apply-replacements): set CLANG_TIDY, RUN_CLANG_TIDY and
CLANG_APPLY_REPLACEMENTS to the programs if they aren't found under those
names, with or without -18.
"""

import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from format import changed_since, find_tool, git  # noqa: E402

FILES = r"^(?!.*/_deps/).*/(src|tests)/.*\.cpp$"


def engines_file(build):
    """engines.cfg with every engine enabled."""
    out = []
    for line in open("engines.cfg", encoding="utf-8"):
        out.append(re.sub(r"^([A-Za-z0-9_]+)=\w+", r"\1=enabled", line))
    path = os.path.join(build, "engines-all.cfg")
    os.makedirs(build, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(out)
    return os.path.abspath(path)


def main():
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    cmake_args = []
    if "--cmake" in args:
        i = args.index("--cmake")
        cmake_args, args = args[i + 1:], args[:i]
    build = "build-tidy"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    fix = "--fix" in args
    checks = [a for a in args if a.startswith("--checks=")]
    args = [a for a in args if a != "--fix" and not a.startswith("--checks=")]
    paths = None
    if "--changed" in args:
        i = args.index("--changed")
        base = args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("-") else None
        paths = changed_since(base)
        del args[i:i + (2 if base else 1)]
    if args:
        named = [os.path.relpath(os.path.abspath(a)).replace(os.sep, "/") for a in args]
        paths = named if paths is None else [p for p in paths if any(p == n or p.startswith(n + "/") for n in named)]

    clang_tidy = find_tool("clang-tidy", "CLANG_TIDY")
    run = os.environ.get("RUN_CLANG_TIDY") or next(
        (p for p in ("run-clang-tidy-18", "run-clang-tidy")
         if subprocess.run(["which", p], capture_output=True).returncode == 0), None)
    if not run:
        sys.exit("run-clang-tidy (from clang-tidy 18) not found: set RUN_CLANG_TIDY to it")

    # The compile database, made once and kept up to date by CMake.
    print(f"Configuring {build}/ (every engine enabled)...", flush=True)
    configure = ["cmake", "-S", ".", "-B", build, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                 f"-DRELUVA_ENGINES_FILE={engines_file(build)}", *cmake_args]
    done = subprocess.run(configure, capture_output=True, text=True)
    if done.returncode != 0:
        print(done.stdout[-3000:], done.stderr[-3000:], file=sys.stderr)
        sys.exit("CMake configure failed")

    # Which files: Reluva's .cpp files, or those of them named or changed.
    # A header is checked through the .cpp files that include it.
    pattern = FILES
    if paths is not None:
        sources = [p for p in paths if p.endswith(".cpp")]
        headers = [p for p in paths if p.endswith(".h")]
        if headers:
            # Its own .cpp, and else every file including it.
            for h in headers:
                own = h[:-2] + ".cpp"
                if os.path.isfile(own):
                    sources.append(own)
                else:
                    inc = h.split("/", 1)[1] if h.startswith("src/") else h
                    sources += git("grep", "-l", f'#include "{inc}"', "--", "src", "tests").splitlines()
        folders = [p.rstrip("/") + "/" for p in paths if os.path.isdir(p)]
        sources = sorted(set(sources))
        if not sources and not folders:
            print("No C++ files to check.")
            return 0
        alts = [re.escape(s) + "$" for s in sources] + [re.escape(f) + r".*\.cpp$" for f in folders]
        pattern = r"^(?!.*/_deps/).*/(" + "|".join(alts) + ")"

    # run-clang-tidy spells it -checks.
    command = [run, "-p", build, "-quiet", f"-clang-tidy-binary={clang_tidy}",
               *["-checks=" + c.split("=", 1)[1] for c in checks]]
    if fix:
        # The program that applies the fixes, once each, however many files
        # include the header they're in.
        apply = os.environ.get("CLANG_APPLY_REPLACEMENTS") or next(
            (p for p in ("clang-apply-replacements-18", "clang-apply-replacements")
             if subprocess.run(["which", p], capture_output=True).returncode == 0), None)
        if not apply:
            sys.exit("--fix: clang-apply-replacements (from clang-tidy 18) not found: "
                     "set CLANG_APPLY_REPLACEMENTS to it")
        command += ["-fix", f"-clang-apply-replacements-binary={apply}"]
    command.append(pattern)
    print("Running clang-tidy...", flush=True)
    done = subprocess.run(command, capture_output=True, text=True)
    root = os.getcwd() + os.sep
    findings = collections.OrderedDict()
    for line in (done.stdout + done.stderr).splitlines():
        m = re.match(r"^(.*?):(\d+):(\d+): (warning|error): (.*) \[([\w.,-]+)\]$", line)
        if m and m.group(1).startswith(root):
            path = m.group(1)[len(root):]
            # With files named or changed, only their findings: not those in
            # the headers they include. A file that doesn't compile always.
            if (paths is not None and m.group(4) != "error"
                    and not any(path == p or path.startswith(p.rstrip("/") + "/") for p in paths)):
                continue
            key = (path, int(m.group(2)), int(m.group(3)), m.group(6))
            findings[key] = f"{key[0]}:{key[1]}:{key[2]}: {m.group(4)}: {m.group(5)} [{m.group(6)}]"
    for text in findings.values():
        print(text)
    counts = collections.Counter(k[3] for k in findings)
    for check, n in counts.most_common():
        print(f"{n:6}  {check}")
    print(f"{len(findings)} findings" + (" (fixes applied)" if fix else ""))
    if done.returncode != 0 and not findings:
        print((done.stdout + done.stderr)[-3000:], file=sys.stderr)
        return 2
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
