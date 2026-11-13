#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Splits class and struct members declared several to a line into one
declaration each, as docs/CODING_GUIDE.md asks ("Split variable
declarations"). clang-tidy's readability-isolate-declaration does it for
local variables only; this does it for members (and for namespace-scope
variables, which it doesn't do either).

    python3 tools/split_members.py                 list them
    python3 tools/split_members.py --fix           split them
    python3 tools/split_members.py --fix src/engines/clue tests/engines/clue
                                                   only those declared there

    int x = 0, y = 0; // where it is     becomes     // where it is
    const char *a, *b;                               int x = 0;
                                                     int y = 0;
                                                     const char *a;
                                                     const char *b;

A comment after the declaration is about all its names, so it goes on a
line of its own above them (a NOLINT comment stays on each line). A declaration
with a comment, a preprocessor line or a function pointer among its names
is listed and left for a person. Generated files (.clang-format-ignore)
are left: their scripts write them. Formats the files it changed.

Reads the compile database tools/tidy.py makes (build-tidy/, or --build
<folder>; run tidy.py once first), so every engine's code is seen. Needs
the Python bindings of libclang 18 (pip install clang==18.1.8) and
libclang 18 itself: set LIBCLANG to it if it isn't found.
"""

import concurrent.futures
import fnmatch
import glob
import json
import os
import re
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from format import git  # noqa: E402

FILES = re.compile(r"^(src|tests)/(?!third_party/).*\.(h|cpp)$")


def generated():
    """The patterns in .clang-format-ignore: generated files and tables,
    regenerated rather than edited."""
    return [line.strip() for line in open(".clang-format-ignore", encoding="utf-8")
            if line.strip() and not line.startswith("#")]


def outside_templates(text):
    """text without what's between < and > (template arguments)."""
    out, depth = [], 0
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">" and depth:
            depth -= 1
        elif not depth:
            out.append(ch)
    return "".join(out)


def libclang():
    import clang.cindex as ci

    path = os.environ.get("LIBCLANG")
    if not path:
        found = sorted(glob.glob("/usr/lib/llvm-18/lib/libclang-18.so*")) + sorted(
            glob.glob("/usr/lib/x86_64-linux-gnu/libclang-18.so*"))
        path = found[0] if found else None
    if path and not ci.Config.loaded:
        ci.Config.set_library_file(path)
    return ci


def compile_args(entry):
    args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    out, skip = [], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-c", entry["file"]):
            continue
        if a == "-o":
            skip = True
            continue
        out.append(a)
    return out


def groups_in(job):
    """The declarations of several names in one source file's translation unit."""
    root, entry = job
    ci = libclang()
    index = ci.Index.create()
    tu = index.parse(entry["file"], compile_args(entry))
    found = {}
    kinds = (ci.CursorKind.FIELD_DECL, ci.CursorKind.VAR_DECL)
    scopes = (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL, ci.CursorKind.UNION_DECL,
              ci.CursorKind.CLASS_TEMPLATE, ci.CursorKind.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION,
              ci.CursorKind.NAMESPACE, ci.CursorKind.TRANSLATION_UNIT, ci.CursorKind.LINKAGE_SPEC)

    def walk(cursor):
        for child in cursor.get_children():
            loc = child.extent.start
            if loc.file is None:
                continue
            path = os.path.relpath(loc.file.name, root).replace(os.sep, "/")
            if not FILES.match(path):
                continue
            if child.kind in kinds and cursor.kind in scopes:
                found.setdefault((path, loc.offset), {})[child.location.offset] = child.extent.end.offset
            if child.kind in scopes or child.kind in (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD,
                                                      ci.CursorKind.CONSTRUCTOR, ci.CursorKind.FUNCTION_TEMPLATE,
                                                      ci.CursorKind.COMPOUND_STMT, ci.CursorKind.DECL_STMT):
                walk(child)

    walk(tu.cursor)
    return {k: v for k, v in found.items() if len(v) > 1}


def split_top_level(text):
    """text split at the commas outside brackets."""
    parts, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return parts


def rewrite(source, start, names):
    """The declaration at start split, or a reason it isn't. names: name
    offset -> end offset of each declared name, in order."""
    name_offsets = sorted(names)
    end = max(names.values())
    semi = source.find(";", end)
    if semi < 0:
        return None, "no ';'"
    statement = source[start:semi]
    if "//" in statement or "/*" in statement or "\n#" in statement:
        return None, "a comment or preprocessor line inside"
    base = source[start:name_offsets[0]].rstrip(" \t*&")
    if "(" in outside_templates(base) or ")" in outside_templates(base):
        return None, "a function pointer or attribute with arguments"
    rest = statement[len(base):]
    pieces = split_top_level(rest)
    if len(pieces) != len(names):
        return None, f"{len(pieces)} parts for {len(names)} names"
    if any("(" in p.split("=")[0] for p in pieces):
        return None, "a function pointer or array of them"
    line_start = source.rfind("\n", 0, start) + 1
    indent = source[line_start:start]
    if indent.strip():
        return None, "not at the start of its line"
    base = " ".join(base.split())
    lines = [f"{base} {' '.join(p.split())};" for p in pieces]
    # A comment after it: about all the names, so on a line of its own
    # above them; a NOLINT stays on each line, the only place it works.
    line_end = source.find("\n", semi)
    after = source[semi + 1:line_end if line_end >= 0 else len(source)].strip()
    if after.startswith("//"):
        if "NOLINT" in after:
            lines = [f"{line} {after}" for line in lines]
        else:
            lines.insert(0, after)
        semi = line_end if line_end >= 0 else len(source)
    elif after:
        return None, "more code after it on its line"
    else:
        semi += 1
    return (("\n" + indent).join(lines), semi), None


def main():
    root = git("rev-parse", "--show-toplevel").strip()
    os.chdir(root)
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    fix = "--fix" in args
    build = "build-tidy"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    only = [os.path.relpath(os.path.abspath(a)).replace(os.sep, "/").rstrip("/") for a in args if a != "--fix"]
    database = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(database):
        sys.exit(f"{database} not found: run tools/tidy.py once (or --build <folder>)")
    try:
        libclang()
    except ImportError:
        sys.exit("libclang's Python bindings not found: pip install clang==18.1.8")
    entries = [e for e in json.load(open(database, encoding="utf-8"))
               if FILES.match(os.path.relpath(e["file"], root).replace(os.sep, "/"))]

    groups = {}
    with concurrent.futures.ProcessPoolExecutor() as pool:
        for found in pool.map(groups_in, [(root, e) for e in entries], chunksize=4):
            for key, names in found.items():
                groups.setdefault(key, {}).update(names)

    skip = generated()

    def wanted(path):
        if any(fnmatch.fnmatch(path, g) for g in skip):
            return False
        return not only or any(path == o or path.startswith(o + "/") for o in only)

    by_file = {}
    for (path, start), names in groups.items():
        if wanted(path):
            by_file.setdefault(path, []).append((start, names))

    split = left = 0
    changed = []
    for path in sorted(by_file):
        # Latin-1: one character per byte, as libclang's offsets count;
        # UTF-8 text goes back out as it came in.
        source = open(path, "rb").read().decode("latin-1")
        edits = []
        for start, names in sorted(by_file[path]):
            line = source.count("\n", 0, start) + 1
            new, why = rewrite(source, start, names)
            if new is None:
                print(f"{path}:{line}: left: {why}")
                left += 1
                continue
            new, end = new
            if not fix:
                print(f"{path}:{line}: {' '.join(source[start:end].split())}")
            edits.append((start, end, new))
            split += 1
        if fix and edits:
            for start, end, new in sorted(edits, reverse=True):
                source = source[:start] + new + source[end:]
            open(path, "wb").write(source.encode("latin-1"))
            changed.append(path)
    print(f"{split} declarations {'split' if fix else 'to split'}, {left} left for a person")
    if changed:
        subprocess.run([sys.executable, "tools/format.py", *changed], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
