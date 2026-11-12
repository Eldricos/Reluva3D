# The @TITLE@ engine

<!-- One or two sentences: what the engine runs, and whether it's meant for
one game or a family of games (then see docs/DOCUMENTATION_STANDARD.md,
for its games/ folders). -->
Engine id `@ENGINE@`, in `src/engines/@ENGINE@/`.

## Games

<!-- A row per game. Releases are recognised by the files in `kVariants`
(src/engines/@ENGINE@/@ENGINE@.cpp), made with `reluva --hash <files...>`. -->

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| | | | | | Not yet recognised |

## Where it stands

<!-- What works now, in a few lines, kept up to date. Plans, phases and
dated progress go in PLAN.md (docs/templates/plan.md), not here. -->
Started from the template: it recognises nothing yet, and shows an empty
screen.

## Using it

<!-- What a player needs: which folder to add (or import from a CD), any
settings, the keys. -->

## This folder

| Page | What's in it |
|---|---|
| This page | The games, where the engine stands, how to use it |
| `formats/` | A page per file format, as it's worked out |
| `systems/` | A page per part of how the game behaves (scripts, dialogue, saves, AI...) |
| `research/` | Working notes: address maps, raw findings, notes on recordings |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

<!-- Link each folder once it has pages, and list its pages. The layout is
described in docs/DOCUMENTATION_STANDARD.md; page templates are in
docs/templates/. -->

## Code

<!-- Where things are in src/engines/@ENGINE@/. Keep files by topic in
subfolders as it grows (the game's formats, its pages, its game logic). -->

| Path | What |
|---|---|
| `@ENGINE@.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `tools/` | `reluva-tool @ENGINE@` commands (`reluva-tool help @ENGINE@` lists them) |

## Tests

- `reluva-tests @ENGINE@` runs its unit tests and its engine test
  (`tests/engines/@ENGINE@/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).
