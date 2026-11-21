# Star Trek: Away Team: testing

## Automated checks

### Unit tests

`tests/engines/stawayteam/unit/`, a file per topic, each a suite
`stawayteam.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `stawayteam.unit.detection` | Recognising the game: an empty folder and the program alone aren't the game; its two files, whatever they hold, are an unknown release. Importing it: from a CD's `Setup` folder, the files Reluva uses and the movies in their folders, and not the rest; from an install without movies, with a note |

Reading `DATA.dat` is the common ZIP reader's, tested in
`common.unit.archives`, with an end record signed as the game signs it.

### The basic engine test

`stawayteam.engine` (`tests/engines/stawayteam/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

That the real release is recognised: its signatures need the real
files, which the tests never hold. That the real `DATA.dat` is read
whole: `reluva-tool stawayteam data-extract` on it.

## A playthrough by hand

1. Add the install's folder in the launcher: it's found as "Star Trek:
   Away Team".
2. Start it: an empty screen, until Esc. With `DATA.dat` missing or
   damaged, it says so instead.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US English retail | Linux | Detection of the install (`reluva-tool detect`); the import's plan (`reluva-tool import`) from the install, and from a CD's layout made of its files; all 6,949 files of `DATA.dat` read, their CRC-32s matching |
