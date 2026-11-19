# The documentation standard

How Reluva's docs are laid out, named and written. Every page follows it,
and [templates/](templates/README.md) has a starting point for each kind of
page. [README.md](README.md) is the map of what's there.

## Where docs go

| Where | What |
|---|---|
| The repository's root | `README.md` (what Reluva is, building, using it, the games table) and `CONTRIBUTING.md` (what may be used) |
| `docs/` | The guides for working on Reluva: [DEVELOPMENT.md](DEVELOPMENT.md), [ADDING_AN_ENGINE.md](ADDING_AN_ENGINE.md), [CODING_GUIDE.md](CODING_GUIDE.md), [COMMIT_STANDARD.md](COMMIT_STANDARD.md), and this page |
| `docs/common/` | Code in `src/common/` that more than one engine can use (below) |
| `docs/engines/<engine>/` | One engine and its games, and only what is about them (below) |
| `docs/templates/` | The templates every page starts from |

## An engine's folder

Every engine's folder has the same layout:

| Page or folder | What goes in it |
|---|---|
| `README.md` | What it runs (games and releases), where it stands, how to use it, a table of this folder, and "Terms": the engine's or game's own words its pages use without explaining, a line each |
| `KNOWN_DIFFERENCES.md` | Where Reluva knowingly differs from the original: by choice, bugs fixed, not yet as the original, additions outside the game |
| `UNCONFIRMED.md` | What hasn't been checked against the running original: read from the code but not seen, seen but not traced, inferred, guessed, and what's been settled |
| `TESTING.md` | The automated checks, what to check by hand in a playthrough, and when it was last checked |
| `REVERSE_ENGINEERING.md` | How the facts were found: the sources, the methods, the tools. An engine ported from another project may also have a `PORTING.md` |
| `formats/` | A page per file format: a file's layout, and how the game reads it |
| `systems/` | A page per part of how the game behaves at run time: scripts, dialogue, the interface, saves, AI, network play |
| `research/` | Working notes that the pages above are built from: address maps, notes read straight off the executable, notes from recordings. They keep the form they were written in, not a template; a `README.md` lists them and explains any marks they use, and each note says it's a working note at its top. Plain-text data files (an address list a tool reads) stay `.txt` |
| `PLAN.md` | Optional, while it's being built: the plan, its phases and the decisions taken. Facts go on their pages as they're found |

**Every page starts from a template** in [templates/](templates/README.md),
with its headings in the same order.

- **The five pages** are made with the engine (`cmake/NewEngine.cmake`,
  from `templates/engine/`) and kept even while short.
- **The three folders** are made when they get their first page.
- **A format or a system** gets its own page once there's something to
  say about it; a page that grows too big is split by topic, not kept as
  one long notes file.

## Engines for a family of games, and for one

- **An engine for a family of games** (one known from the start to be
  meant for more than one game, as `idtech3`, `awe` and `netimmerse` are)
  keeps each game's docs in `games/<game id>/`, laid out as above: the
  five pages and the three folders. The engine's own folder then holds its
  `README.md` (the engine, and a table of its games) and only what holds
  for every game on it, such as the engine's or a middleware's own file
  formats in `formats/`. A page worked out from one game stays in that
  game's folder until another game is found to share it, then moves up.
- **An engine for one game** (`clue`, `stgenerations`, `stharbinger`)
  keeps everything in its own folder, with no `games/`. If a second game
  comes to it, it moves to the layout above.

## Shared code: `common/`

**Shared code** that more than one engine can use (`src/common/`,
`src/audio/`, `src/video/`, `src/graphics/`, `src/image/`,
`src/emulation/`) is documented in `common/`, a folder per subject
(`common/video/` for `src/video/`, `common/x86/` for
`src/emulation/x86/`), a page per component. A common page holds only
what's true of the component for any engine, under these headings:

1. `## What it is`: what it is, and where its code is.
2. `## What Reluva supports`: what it handles, and what it doesn't.
3. `## The format` (a file format) or `## How it works` (a component),
   when there's a layout or a design to describe.
4. `## Sources and checks`: where the facts came from, and how the code
   was checked. A game's files or program used as a check is named here.
5. `## Used by`: each engine that uses it, with a link to the page in
   that engine's docs that says how its games use it.

How one game uses a component (which of its files, its quirks) goes in
that engine's docs, not here.

## Naming

- **Standard pages are in capitals, with underscores:** `README.md`,
  `KNOWN_DIFFERENCES.md`, `UNCONFIRMED.md`, `TESTING.md`,
  `REVERSE_ENGINEERING.md`, `PLAN.md`, `PORTING.md`; so are the guides in
  `docs/` (`DEVELOPMENT.md`, `ADDING_AN_ENGINE.md`, `CODING_GUIDE.md`,
  `COMMIT_STANDARD.md`, `DOCUMENTATION_STANDARD.md`) and the root's `README.md` and
  `CONTRIBUTING.md`.
- **Every other page is in lower case, with hyphens:** format, system,
  common and research pages, and the templates. A format page is named
  after the file it describes (`pac.md`, `gr2.md`), a system page after the
  part of the game (`dialogue.md`, `script-reference.md`).
- **Folders are in lower case:** `formats/`, `systems/`, `research/`,
  `games/<game id>/` (the game id as the engine's detection table has it).
- **Data files** in `research/` (a list a tool reads) are in lower case
  too, and keep their own extension.
- **Titles:** an engine's README is "The <name> engine", a game's README
  is the game's name, and any other page is `# <Game or engine>: <topic>`.
  The title isn't repeated as the first `##`, and a page's status and code
  go in its status line, not in a heading.

## Sources and confidence

Each fact gets its source: an address in a named binary, a file and its
hash, or the published source it was read in. See
[CONTRIBUTING.md](../CONTRIBUTING.md) for what may be used.

The source is in the docs, on the fact's page. A commit message may give
more detail for a small change, but the docs say the same: nobody should
need the commit history to find where a fact came from.

**Every fact has its source and a confidence.** A page's sources are in
its "Sources and checks". Its confidence uses these words (the middle four
are also `UNCONFIRMED.md`'s headings):

| Word | Means |
|---|---|
| confirmed | Checked against the original: seen running (or in a recording), against its own code's output, or a reader checked on every one of its files |
| from the code | Read from the original's executable, not seen running |
| seen, not traced | Seen running, not found in the code |
| inferred | Follows from other facts |
| guessed | A working assumption |
| published | From published documentation or source code |

- A format or system page states its default once, in its status line
  (`**Confidence:** from the code unless marked`; a page whose layout and
  behaviour differ can say both: `layout confirmed; behaviour from the
  code`).
- Only what differs from the default is marked, with a short italic tag
  at the end of its sentence, bullet or table row: `*(inferred)*`. A
  table whose rows differ gets a Confidence column instead. Prose that
  already says how a fact was found ("as a recording shows") needs no
  tag.
- Headings carry neither tags nor addresses: an address goes on the
  section's first line (``In `motoe.exe`: `0x438f00`.``).
- Marked items that matter to how Reluva behaves are also listed in
  `UNCONFIRMED.md`, under the heading of the same name.
- A common page's default is in its "Sources and checks"; it marks
  exceptions the same way. `research/` notes keep their own marks, which
  their folder's `README.md` explains.

## Checks

CI's "Docs" job checks the docs on every push, and both checks can be run
before pushing, from the repository's root:

```sh
python3 tools/check-docs.py
```

checks that every relative link and `#anchor` goes somewhere, that each
engine (or each game, in a family's engine) has its five pages, that each
format and system page has a status line with a Confidence and a "Sources
and checks", that Status, Confidence and italic tags use the standard's
words, that the standard pages have their templates' headings, that every
page's title starts with its engine's or game's name, that each common
page has its headings in order, that each `research/` has a `README.md`,
and that names follow "Naming" above.

```sh
npx markdownlint-cli2@0.23.3
```

checks the headings' shape (one `#` title first, one level at a time, a
blank line around each) and a few basics (no spaces at a line's end, no
runs of blank lines). Its rules are in `.markdownlint-cli2.jsonc`.

`research/` notes are left out of markdownlint, as they're kept as
written; their links are still checked. An engine whose docs don't follow
this standard yet is listed in `tools/check-docs.py` (`EXEMPT_ENGINES`)
with the reason: its pages, names and Markdown aren't checked until it's
taken off that list, but its links are.
