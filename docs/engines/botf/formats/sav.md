# Star Trek: Birth of the Federation: saved games (`.sav`)

> **Status:** partial · **Code:** `src/engines/botf/formats/save_game.cpp` · **Checked on:** `game0.sav` of the English retail install (Tutorial #1): every file decoded, `gameInfo`'s fields read (November 2026) · **Confidence:** confirmed unless marked

## Overview

A saved game is an archive of the game's state, one file per part of it
(`gameInfo`, `techInfo`, the empires, the map, the AI ...). The install
keeps one per Load Game slot, `game0.sav` to `game11.sav`, top down (a
fresh install has the four tutorials as `game0.sav` to `game3.sav`), with
`Auto.sav` and `Last.sav` beside them *(inferred: what `Auto` and `Last`
are for)*. A fresh install has every one of them: the four tutorials, and
the other ten files 0 bytes long (`Game4.sav` to `Game9.sav`, `Auto.sav`
dated December 1998, `game10.sav` and `game11.sav` August 1998,
`Last.sav` April 1999). An empty slot on the Load Game screen is one of
those. Reluva reads only what the load and save screens show: each
game's description.

## Layout

The archive is [alt.res's](alt-res.md): a 24-byte header (a 16-byte name,
the directory's offset, the number of files), each file's LZSS data, and a
directory of 40-byte entries. Two differences:

- **The header's name** is the save's file name, `game0.sav` in
  `game0.sav`, up to 15 characters (the Ultimate Editor refuses longer
  ones), not "alt.res".
- **The LZSS window** starts filled with spaces (`0x20`) for every file but
  `techInfo`, whose window starts filled with zeros as `alt.res`'s does
  *(published)*. Decoded with the wrong fill, any reference into the window
  before it's been written gives the wrong bytes. In `game0.sav` no file
  but `result.lst` has such a reference, so the save doesn't show which
  fill is right; `result.lst` is stored in 61 pieces (its entry's sixth
  field), which Reluva doesn't read yet.

`auto.sav`, an autosave at turn 1, holds 539 files; there too only
`result.lst` decodes differently with the two fills. `game0.sav` is 81,944 bytes: 249 files, the directory at 55,924, every
file decoding to its stated size. Some files are empty (`starBaseInfo`,
`GTFStasis`: offset 0 or the next file's, both sizes 0).

The Ultimate Editor's decoder writes its window from 4,079 and reads a
reference's position plus 4,078, which comes to `alt.res`'s: writing from
1, references read where they say. It fills all but the window's last 17
bytes; those are the first 17 written, so no reference reads them before
they are, and Reluva fills the whole window *(published)*.

### `gameInfo`

The parts the screens use:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[13]` | the save's file name again (`game0.sav`) |
| 0x0D | `char[51]` | the game's description, zero-terminated, Windows-1252: "Tutorial #1", what the Load Game screen shows |
| 0x40 | `i32` | the turn, 0 the first (0 in Tutorial #1) *(published)* |
| 0x4C | `i32` | 1 for a multiplayer game (0 in both saves) *(published)* |

Further on is the game's setup, which Reluva doesn't read yet. The
offsets are the Ultimate Editor's; what the values mean was checked on two
saves: the first tutorial's `game0.sav`, and an `auto.sav` at turn 1 of a
game the user set up as the Federation, every empire Early, a ring galaxy
of medium size, many minor races, Normal difficulty and random events on:

| Offset | Type | Field | Tutorial #1 | `auto.sav` |
|---|---|---|---|---|
| 0x252 | `u8` | the player's empire, 0 Cardassians to 4 Romulans (`race.rst`'s order) | 4: the Romulans, as the manual says the tutorials are | 1: the Federation |
| 0x254 | `u8[5]` | each major empire's civilization level, 0 Beginning to 4 Advanced | 1 1 1 1 2 | 1 1 1 1 1: all Early |
| 0x260 | `i32` | galaxy shape: 0 irregular, 1 elliptical, 2 ring, 3 spiral | 0 | 2: ring |
| 0x264 | `i32` | galaxy size: 0 small, 1 medium, 2 large | 1 | 1: medium |
| 0x268 | `i32` | minor races: 0 none, 1 few, 2 some, 3 many | 3 | 3: many |
| 0x26C | `i16` | difficulty: 1 simple to 5 impossible, one more than the setup screen's order | 1 | 3: normal |
| 0x26E | `i16` | random events: 1 on | 0 | 1: on |
| 0x290 | `i32` | tactical combat: 0 manual, 2 automatic (`last.sav`, beside `Stbof.ini`'s `TACTICAL=ABSTRACT`) | 0 | 0 |
| 0x2A4, 0x2A8 | `i32` | the strategic and tactical timers: 0 none *(published)* | 0, 0 | 0, 0 |

A third save, the user's `last.sav` (664 files, at turn 318, the
Federation), holds 2 for tactical combat, 1 (elliptical) for the shape, 0
(small) for the size, no minor races, difficulty 1, every empire at 0
and random events off: the setup `Stbof.ini` keeps
([systems/game-files.md](../systems/game-files.md)). Its header's name is
`auto.sav`, so it's presumably an autosave copied.

Each value checked is one of those the saves show; the others in each
field (a small or large galaxy, a spiral one ...) are the setup screen's
order, which the ones seen fit *(inferred)*. The timers weren't set away
from none in any of them, so their other values are the editor's. `auto.sav`'s description is empty, and its header's name is
`auto.sav`.

## In Reluva

`readSaveSummary` (`src/engines/botf/formats/save_game.cpp`) opens a save
with `AltResArchive`, takes its name from the header, and decodes
`gameInfo` with the window full of spaces for the description and turn;
on `game0.sav` it gives "game0.sav", "Tutorial #1", turn 0.
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
- **How it was checked:** on the user's `game0.sav` (Tutorial #1, from a
  fresh install) and `auto.sav` (539 files, at turn 1), November 2026: every file decoded with each window fill,
  and `readSaveSummary` run on it, its description matching the Load Game
  screenshot's. The unit tests use files built to this layout, not the
  game's. The window's fill is from the editor alone.

## Open questions

- Whether the Load Game screen's name field shows the file's name or the
  header's: they're the same in `game0.sav`.
- Which fill each file's window starts with: settled by a save with a
  reference into the unwritten window, or by `result.lst` once its pieces
  are read.
- The timers' other values, from a game set up with them.
- What `Auto.sav` and `Last.sav` are: presumably the autosave and the
  game the main menu's Continue loads, `Last.sav` a copy of an autosave
  (its header names it `auto.sav`).
- The rest of `gameInfo`, and every other file in the archive.
