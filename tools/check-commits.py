#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Checks commits and a branch against docs/COMMIT_STANDARD.md.

    python3 tools/check-commits.py <base>..<head> [--branch <name>]

Checks every commit in the range (as `git log <base>..<head>` lists them)
and the branch's name, prints each problem, and exits with 1 if there is
any. CI runs it on every pull request; before pushing, run it with
origin/main..HEAD.

- Subjects: "<scope>: <what changed>", a known scope, no full stop, at
  most 100 characters. Merges and reverts keep git's own subjects.
- Bodies: lines of 72 characters at most, but for trailers, URLs, indented
  lines and lines without spaces.
- A Signed-off-by: trailer on every commit but merges.
- The branch: <kind>/<scope>-<topic>, or claude/... .
- No game files added or changed, in any commit, merges included.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SUBJECT_MAX = 100
BODY_MAX = 72
OTHER_SCOPES = {"docs", "tools", "tests", "ci", "build", "third_party", "testbed"}
BRANCH = re.compile(r"^(engine|fix|common|docs|tools|ci)/[a-z0-9]+(-[a-z0-9]+)*$")
BRANCH_FREE = ("claude/", "main", "dependabot/")

# Extensions of games' data and programs: never committed (CONTRIBUTING.md).
GAME_EXTENSIONS = {
    ".exe", ".dll", ".pk3", ".pak", ".bik", ".smk", ".avi", ".mov", ".wav", ".mp3", ".ogg", ".iso",
    ".cue", ".bsp", ".md3", ".mdr", ".qvm", ".aas", ".pac", ".fin", ".ddf", ".gr2", ".ac2", ".lvl",
    ".lv2", ".ovr", ".fnt", ".gs2", ".gsf", ".cmp", ".rle", ".cat", ".all", ".lst", ".res", ".dds",
    ".geo", ".gmf", ".arf", ".mdl", ".fif", ".lbp", ".stb", ".viv", ".fsh", ".frd",
}
SIZE_MAX = 1024 * 1024
# Files over SIZE_MAX that are the project's own, by path prefix.
SIZE_ALLOWED = ("tests/render/",)

problems = []


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def problem(where, message):
    problems.append(f"{where}: {message}")


def scopes():
    """Engine ids from engines.cfg, the folders under src/, and the others the standard names."""
    known = set(OTHER_SCOPES)
    with open(os.path.join(ROOT, "engines.cfg"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"([a-z0-9_]+)=", line)
            if m:
                known.add(m.group(1))
    src = os.path.join(ROOT, "src")
    known.update(d for d in os.listdir(src) if os.path.isdir(os.path.join(src, d)))
    return known


SUBJECT = re.compile(r"^(?P<scope>[a-z0-9_]+(?:/[A-Za-z0-9_.+-]+)*): (?P<what>\S.*)$")
TRAILER = re.compile(r"^[A-Za-z][A-Za-z-]*: ")


def check_message(sha, message, known):
    where = sha[:10]
    lines = message.rstrip("\n").split("\n")
    subject = lines[0]
    if subject.startswith(("Merge ", 'Revert "')):
        return
    m = SUBJECT.match(subject)
    if not m:
        problem(where, f'subject "{subject}" isn\'t "<scope>: <what changed>"')
    elif m.group("scope").split("/")[0] not in known:
        problem(where, f'scope "{m.group("scope")}" isn\'t an engine id, a folder under src/, or one of: '
                       + ", ".join(sorted(OTHER_SCOPES)))
    if subject.endswith("."):
        problem(where, "the subject ends with a full stop")
    if len(subject) > SUBJECT_MAX:
        problem(where, f"the subject is {len(subject)} characters (at most {SUBJECT_MAX})")
    if len(lines) > 1 and lines[1] != "":
        problem(where, "no blank line after the subject")
    fenced = False
    for number, line in enumerate(lines[1:], 2):
        if line.startswith("```"):
            fenced = not fenced
        if fenced or len(line) <= BODY_MAX:
            continue
        if TRAILER.match(line) or "://" in line or line.startswith(("    ", "\t", "|")) or " " not in line.strip():
            continue
        problem(where, f"line {number} is {len(line)} characters (at most {BODY_MAX})")
    if not any(l.startswith("Signed-off-by: ") for l in lines):
        problem(where, "no Signed-off-by: trailer (git commit -s)")


def check_files(sha, parents):
    where = sha[:10]
    base = parents[0] if parents else "4b825dc642cb6eb9a060e54bf8d69288fbee4904"  # the empty tree
    changed = git("diff", "--name-only", "--diff-filter=AMR", base, sha).split("\n")
    for path in filter(None, changed):
        ext = os.path.splitext(path)[1].lower()
        if ext in GAME_EXTENSIONS:
            problem(where, f"{path}: a game's file type ({ext}); games' files are never committed")
        size = int(git("cat-file", "-s", f"{sha}:{path}").strip())
        if size > SIZE_MAX and not path.startswith(SIZE_ALLOWED):
            problem(where, f"{path}: {size} bytes, over {SIZE_MAX} (a game's file? if not, list it in SIZE_ALLOWED)")


def check_branch(name):
    if not name or name == "HEAD" or name.startswith(BRANCH_FREE):
        return
    if not BRANCH.match(name):
        problem(f"branch {name}", "isn't <kind>/<scope>-<topic> (kinds: engine, fix, common, docs, tools, ci)")


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 2
    rng = argv[0]
    branch = None
    if "--branch" in argv:
        branch = argv[argv.index("--branch") + 1]
    else:
        branch = os.environ.get("GITHUB_HEAD_REF") or git("rev-parse", "--abbrev-ref", "HEAD").strip()
    known = scopes()
    try:
        log = git("log", "--format=%H %P", rng).strip()
    except subprocess.CalledProcessError as e:
        print(f"git can't list {rng}: {e.stderr.strip()} (in CI, check out with fetch-depth: 0)")
        return 2
    commits = [l.split() for l in log.split("\n") if l]
    for sha, *parents in commits:
        message = git("log", "-1", "--format=%B", sha)
        if len(parents) < 2:
            check_message(sha, message, known)
        check_files(sha, parents)
    check_branch(branch)
    for p in problems:
        print(p)
    print(f"{len(commits)} commit{'' if len(commits) == 1 else 's'} checked, "
          f"{len(problems)} problem{'' if len(problems) == 1 else 's'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
