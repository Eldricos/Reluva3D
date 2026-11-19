#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Flags comments that break docs/CODING_GUIDE.md's Comments rules in ways
a script can be sure of:

    commented-out code       // x = f(y);  (delete it: git keeps it)
    history                  // Fixed: ..., // previously ..., bug fix
                             (the commit message says what changed)
    empty Doxygen tag        @param size The size.  (says nothing the
                             name doesn't; give more or leave it out)
    bare TODO / FIXME        // TODO  (say what's missing and why)

Whether a comment only repeats the code is for a person to judge; this
doesn't try.

    python3 tools/comments.py                every file
    python3 tools/comments.py <file or folder>...   only those
    python3 tools/comments.py --changed      only lines added since the
                                             branch left main (or
                                             --changed <base>)
    --github                                 as GitHub warnings, exiting 0
                                             (CI's Code style job)

Exits 1 if it flags anything (but with --github). Generated files
(.clang-format-ignore) and third_party/ are left out.
"""

import fnmatch
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from format import git, select  # noqa: E402

# Code a comment line can't be mistaken for: a statement, a block's end,
# a preprocessor line.
CODE = re.compile(
    r"^(?:"
    r"(?:if|for|while|switch)\s*\(.*\)\s*[{;]?"
    r"|(?:return|break|continue|goto)\b[^.]*;"
    r"|#\s*(?:include|define|if|ifdef|ifndef|endif|else|pragma)\b.*"
    r"|[}{]\s*;?"
    r"|(?:[A-Za-z_][\w:<>,]*[\s*&]+)?[A-Za-z_][\w.\->\[\]]*\s*(?:[-+*/%&|^]?=|\+\+|--)\s*[^;]*;"
    r"|[A-Za-z_][\w:.\->]*\([^;]*\)\s*;"
    r")$"
)
# Words only a change log uses. Anywhere in a comment:
HISTORY = re.compile(
    r"\b(?:previously|used to be|was changed|instead of the old|"
    r"fixed (?:a |the )?(?:bug|crash|issue)|bug ?fix|as requested|per review)\b",
    re.IGNORECASE,
)
# At the start of a comment's first line (a later line of a wrapped
# comment can start with any word):
HISTORY_START = re.compile(r"^(?:fixed|fix|changed|updated|refactored|removed|added|new|old):", re.IGNORECASE)
FILLER = {"the", "a", "an", "to", "of", "for", "in", "on", "is", "this", "that", "given", "value"}
TAG = re.compile(r"[@\\](param(?:\[[a-z,]+\])?)\s+(\w+)\s*(.*)$|[@\\](returns?)\s*(.*)$")
BARE = re.compile(r"\b(TODO|FIXME)\b[\s:.\-]*(.*)$")


def words(name):
    """A name's words: cameCase and snake_case split, lowercase."""
    parts = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name.lstrip("_k")).replace("_", " ")
    return {w.lower() for w in parts.split()}


def comment_of(line, in_block):
    """The comment text on a line, whether a block comment goes on past
    it, and whether the line was only a comment."""
    s = line.strip()
    if in_block:
        end = s.find("*/")
        text = s if end < 0 else s[:end]
        return text.lstrip("*").strip(), end < 0, True
    # A // or /* outside a string.
    stripped = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m: " " * len(m.group()), line)
    i, j = stripped.find("//"), stripped.find("/*")
    if i >= 0 and (j < 0 or i < j):
        return line[i + 2:].lstrip("/<!").rstrip(), False, not stripped[:i].strip()
    if j >= 0:
        rest = line[j + 2:]
        end = rest.find("*/")
        text = rest if end < 0 else rest[:end]
        return text.lstrip("*!<").strip(), end < 0, not stripped[:j].strip()
    return None, False, False


def check(path, lines, only):
    """(line number, message) for each problem, on the lines in `only`
    (None: all)."""
    out = []
    in_block = False
    previous = None  # the comment line before, if the line before was one
    for n, line in enumerate(lines, 1):
        text, in_block_next, alone = comment_of(line, in_block)
        was_block = in_block
        in_block = in_block_next
        # A line carrying on the one before's sentence.
        continued = bool(previous) and not previous.endswith((".", ":", "!", "?", ";", "{", "}"))
        previous = text.strip() if text is not None and alone else None
        if text is None or (only is not None and n not in only):
            continue
        # Indented under a comment's text: an example of using the code.
        example = text.startswith(("  ", "\t"))
        text = text.strip()
        if alone and not was_block and not example and CODE.match(text):
            out.append((n, "commented-out code: delete it (git keeps it)"))
        if HISTORY.search(text) or (not continued and HISTORY_START.match(text)):
            out.append((n, "history: what changed goes in the commit message, not a comment"))
        m = TAG.search(text)
        if m:
            if m.group(1):
                rest = [w.lower().strip(".,") for w in m.group(3).split()]
                if not [w for w in rest if w not in FILLER and w not in words(m.group(2))]:
                    out.append((n, f"empty Doxygen tag: @param {m.group(2)} says only its name; say more or leave it out"))
            elif not [w for w in m.group(5).lower().strip(".").split() if w not in FILLER | {"result"}]:
                out.append((n, "empty Doxygen tag: @return says nothing; say what it returns or leave it out"))
        m = BARE.search(text)
        if m and len(m.group(2).split()) < 3:
            out.append((n, f"bare {m.group(1)}: say what's missing and why"))
    return out


def added_lines(base):
    """{file: set of line numbers} added since HEAD left base."""
    if base is None:
        for candidate in ("origin/main", "main"):
            if subprocess.run(["git", "rev-parse", "-q", "--verify", candidate], capture_output=True).returncode == 0:
                base = candidate
                break
        else:
            sys.exit("--changed: no main branch to compare with; name one: --changed <base>")
    fork = git("merge-base", base, "HEAD").strip()
    out, path = {}, None
    for line in git("diff", "-U0", "--diff-filter=d", fork).splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@") and path:
            m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
            first, count = int(m.group(1)), int(m.group(2) or 1)
            out.setdefault(path, set()).update(range(first, first + count))
    return out


def main():
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    github = "--github" in args
    args = [a for a in args if a != "--github"]
    only = None
    if "--changed" in args:
        i = args.index("--changed")
        base = args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("-") else None
        del args[i:i + (2 if base else 1)]
        only = added_lines(base)
    paths = [os.path.relpath(os.path.abspath(a)).replace(os.sep, "/") for a in args] or None
    skip = [line.strip() for line in open(".clang-format-ignore", encoding="utf-8")
            if line.strip() and not line.startswith("#")]
    files = [f for f in select(paths) if not any(fnmatch.fnmatch(f, g) for g in skip)]
    if only is not None:
        files = [f for f in files if f in only]

    found = 0
    for f in files:
        lines = open(f, encoding="utf-8", errors="replace").read().split("\n")
        for n, message in check(f, lines, None if only is None else only[f]):
            found += 1
            if github:
                print(f"::warning file={f},line={n}::{message}")
            else:
                print(f"{f}:{n}: {message}")
    print(f"{found} comments flagged in {len(files)} files")
    return 1 if found and not github else 0


if __name__ == "__main__":
    sys.exit(main())
