#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Checks the docs against docs/DOCUMENTATION_STANDARD.md.

Run from anywhere: python3 tools/check-docs.py. It prints each problem
and exits with 1 if there is any. CI runs it on every push.

- Links: every relative link in a Markdown file goes to a file or folder
  that exists, and every #anchor to a heading that's there.
- Engines: each engine's folder (or, for an engine with games/, each
  game's folder) has the five standard pages, and the engine its README.
- Format and system pages: a status line with a Confidence, and a
  "Sources and checks" section.
- Common pages: their five headings, in order, and a Confidence.
- research/ folders: a README.md.
- Names: standard pages and guides in capitals, every other page and
  folder in lower case with hyphens.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Markdown that isn't the project's own.
SKIP_DIRS = {".git", "build", "out", "third_party", "node_modules"}
# Build folders, as .gitignore has them (tools/tidy.py makes build-tidy/,
# with SDL's and Dear ImGui's docs in it).
SKIP_PREFIXES = ("build-", "cmake-build-")

# Templates hold placeholders (<engine>) and links to pages that only exist
# once a template is copied into place: their links aren't checked.
TEMPLATES = "docs/templates"

# Engines not brought in line with the standard yet, and why. Remove an
# engine from here once its docs follow it.
EXEMPT_ENGINES = {
    "newworlds": "work in progress outside the main branch, to be brought in line when it's merged",
}

STANDARD_PAGES = ["README.md", "KNOWN_DIFFERENCES.md", "UNCONFIRMED.md", "TESTING.md", "REVERSE_ENGINEERING.md"]
CAPITAL_NAMES = set(STANDARD_PAGES) | {
    "PLAN.md", "PORTING.md", "CONTRIBUTING.md", "DEVELOPMENT.md",
    "ADDING_AN_ENGINE.md", "CODING_GUIDE.md", "COMMIT_STANDARD.md", "DOCUMENTATION_STANDARD.md",
}
LOWER_NAME = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*\.(md|txt)$")
LOWER_DIR = re.compile(r"^[a-z0-9]+([-_][a-z0-9]+)*$")
COMMON_HEADINGS = ["## What it is", "## What Reluva supports", "## Sources and checks", "## Used by"]
# The ## headings each standard page has, in order (docs/templates/engine/).
STANDARD_HEADINGS = {
    "KNOWN_DIFFERENCES.md": ["By choice", "Bugs fixed", "Not yet as the original", "Additions outside the game"],
    "UNCONFIRMED.md": ["Read from the code, not seen running", "Seen running, not traced in the code",
                       "Inferred", "Guessed", "Settled"],
    "TESTING.md": ["Automated checks", "A playthrough by hand", "Last checked"],
    "REVERSE_ENGINEERING.md": ["Ground rules", "Sources", "Methods", "The tools", "Working files not included"],
    "PLAN.md": ["Goal", "Phases", "Decisions"],
}
# The words an italic tag may use: *(inferred)*.
TAG_WORDS = {"confirmed", "from the code", "seen, not traced", "inferred", "guessed", "published"}
STATUS_WORDS = {"formats": {"decoded", "partial", "unknown"}, "systems": {"done", "partial", "not started"}}
# How sure a fact is (docs/DOCUMENTATION_STANDARD.md, "Sources and confidence"),
# and what a page about Reluva's own design says instead.
CONFIDENCE_WORDS = ["confirmed", "from the code", "seen, not traced", "inferred", "guessed", "published", "Reluva's own"]

problems = []


def problem(path, message):
    problems.append(f"{os.path.relpath(path, ROOT)}: {message}")


def markdown_files():
    for top, dirs, files in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith(SKIP_PREFIXES))
        for name in sorted(files):
            if name.endswith(".md"):
                yield os.path.join(top, name)


def slug(heading):
    """The anchor GitHub gives a heading."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # a link: its text
    text = re.sub(r"<[^>]+>", "", text)  # HTML tags
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


anchor_cache = {}


def anchors(path):
    if path not in anchor_cache:
        found, seen, fenced = set(), {}, False
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("```"):
                    fenced = not fenced
                m = None if fenced else re.match(r"#{1,6} (.*)", line)
                if m:
                    base = slug(m.group(1))
                    n = seen.get(base, 0)
                    seen[base] = n + 1
                    found.add(base if n == 0 else f"{base}-{n}")
        anchor_cache[path] = found
    return anchor_cache[path]


LINK = re.compile(r"\]\(([^)\s]+)\)")


def check_tags(path):
    """Italic tags (*(inferred)*) use the standard's words."""
    rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
    if not rel.startswith(("docs/engines/", "docs/common/")) or "/research/" in rel:
        return
    if any(rel.startswith(f"docs/engines/{e}/") for e in EXEMPT_ENGINES):
        return
    for number, line in enumerate(text(path).split("\n"), 1):
        for word in re.findall(r"\*\(([^)*]+)\)\*", line):
            if word not in TAG_WORDS:
                problem(path, f"line {number}: the tag *({word})* isn't one of the standard's words")


def check_links(path):
    if os.path.relpath(path, ROOT).replace(os.sep, "/").startswith(TEMPLATES + "/"):
        return
    fenced = False
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if line.startswith("```"):
                fenced = not fenced
            if fenced:
                continue
            for url in LINK.findall(re.sub(r"`[^`]*`", "", line)):
                if re.match(r"^[a-z][a-z0-9+.-]*:", url):  # http:, mailto: ...
                    continue
                target, _, anchor = url.partition("#")
                full = os.path.normpath(os.path.join(os.path.dirname(path), target)) if target else path
                if not os.path.exists(full):
                    problem(path, f"line {number}: link to {url}: no such file")
                elif anchor and full.endswith(".md") and anchor not in anchors(full):
                    problem(path, f"line {number}: link to {url}: no such heading")


def text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def first_line(path):
    with open(path, encoding="utf-8") as f:
        return f.readline().rstrip("\n")


def check_titles(folder, name):
    """Every page but the folder's README is titled "# <name>: <topic>"."""
    pages = [os.path.join(folder, n) for n in os.listdir(folder) if n.endswith(".md") and n != "README.md"]
    for kind in ("formats", "systems"):
        sub = os.path.join(folder, kind)
        if os.path.isdir(sub):
            pages += [os.path.join(sub, n) for n in os.listdir(sub) if n.endswith(".md")]
    if os.path.isfile(os.path.join(folder, "research", "README.md")):
        pages.append(os.path.join(folder, "research", "README.md"))
    for page in sorted(pages):
        if not first_line(page).startswith(f"# {name}: "):
            problem(page, f'its title is "# {name}: <topic>"')


def check_page_folder(folder, label):
    """A folder that holds an engine's or a game's own pages."""
    for name in STANDARD_PAGES:
        if not os.path.isfile(os.path.join(folder, name)):
            problem(folder, f"{label} has no {name}")
    for name, headings in STANDARD_HEADINGS.items():
        page = os.path.join(folder, name)
        if os.path.isfile(page):
            found = [l[3:].strip() for l in text(page).split("\n") if l.startswith("## ")]
            if found != headings:
                problem(page, f"its ## headings are the template's: {', '.join(headings)}")
    known = os.path.join(folder, "KNOWN_DIFFERENCES.md")
    m = re.match(r"# (.+): known differences from the original$", first_line(known)) if os.path.isfile(known) else None
    if m:
        check_titles(folder, m.group(1))
    else:
        problem(known, 'its title is "# <game>: known differences from the original"')
    for kind in ("formats", "systems"):
        if os.path.isdir(os.path.join(folder, kind)):
            check_pages(os.path.join(folder, kind))
    research = os.path.join(folder, "research")
    if os.path.isdir(research) and not os.path.isfile(os.path.join(research, "README.md")):
        problem(research, "research/ has no README.md")


def check_engines():
    engines = os.path.join(ROOT, "docs", "engines")
    for engine in sorted(os.listdir(engines)):
        folder = os.path.join(engines, engine)
        if not os.path.isdir(folder) or engine in EXEMPT_ENGINES:
            continue
        games = os.path.join(folder, "games")
        if os.path.isdir(games):
            if not os.path.isfile(os.path.join(folder, "README.md")):
                problem(folder, "the engine has no README.md")
            for game in sorted(os.listdir(games)):
                if os.path.isdir(os.path.join(games, game)):
                    check_page_folder(os.path.join(games, game), f"game {game}")
            for kind in ("formats", "systems"):
                if os.path.isdir(os.path.join(folder, kind)):
                    check_pages(os.path.join(folder, kind))
            m = re.match(r"# The (.+?) engine\b", first_line(os.path.join(folder, "README.md")))
            if m:
                for kind in ("formats", "systems"):
                    sub = os.path.join(folder, kind)
                    if os.path.isdir(sub):
                        for n in sorted(os.listdir(sub)):
                            if n.endswith(".md") and not first_line(os.path.join(sub, n)).startswith(f"# {m.group(1)}: "):
                                problem(os.path.join(sub, n), f'its title is "# {m.group(1)}: <topic>"')
            else:
                problem(folder, 'its README\'s title is "# The <name> engine"')
        else:
            check_page_folder(folder, f"engine {engine}")


def fields(status_line):
    """A status line's fields: {"Status": "decoded", "Code": ..., ...}."""
    found = {}
    for part in status_line.lstrip("> ").split(" · "):
        m = re.match(r"\*\*([^*]+):\*\* (.*)", part.strip())
        if m:
            found[m.group(1)] = m.group(2).strip()
    return found


def check_confidence(path, value):
    """Each part of a Confidence ("layout confirmed; behaviour from the code") uses the standard's words."""
    value = re.sub(r"\s+unless marked\.?$", "", value.strip()).rstrip(".")
    for part in value.split(";"):
        if not any(word in part for word in CONFIDENCE_WORDS):
            words = ", ".join(CONFIDENCE_WORDS)
            problem(path, f"Confidence \"{part.strip()}\" uses none of the standard's words ({words})")


def check_pages(sub):
    """The pages of a formats/ or systems/ folder."""
    for name in sorted(os.listdir(sub)):
        if not name.endswith(".md") or name == "README.md":
            continue
        page = os.path.join(sub, name)
        body = text(page)
        status = next((l for l in body.split("\n") if l.startswith("> **")), None)
        if status is None:
            problem(page, "no status line (> **Status:** ... · **Confidence:** ...)")
        elif "**Confidence:**" not in status:
            problem(page, "the status line has no Confidence")
        else:
            f = fields(status)
            check_confidence(page, f["Confidence"])
            words = STATUS_WORDS[os.path.basename(sub)]
            if "Status" in f and f["Status"] not in words:
                allowed = ", ".join(sorted(words))
                problem(page, f"Status \"{f['Status']}\" isn't one of: {allowed}")
        if not re.search(r"^## Sources and checks$", body, re.M):
            problem(page, 'no "## Sources and checks" section')


def check_common():
    common = os.path.join(ROOT, "docs", "common")
    for top, _, files in os.walk(common):
        for name in sorted(files):
            if not name.endswith(".md"):
                continue
            page = os.path.join(top, name)
            lines = text(page).split("\n")
            places = [lines.index(h) if h in lines else -1 for h in COMMON_HEADINGS]
            for heading, place in zip(COMMON_HEADINGS, places):
                if place < 0:
                    problem(page, f'no "{heading}"')
            found = [p for p in places if p >= 0]
            if found != sorted(found):
                problem(page, "its headings aren't in the standard's order")
            confidence = [l for l in lines if "**Confidence:**" in l]
            if not confidence:
                problem(page, "no Confidence in its Sources and checks")
            for line in confidence:
                check_confidence(page, line.split("**Confidence:**", 1)[1])


def check_names():
    docs = os.path.join(ROOT, "docs")
    for top, dirs, files in os.walk(docs):
        rel = os.path.relpath(top, docs).replace(os.sep, "/")
        if any(rel == f"engines/{e}" or rel.startswith(f"engines/{e}/") for e in EXEMPT_ENGINES):
            continue
        for d in dirs:
            if not LOWER_DIR.match(d):
                problem(os.path.join(top, d), "a folder's name is in lower case")
        for name in files:
            if name in CAPITAL_NAMES:
                continue
            if not LOWER_NAME.match(name):
                problem(os.path.join(top, name), "a page's name is in lower case with hyphens (or one of the standard pages' capitals)")


def main():
    for path in markdown_files():
        check_links(path)
        check_tags(path)
    check_engines()
    check_common()
    check_names()
    for p in problems:
        print(p)
    print(f"{len(problems)} problem{'' if len(problems) == 1 else 's'}")
    if EXEMPT_ENGINES:
        print("Not checked: " + "; ".join(f"{e} ({why})" for e, why in EXEMPT_ENGINES.items()))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
