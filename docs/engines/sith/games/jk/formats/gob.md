# Jedi Knight: archives (`.gob`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/gob.cpp` · **Checked on:** US retail, all six archives (5,615 files) · **Confidence:** confirmed unless marked

## Overview

Nearly all of the game's data is in six archives:

| Archive | Where | Files | What |
|---|---|---|---|
| `Res1hi.gob` | install `Resource/`; CD 1 `GAMEDATA/MININSTALL/` | 820 | The interface (`ui/`): pictures, fonts, strings; sounds and speech |
| `Res1low.gob` | CD 1 `GAMEDATA/MININSTALL/` | 820 | The same names as `Res1hi.gob`, in less space |
| `Res2.gob` | install `Resource/`; both CDs | 3,610 | Textures, models, animations, scripts, AI, colormaps |
| `JK1.GOB` | install `Episode/`; both CDs | 296 | Single player: `episode.jk`, 25 levels, their scripts |
| `JK1MP.GOB` | install `Episode/`; both CDs | 22 | Multiplayer levels |
| `JK1CTF.GOB` | install `Episode/`; both CDs | 47 | Capture the Flag levels |

Names inside use `\` between folders, in lower case.

## Layout

All numbers little-endian.

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `GOB ` |
| 4 | 4 | Version: `0x14` in every archive |
| 8 | 4 | Where the index starts: 12 in every archive, just after the header |

### Index

A count (4 bytes), then that many entries of 136 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Where the file starts in the archive |
| 4 | 4 | Its size |
| 8 | 128 | Its name, ended by a 0; the rest is 0 |

The files follow the index, one after another with no gaps, in the
index's order, which isn't sorted by name.

## Behaviour

`JK.EXE` names the archives it opens by pattern (`%s.gob`,
`episode\%s`, `%s\gamedata\resource`), and says "Bad version %d for gob
file" when the version is wrong. Run in the emulator, it opens the
install's `Resource\Res1hi.gob` and `Resource\Res2.gob`, and for each
file it wants tries a loose file first: at the install's root, in its
`resource` folder, then in the CD's `gamedata\resource`; then the
archives ([systems/hosting.md](../systems/hosting.md)). Which archive
wins when two have a name isn't traced yet.

## In Reluva

- `GobArchive` (`src/engines/sith/formats/gob.h`) reads the index and
  reads each file from the archive when asked, as an `Archive`.
- `GameFiles` (`src/engines/sith/game_files.h`) opens every `.gob` in the
  install's `Resource` and `Episode` folders, and finds a name in the
  first that has it: an order of Reluva's own until the game's is known
  ([UNCONFIRMED.md](../UNCONFIRMED.md#guessed)).
- `reluva-tool sith gob <archive.gob>` prints the index.

## Sources and checks

- **Where the facts come from:** the six archives, read byte by byte;
  the editing community's published write-ups of the format, which agree;
  the error message and name patterns in `JK.EXE`'s strings.
- **How it was checked:** every archive's index read, its files found
  back to back to its end; `reluva-tool sith gob` on each;
  `sith.unit.gob` on archives made by the test.
