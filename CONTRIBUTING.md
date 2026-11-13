# Contributing

Reluva reimplements classic game engines from scratch. Every line of code
has to be our own, written from information we are allowed to use.

## Allowed sources

- The game's own data files, from a copy you legally own.
- Observing the original game running (screenshots, recordings, notes).
- Reverse engineering of a legally owned executable, where your local law
  permits it for interoperability.
- Public documentation, file format write-ups, and fan wikis.

## Not allowed

- **Leaked, stolen, or otherwise unlicensed source code**, or anything
  derived from it, including notes, specs, or comments written while
  reading it. If you have read leaked source for a game, don't contribute
  to that game's engine.
- Copying code from projects whose license is incompatible with ours.
- Committing any game assets (graphics, audio, text, executables).

## Document where knowledge comes from

When you add support for a file format or a game mechanic, record how it was
worked out (e.g. "header layout found by comparing 40 .xyz files" or
"timings measured from a gameplay recording") in `docs/`, on the page for
that format or mechanic, with how sure it is: see
[Sources and confidence](docs/DOCUMENTATION_STANDARD.md#sources-and-confidence).
For a small change the commit message may give more detail, but the docs
must say the same: nobody should need the commit history to find where a
fact came from. This record shows the work was done independently.

## Code

Write code as [docs/CODING_GUIDE.md](docs/CODING_GUIDE.md) says: its
layout, names, and Doxygen comments on public classes, methods and
structures (turn a file's plain comments into them when you work on it).
The tools that check it, and how to run them before a pull request, are in
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#code-style): `tools/format.py`,
`tools/tidy.py` and `tools/split_members.py`, each with `--changed`. CI
runs the same checks on what a pull request changes.

## Commits and pull requests

Write commits, branches and pull requests as
[docs/COMMIT_STANDARD.md](docs/COMMIT_STANDARD.md) says: subjects as
`<scope>: <what changed>`, a body that says why and where the facts came
from, and a `Signed-off-by:` on every commit (`git commit -s`), which says
the change was written from the sources this page allows. A pull request
fills in its template. CI checks the commits with
`tools/check-commits.py`; run it before pushing with
`python3 tools/check-commits.py origin/main..HEAD`.
