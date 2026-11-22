# Jedi Knight: testing

## Automated checks

### Unit tests

`tests/engines/sith/unit/`, a file per topic, each a suite
`sith.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sith.unit.detection` | Recognising the game's releases: the fallback table, names in any case, a partial install or a CD not taken for one |
| `sith.unit.folders` | Telling the CDs apart by `JK_.CD`, and finding them in and beside an install and by the settings |

### The basic engine test

`sith.engine` (`tests/engines/sith/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/sith/data/engine-record.txt`).
So far: an empty folder isn't the game, the install is recognised (as an
unknown release, its files being empty), and a CD is found beside it and
another in it.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

That the US release's signatures match a real install: checked by hand
([Last checked](#last-checked)).

## A playthrough by hand

- *Add Game...* on the install finds "Jedi Knight: Dark Forces II (US,
  1.0)".
- `reluva-tool sith folders <install>` finds both CDs where they are.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux | `reluva-tool sith folders` on the install, with copies of both CDs beside it: recognised as the US release, both CDs found |
