#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Moves source files to where a relocation list says, and rewrites every
reference to them: #includes, CMake source lists, CI, scripts, comments
and the links in the docs.

    python3 tools/relocate.py tools/relocations/<list>.txt [--dry-run]

Safe to run more than once, and on any branch: what is still at an old
path is moved (git mv), and references are rewritten whether or not the
files were moved here. After merging main into a branch made before a
relocation, run it to bring the branch's own new code in line, then build.

A list has one move per line, "<old> <new>", repository-relative:
    src/common/cursor.h   src/graphics/cursor.h
    src/common/x86/       src/emulation/x86/      bare
A path ending in "/" is a folder: everything under it moves, and mentions
of the folder are rewritten. References are rewritten in these forms:
  - with src/ ("src/common/cursor.h"), anywhere, also in relative links
    ("../../src/common/cursor.h");
  - as an #include names it, without src/ ("common/cursor.h"), for files,
    and for folders marked "bare" (a folder's include form can look like
    other paths: an engine's tools/, or docs/common/video/; mark only
    distinctive ones). Never in a Markdown link target, which is relative
    to the page, nor in CMake files, which name files from src/ or
    relative to themselves;
  - relative to the CMakeLists.txt that lists a file ("board.cpp" in
    src/engines/clue/CMakeLists.txt becomes "game/board.cpp").
third_party/ is never touched. Lines starting with # are comments.
"""

import os
import re
import subprocess
import sys


def git(*args):
    return subprocess.check_output(["git", *args], text=True)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    dry = "--dry-run" in sys.argv

    files, folders = [], []  # (old, new), folders as (old, new, bare)
    for number, line in enumerate(open(sys.argv[1], encoding="utf-8"), 1):
        line = line.split("#", 1)[0].split()
        if not line:
            continue
        if len(line) not in (2, 3) or (len(line) == 3 and line[2] != "bare"):
            sys.exit(f"{sys.argv[1]}:{number}: expected '<old> <new> [bare]'")
        old, new = line[0], line[1]
        if old.endswith("/") != new.endswith("/"):
            sys.exit(f"{sys.argv[1]}:{number}: a folder moves to a folder")
        if old.endswith("/"):
            folders.append((old.rstrip("/"), new.rstrip("/"), len(line) == 3))
        else:
            files.append((old, new))

    tracked = [f for f in git("ls-files").splitlines() if f]
    untracked = [f for f in git("ls-files", "--others", "--exclude-standard").splitlines() if f]
    present = set(tracked)

    # What moves here: listed files still at the old path, and everything
    # still under a listed folder (a branch may have added files there).
    moves = {old: new for old, new in files if old in present}
    for old, new, _ in folders:
        for f in tracked:
            if f.startswith(old + "/") and f not in moves:
                moves[f] = new + f[len(old):]

    # Rewrites. Folder entries rewrite by prefix; file entries exactly.
    src_form, bare_form = {}, {}
    for old, new in list(files) + list(moves.items()):
        src_form[old] = new
        if old.startswith("src/") and new.startswith("src/"):
            bare_form[old[4:]] = new[4:]
    for old, new, bare in folders:
        src_form[old] = new
        if bare and old.startswith("src/") and new.startswith("src/"):
            bare_form[old[4:]] = new[4:]

    files_bare = {old[4:] for old, _ in list(files) + list(moves.items()) if old.startswith("src/")}

    def pattern(keys, before):
        keys = sorted(keys, key=len, reverse=True)
        return re.compile(before + "(" + "|".join(map(re.escape, keys)) + r")(?![\w.-])") if keys else None

    src_re = pattern(src_form, r"(?<![\w.-])")
    bare_re = pattern(bare_form, r"(?<![\w./-])")

    # A file listed relative to its CMakeLists.txt: the nearest one above
    # it (inside src/) that the new path is also under.
    cmake_rel = {}
    for old, new in list(files) + list(moves.items()):
        d = os.path.dirname(old)
        while d.startswith("src/"):
            lists = d + "/CMakeLists.txt"
            if lists in present or os.path.isfile(lists):
                if new.startswith(d + "/"):
                    cmake_rel.setdefault(lists, {})[old[len(d) + 1:]] = new[len(d) + 1:]
                break
            d = os.path.dirname(d)

    def rewrite(path, text):
        is_markdown = path.endswith(".md")
        is_cmake = path.endswith((".cmake", "CMakeLists.txt", ".cmake.in")) or os.path.basename(path) == "CMakeLists.txt.in"
        found = []
        for rx, table, kind in ((src_re, src_form, "src"), (bare_re, bare_form, "bare")):
            if not rx:
                continue
            for m in rx.finditer(text):
                if kind == "bare" and is_markdown and text[max(0, m.start() - 2):m.start()] == "](":
                    continue  # a link relative to the page: docs/common/... and the like
                if kind == "bare" and is_markdown and m.group(1) not in files_bare:
                    # A folder in prose: only its files ("common/x86/cpu.h"),
                    # not the folder alone or a page in it, which in the docs
                    # is more likely docs/common/x86/ than the source.
                    rest = re.match(r"/[\w./-]*", text[m.end():])
                    if not rest or rest.group(0) == "/" or rest.group(0).endswith(".md"):
                        continue
                if kind == "bare" and is_cmake:
                    continue  # CMake lists name files from src/, or relative to the list
                found.append((m.start(), m.end(), table[m.group(1)]))
        rel = cmake_rel.get(path) or cmake_rel.get(moves.get(path, ""), None)
        if rel:
            for m in pattern(rel, r"(?<![\w./${}-])").finditer(text):
                found.append((m.start(), m.end(), rel[m.group(1)]))
        found.sort()
        out, pos = [], 0
        for start, end, replacement in found:
            if start < pos:
                continue  # overlaps one already taken: the longer, earlier one wins
            out += [text[pos:start], replacement]
            pos = end
        out.append(text[pos:])
        return "".join(out), (len(out) - 1) // 2

    changed = {}
    for path in tracked + untracked:
        if path.startswith(("third_party/", "tools/relocations/")) or path == "tools/relocate.py":
            continue  # code from elsewhere; the lists and this script name old paths on purpose
        if not os.path.isfile(path):
            continue
        try:
            text = open(path, encoding="utf-8", newline="").read()
        except UnicodeDecodeError:
            continue
        new_text, count = rewrite(path, text)
        if new_text != text:
            changed[path] = (new_text, count)

    print(f"{len(moves)} files to move; {sum(c for _, c in changed.values())} references in {len(changed)} files")
    if dry:
        for old, new in sorted(moves.items()):
            print(f"  move {old} -> {new}")
        for path, (_, count) in sorted(changed.items()):
            print(f"  {count:5} {path}")
        return

    for path, (new_text, _) in changed.items():
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(new_text)
    for old, new in sorted(moves.items()):
        os.makedirs(os.path.dirname(new), exist_ok=True)
        subprocess.check_call(["git", "mv", old, new])
    # Folders left empty go.
    for old in sorted({os.path.dirname(o) for o in moves}, key=len, reverse=True):
        while old and os.path.isdir(old) and not os.listdir(old):
            os.rmdir(old)
            old = os.path.dirname(old)


if __name__ == "__main__":
    main()
