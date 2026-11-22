# SimCoaster: testing

## Automated checks

### Unit tests

`tests/engines/tpw/unit/`, a file per topic, each a suite
`tpw.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `tpw.unit.import` | Importing from a small CD laid out as the US one: the cabinet's `Data` group in `Data/`, the movie and `Game.exe` at the top, the language's folders over the cabinet's files, the DLLs left out, then found by detection; a cabinet without `Game.exe` isn't the game's; a folder missing is said |
| `tpw.unit.detection` | Recognising the game: a release not in the table found by its files' names, in any case, as an unknown release; not without `Data/Challenges.sam` |

### The basic engine test

`tpw.engine` (`tests/engines/tpw/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/tpw/data/engine-record.txt`).
So far the install is stand-in files found as an unknown release of
SimCoaster.

### The game-files test

Not yet: it comes with the first readers of the game's files.

### What the automated checks don't cover

The US release's detection by its files' hashes, and importing the real
CD: the tests can't hold the game's files. Checked by hand.

## A playthrough by hand

1. Import the CD in the launcher (*Import Game...*): it's listed as
   "SimCoaster (US)", and the folder made holds the game's files as setup
   installs them ([cd.md](formats/cd.md)). Adding an installed folder
   (*Add Game...*) finds it the same.
2. Start it: an empty screen, which Esc leaves.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US | Linux | `reluva-tool detect` on the install: "SimCoaster (US)"; on the CD: nothing found, as expected |
| November 2026 | US | Linux | `reluva-tool import` from the CD: 1,276 files, byte for byte the install's; found as "SimCoaster (US)" |
