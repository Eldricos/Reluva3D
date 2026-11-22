# SimCoaster: testing

## Automated checks

### Unit tests

`tests/engines/tpw/unit/`, a file per topic, each a suite
`tpw.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `tpw.unit.import` | Importing from a small CD laid out as the US one: the cabinet's `Data` group in `Data/`, the movie and `Game.exe` at the top, the language's folders over the cabinet's files, the DLLs left out, then found by detection; a cabinet without `Game.exe` isn't the game's; a folder missing is said |
| `tpw.unit.formats` | The game's files: archives with folders, stored and RefPack files, damage; the `Data` folder mounted, each archive as its folder, loose files over them; settings files, every kind of line, broken ones turned away |
| `tpw.unit.detection` | Recognising the game: a release not in the table found by its files' names, in any case, as an unknown release; not without `Data/Challenges.sam` |

### The basic engine test

`tpw.engine` (`tests/engines/tpw/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/tpw/data/engine-record.txt`).
So far: stand-in files found as an unknown release of SimCoaster, and
its `Data` folder (an archive and a loose file) mounted and every file
read.

### The game-files test

With your own copy, installed or imported from the CD:
`RELUVA_SIMCOASTER=<the game's folder> ctest --test-dir build -L gamedata`
(`tests/engines/tpw/gamedata.cmake`), against the records in
`tests/engines/tpw/data/gamedata/us/`:

| Step | What it runs |
|---|---|
| `release` | What the game is (`reluva-tool detect`) |
| `data` | Every file of the `Data` folder read, every archive's unpacked (`reluva-tool tpw data-check`) |
| `settings` | Every settings file read (`reluva-tool tpw sam-check`) |

The installed folder and one imported from the CD pass against the same
records. A baseline of your own is made as
[docs/DEVELOPMENT.md](../../../../DEVELOPMENT.md#the-game-files-test-with-your-own-copy-of-the-game)
says.

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
| November 2026 | US | Linux | The game-files test on the install and on the CD imported: 303 archives, 16,225 files read |
