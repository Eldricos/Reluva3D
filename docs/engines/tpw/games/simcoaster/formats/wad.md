# SimCoaster: archives (`.wad`)

> **Status:** decoded · **Code:** `src/engines/tpw/formats/wad.cpp` · **Checked on:** the US release's 303 archives, every file read · **Confidence:** confirmed unless marked

## Overview

Most of the game's files are kept in archives: 303 of them under `Data/`
(15,007 files in all), from `Data/ui.wad` (the front end, 1,462 files) and
`Data/esprites.wad` (the guests' costumes) to one per ride, shop and
scenery item (`Data/levels/water/rides/TourRide.wad`). Three are named
`.WAD`, in capitals (`Data/levels/wIntro/features/`); they're the same.
Most files in them are compressed with EA's RefPack
([refpack.md](../../../../../common/refpack.md)).

Each archive stands for the folder of its name: `ui.wad` beside a loose
`Data/ui/` folder (the cursors), and no file in an archive has a loose
namesake *(inferred)*.

## Layout

All numbers little-endian. The header, then a record for each file, then
the files' data back to back in the records' order, then their names:
nothing between them, and nothing after.

### The header

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `DWFB` |
| 0x04 | 4 | 2: the version *(guessed)* |
| 0x08 | 0x40 | 0 |
| 0x48 | 4 | How many files |
| 0x4c | 4 | Where the names are, from the start of the file |
| 0x50 | 4 | The names' size, in bytes |
| 0x54 | 4 | 0 |

### A file's record

0x28 bytes each, from 0x58.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Not worked out: it differs for one name in two folders, so it depends on the folder too; perhaps a hash of the path *(guessed)*. No simple sum of the path's bytes gives it |
| 0x04 | 4 | Where its name is, from the start of the file |
| 0x08 | 4 | The name's length, with its NUL |
| 0x0c | 4 | Where its data is, from the start of the file |
| 0x10 | 4 | Its data's size, in the archive |
| 0x14 | 4 | How it's stored: 0 as it is, 4 RefPack |
| 0x18 | 4 | Its size unpacked; 0 when stored as it is |
| 0x1c | 12 | 0 |

13,075 files are RefPack (each starting `10 FB`), 1,932 stored.

### Names and folders

Names are in the archive's own code page (all ASCII here), with `\`
between folders. A name with a folder sets the folder for the names
after it, which have none of their own: `stexture\birdbeak.tga`, then
`birdbody.tga`, which is `stexture\birdbody.tga`, until the next name
with a folder. `esprites.wad`'s have two levels
(`Arabian\Costumeheads\SPR_AK.ESP`). The names before the first folder
are at the archive's top.

### `qickload.txt`

Most folders in an archive (851 of them) have a `qickload.txt` (so
spelled): a list of names, one a line, ending `\r\n`. It names the
folder's models and pictures but not its scripts, settings or signs
(`.hmp`, `.sam`, `.rse`, `.sgn`): a list of what to load first
*(inferred)*.

## In Reluva

`WadArchive` (`src/engines/tpw/formats/wad.cpp`) reads the records and
names, and a file when it's asked for, unpacking it with the shared
RefPack code; a file that doesn't unpack to its recorded size is an
error. `mountGame` (`src/engines/tpw/game_files.cpp`) puts the `Data`
folder together as one set of files: each archive as the folder of its
name, the loose files over them.

`reluva-tool tpw wad-list` lists an archive's files with their sizes and
how each is stored; `reluva-tool tpw wad-extract` writes them out;
`reluva-tool tpw data-check` reads every file of a game's `Data` folder,
which the game-files test runs (`tpw.gamedata.data`).

## Sources and checks

- **Where the facts come from:** the US release's archives, compared with
  each other: the header's fields by where the records and names end,
  the records' by the data they point at, the folders by the names in
  each folder's `qickload.txt` and by every path coming out unique
  (November 2026).
- **How it was checked:** every file of all 303 archives read with
  `reluva-tool tpw data-check`, each packed one unpacking to its recorded
  size; the same on a copy imported from the CD. The unit tests
  (`tpw.unit.formats`) read archives built in the test, with folders,
  both storage kinds, and damage.

## Open questions

- What the record's first word is, and whether the game uses it to find
  a file.
- Whether the game looks a file up in an archive by the folder the
  archive stands for, as Reluva does, or by the archive's name and the
  name inside it.
