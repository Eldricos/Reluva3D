# Mysteries of the Sith: testing

## Automated checks

### Unit tests

The engine's, shared with Jedi Knight ([../jk/TESTING.md](../jk/TESTING.md)),
check this game too:

| Suite | What it checks |
|---|---|
| `sith.unit.detection` | Its install recognised by its program and archives |
| `sith.unit.folders` | Its CD and Jedi Knight's CD 2 told apart by their markers, as its CDs 1 and 2; found for its install |
| `sith.unit.import` | Imported from its CD's image (a cue sheet, a raw data track and an audio track built in the test), Jedi Knight's CD 2 added with *Add other disc...*; what's made recognised, its CDs and music found |
| `common.unit.files` | Disc images read: ISO 9660 on a raw track through a cue sheet and on an `.iso` |

### The basic engine test

The engine's, `sith.engine`, on Jedi Knight's files.

### The game-files test

Not yet.

### What the automated checks don't cover

Running the game: the CD checks, its cut scenes, levels and music, by
hand ([PLAN.md](PLAN.md), phases 3 and 4).

## A playthrough by hand

Not yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux, the emulator by hand (`reluva-tool sith host`, changed for `JKM.EXE`) | The CD checks, the opening, the menus and the first level's start ([README.md](README.md#where-it-stands)) |
| November 2026 | US retail, from its CD's image | Linux, `reluva-tool import` with Jedi Knight's CD 2, then `reluva-tool sith host` on the import, its save folder new | Files the same as the install's; 13 music tracks found; on its first start both CD checks passed without a request, the opening, a new game's cut scene, and play in the first level with its music asked for |
