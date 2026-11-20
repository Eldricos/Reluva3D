# Star Trek: Birth of the Federation: saved games (`.sav`)

> **Status:** partial · **Code:** `src/engines/botf/formats/save_game.cpp` · **Checked on:** no saved game yet: built from the Ultimate Editor's reader, and test files made to it · **Confidence:** published unless marked

## Overview

A saved game is an archive of the game's state, one file per part of it
(`gameInfo`, `techInfo`, the empires, the map, the AI ...). The install
keeps one per Load Game slot, `game0.sav` to `game11.sav`, top down (a
fresh install has the four tutorials as `game0.sav` to `game3.sav`), with
`Auto.sav` and `Last.sav` beside them *(inferred: what `Auto` and `Last`
are for)*. Reluva reads only what the load and save screens show: each
game's description.

## Layout

The archive is [alt.res's](alt-res.md): a 24-byte header (a 16-byte name,
the directory's offset, the number of files), each file's LZSS data, and a
directory of 40-byte entries. Two differences:

- **The header's name** is the saved game's own, up to 15 characters (the
  Ultimate Editor refuses longer ones), not "alt.res".
- **The LZSS window** starts filled with spaces (`0x20`) for every file but
  `techInfo`, whose window starts filled with zeros as `alt.res`'s does.
  Decoded with the wrong fill, any reference into the window before it's
  been written gives the wrong bytes.

The Ultimate Editor's decoder writes its window from 4,079 and reads a
reference's position plus 4,078, which comes to `alt.res`'s: writing from
1, references read where they say. It fills all but the window's last 17
bytes; those are the first 17 written, so no reference reads them before
they are, and Reluva fills the whole window.

### `gameInfo`

The parts the screens use:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[13]` | a file name, unused |
| 0x0D | `char[51]` | the game's description, zero-terminated, Windows-1252 |
| 0x40 | `i32` | the turn, 0 the first |
| 0x4C | `i32` | 1 for a multiplayer game |

Further on are the game's setup (the galaxy's shape and size, the minor
races, the difficulty, random events, the timers, each empire's starting
level) and each empire's statistics, which Reluva doesn't read yet.

## In Reluva

`readSaveSummary` (`src/engines/botf/formats/save_game.cpp`) opens a save
with `AltResArchive`, takes its name from the header, and decodes
`gameInfo` with the window full of spaces for the description and turn.
`decompressLzss` takes the window's fill. The unit tests
(`botf.unit.archives`: "lzss" and "saved games") cover both.

## Sources and checks

- **Where the facts come from:** the Ultimate Editor's source
  (`ue/util/file/LzssFile.java` for the archive and its header's name,
  `ue/util/file/LzssInputStream.java` for the window,
  `ue/edit/sav/tools/SavTools.java` for which files are text,
  `ue/edit/sav/files/GameInfo.java` for `gameInfo`'s fields; GPL-2.0, read
  for the layout only; see [tables.md](tables.md#other-sources)), November
  2026. The slots' file names are from a listing of the user's install.
- **How it was checked:** not yet on a saved game: Reluva's reader is
  checked only on test files built to this layout. The window's fill is
  from the editor alone.

## Open questions

- Whether the Load Game screen's description field shows `gameInfo`'s
  description or the header's name (its name field shows the file's
  name): settled by a tutorial's saved game, whose screen shows "Tutorial
  #1".
- What `Auto.sav` and `Last.sav` are: presumably autosave and the game the
  main menu's Continue loads.
- The rest of `gameInfo`, and every other file in the archive.
