#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Formats Reluva's C and C++ files with clang-format, as
docs/CODING_GUIDE.md has them (.clang-format).

    python3 tools/format.py                  format every file in place
    python3 tools/format.py --check          list the files that need it;
                                             exit 1 if there is any
    python3 tools/format.py --changed        only files changed since they
                                             left main (or --changed <base>)
    python3 tools/format.py <file or folder>...   only those
    python3 tools/format.py --verify <commit>
                                             check that every C/C++ file
                                             changed since <commit> differs
                                             only in layout: the same text
                                             once whitespace is removed
                                             (namespace closing comments
                                             aside). For reviewing a
                                             formatting commit:
                                             --verify <commit>~1 after
                                             checking it out.

Covers src/ and tests/, leaving out what .clang-format-ignore lists
(generated files) and what clang-format is switched off for (third_party/).
Needs clang-format 18: another version lays code out differently. Set
CLANG_FORMAT to the program to use if it isn't found as clang-format-18 or
clang-format.
"""

import concurrent.futures
import os
import re
import subprocess
import sys

VERSION = 18
SOURCES = (".c", ".cpp", ".h")
FOLDERS = ("src/", "tests/")


def git(*args):
    return subprocess.check_output(["git", *args], text=True)


def find_tool(name, env):
    """The tool of the right major version, or exit saying why."""
    candidates = [os.environ[env]] if os.environ.get(env) else [f"{name}-{VERSION}", name]
    for program in candidates:
        try:
            out = subprocess.check_output([program, "--version"], text=True, stderr=subprocess.STDOUT)
        except (OSError, subprocess.CalledProcessError):
            continue
        words = out.replace("-", " ").split()
        version = next((w for w in words[words.index("version") + 1:] if w[0].isdigit()), "") if "version" in words else ""
        if version.split(".")[0] == str(VERSION):
            return program
        sys.exit(f"{program} is version {version}; Reluva's style needs {name} {VERSION} "
                 f"(set {env} to the program to use)")
    sys.exit(f"{name} {VERSION} not found: install it, or set {env} to the program to use")


def changed_since(base):
    """Files changed between where HEAD left `base` and the working tree."""
    if base is None:
        for candidate in ("origin/main", "main"):
            if subprocess.run(["git", "rev-parse", "-q", "--verify", candidate], capture_output=True).returncode == 0:
                base = candidate
                break
        else:
            sys.exit("--changed: no main branch to compare with; name one: --changed <base>")
    fork = git("merge-base", base, "HEAD").strip()
    names = git("diff", "--name-only", "--diff-filter=d", fork).splitlines()
    names += git("ls-files", "--others", "--exclude-standard").splitlines()
    return sorted(set(names))


def select(paths):
    tracked = git("ls-files", *FOLDERS).splitlines() + git("ls-files", "--others", "--exclude-standard", *FOLDERS).splitlines()
    out = []
    for f in tracked:
        if not f.endswith(SOURCES) or not os.path.isfile(f):
            continue
        if paths is None or any(f == p or f.startswith(p.rstrip("/") + "/") for p in paths):
            out.append(f)
    return sorted(set(out))


NAMESPACE_COMMENT = re.compile(r"//\s*namespace\b[^\n]*")
WHITESPACE = re.compile(r"\s+")


def layout_free(text):
    """The text with its layout taken out: no whitespace, and no
    `// namespace x` closing comments (clang-format adds and corrects them)."""
    text = text.replace("\\\r\n", "").replace("\\\n", "")  # a line continuation is layout too
    return WHITESPACE.sub("", NAMESPACE_COMMENT.sub("", text))


def verify(base):
    """Every C/C++ file changed since `base`: the same once layout is taken
    out. Prints each that isn't, with where they first differ."""
    names = [f for f in git("diff", "--name-only", "-M", "--diff-filter=AMR", base).splitlines()
             if f.endswith(SOURCES)]
    renamed = {}
    for line in git("diff", "--name-status", "-M", "--diff-filter=R", base).splitlines():
        _, old, new = line.split("\t")
        renamed[new] = old
    bad = 0
    for f in names:
        try:
            before = git("show", f"{base}:{renamed.get(f, f)}")
        except subprocess.CalledProcessError:
            continue  # a new file: nothing to compare with
        with open(f, encoding="utf-8", newline="") as h:
            after = h.read()
        a, b = layout_free(before), layout_free(after)
        if a != b:
            bad += 1
            i = next((k for k, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            print(f"{f}: not only layout; first difference (whitespace removed):\n"
                  f"    before: ...{a[max(0, i - 40):i + 40]}...\n"
                  f"    after:  ...{b[max(0, i - 40):i + 40]}...")
    print(f"{len(names)} files changed since {base}; {bad} with more than layout changed")
    return 1 if bad else 0


def main():
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    if "--verify" in args:
        i = args.index("--verify")
        if i + 1 >= len(args):
            sys.exit("--verify <commit>: the commit to compare with")
        return verify(args[i + 1])
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    paths = None
    if "--changed" in args:
        i = args.index("--changed")
        base = args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("-") else None
        paths = changed_since(base)
        del args[i:i + (2 if base else 1)]
    if args:
        named = [os.path.relpath(os.path.abspath(a)).replace(os.sep, "/") for a in args]
        paths = named if paths is None else [p for p in paths if any(p == n or p.startswith(n + "/") for n in named)]
    files = select(paths)
    clang_format = find_tool("clang-format", "CLANG_FORMAT")

    def needs(f):
        # clang-format prints nothing for a file .clang-format-ignore lists.
        out = subprocess.run([clang_format, "--style=file", f], capture_output=True, text=True, encoding="utf-8")
        if out.returncode != 0:
            return f, None, out.stderr.strip()
        if out.stdout == "":
            return f, 0, None
        with open(f, encoding="utf-8", newline="") as h:
            old = h.read()
        if out.stdout == old:
            return f, 0, None
        a, b = old.splitlines(), out.stdout.splitlines()
        return f, sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b)), None

    with concurrent.futures.ThreadPoolExecutor() as pool:
        results = list(pool.map(needs, files))

    failed = [(f, e) for f, n, e in results if n is None]
    todo = [(f, n) for f, n, e in results if n]
    for f, e in failed:
        print(f"{f}: clang-format failed: {e}", file=sys.stderr)
    if check:
        for f, n in todo:
            print(f"{f}: {n} lines to format")
        print(f"{len(todo)} of {len(files)} files need formatting"
              + ("" if not todo else ": python3 tools/format.py <files>, or --changed"))
        return 1 if todo or failed else 0
    if todo:
        subprocess.check_call([clang_format, "-i", "--style=file", *[f for f, _ in todo]])
    print(f"formatted {len(todo)} of {len(files)} files")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
