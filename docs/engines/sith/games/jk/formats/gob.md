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
archives ([systems/hosting.md](../systems/hosting.md)).

The game looks for a name in five places in turn (`0x40f4c0`), each a
folder with up to 64 archives (`0x5541e0`, `0x184` bytes apart): a
loose file in the folder, then the folder's archives in the order its
listing gave them (`0x40f190`, by the pattern `gob`), the first that has
the name winning *(from the code)*:

| Order | Folder | Archives |
|---|---|---|
| 1 | The `-path` (or `/path`) option's (`0x4014dc`, `0x401782`) | Its `.gob`s, if it's given (`0x40e44c`) |
| 2 | `episode\<the episode>` in the install (`0x40e66a`) | The episode's own, `<the episode>.gob` (`0x40e64e`, `0x40e741`) |
| 3 | `<CD>\gamedata\episode\<the episode>` (`0x40e819`) | Those of the episode's folder on the CD |
| 4 | `resource` in the install (`0x40e460`) | `Res1hi.gob`, `Res2.gob` |
| 5 | `<CD>\gamedata
esource` (`0x40e9e5`) | The CD's, unless the registry's `InstallType` is 9 (`0x40ea04`), which it is when it isn't set (`0x41ec53`) |

One episode is open at a time: opening one closes the archives of
places 2 and 3 first (`0x40e5c0`) *(from the code)*. So an episode's
files come before the resources'. In the US retail files no name is in
two archives that are open together: of the 4,791 names, only
`episode.jk` and `misc\cogstrings.uni` are in more than one archive,
each in all three episodes'.

## In Reluva

- `GobArchive` (`src/engines/sith/formats/gob.h`) reads the index and
  reads each file from the archive when asked, as an `Archive`.
- `GameFiles` (`src/engines/sith/game_files.h`) opens every `.gob` in the
  install's `Resource` and `Episode` folders, and finds a name as the
  game does, in the episodes' archives before the resources'. It has all
  the episodes open where the game has one, so for the two names they
  share the first by name, `JK1.GOB`, the single-player game's, wins.
- `reluva-tool sith gob <archive.gob>` prints the index.

## Sources and checks

- **Where the facts come from:** the six archives, read byte by byte;
  the editing community's published write-ups of the format, which agree;
  the error message and name patterns in `JK.EXE`'s strings.
- **How it was checked:** every archive's index read, its files found
  back to back to its end; `reluva-tool sith gob` on each;
  `sith.unit.gob` on archives made by the test.
