# Star Trek: Armada: testing

## Automated checks

### Unit tests

`tests/engines/storm3d/unit/`, a file per topic, each a suite
`storm3d.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `storm3d.unit.detection` | An empty folder, and the executable alone, aren't the game; Armada's three files by name are found as an unknown release |

### The basic engine test

`storm3d.engine` (`tests/engines/storm3d/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`). So far its install
is Armada's three files by name, found as an unknown release.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

Recognising the English release by its hashes: that needs the game's own
files, so it's checked by hand ([Last checked](#last-checked)).

## A playthrough by hand

1. Add the install in the launcher: it's found as "Star Trek: Armada".
2. Start it: an empty screen; Esc goes back to the launcher.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail 1.0 | Linux | `reluva-tool detect` on the install finds it as the English release |
