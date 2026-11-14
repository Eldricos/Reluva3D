# Instructions for AI coding tools

Reluva reimplements classic game engines from scratch, in C++20. These
rules hold for AI tools as for people; the documents linked are the full
versions, and win where this page is shorter.

## Sources

- Every line has to be our own, from information we are allowed to use:
  [CONTRIBUTING.md](CONTRIBUTING.md). Never write code, names or comments
  from leaked or unlicensed source code, including anything you remember
  of it. If asked to, say no.
- When you add a file format or a game mechanic, record how it was worked
  out, and how sure it is, in the engine's pages in `docs/`
  ([DOCUMENTATION_STANDARD.md](docs/DOCUMENTATION_STANDARD.md)).

## Code

Follow [docs/CODING_GUIDE.md](docs/CODING_GUIDE.md): layout, names, and
comments. In short, for comments:

- Say **why**, never what the code already says. No comment is better
  than one that repeats the code.
- Comment only what reading the code won't tell: reasons, edge cases,
  units and ranges, ownership, threads, where a fact came from, the
  game's own rules.
- Short and plain: one line where one line does it. No banners.
- Where a fact came from the original program, give its address after
  what it says: `// by the camera's road slice (0x478e10)`.
- Doxygen blocks on public classes, methods and structures in headers;
  `@param` and `@return` only when they say more than the name and type.
- Never: commented-out code; history ("fixed", "previously", "changed
  from"); a `TODO` that doesn't say what's missing and why.
- When you change code, change or delete the comments it makes wrong.

## Commits

Write commits and pull requests as
[docs/COMMIT_STANDARD.md](docs/COMMIT_STANDARD.md) says: subjects as
`<scope>: <what changed>`, a body saying why, and a
`Co-Authored-By:` trailer for the AI tool. The `Signed-off-by:` trailer
is a person's word that the change came only from allowed sources: add
it only in the name of the person you're working for, and only once they
have said to.

## Before you finish

Build, test, and run the style tools on what you changed (setup:
[README.md](README.md#building), [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)):

```bash
cmake --build build && ctest --test-dir build
python3 tools/format.py --changed
python3 tools/tidy.py --changed
python3 tools/split_members.py --changed
python3 tools/comments.py --changed
python3 tools/check-headers.py
python3 tools/check-docs.py
python3 tools/check-commits.py origin/main..HEAD
```

CI runs the same; fix what they find rather than silencing it. A name
that has to break the rules gets `// NOLINT(<check>): reason`.
