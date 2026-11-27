# SimCoaster: Reluva's saved games (`.rsv`)

> **Status:** decoded · **Code:** `src/engines/tpw/saves.cpp`, `src/engines/tpw/park/saving.cpp`, `src/engines/tpw/park/state_io.h` · **Checked on:** parks of all three worlds saved and loaded back · **Confidence:** confirmed

## Overview

Reluva saves a game in a file of its own: the game's own saves
(`.TPWS`, [tpws.md](tpws.md)) are its memory written out as it lay, of
which only the park's grid is worked out, so they can't be written. A
Reluva save holds what play has changed of a park; the rest, the
park's settings, its world's and the mission's, is read again as it
loads ([saving.md](../systems/saving.md)). The saves are in the game's
save folder's `saves/`, named `save1.rsv`, `save2.rsv` and on; a save's
own name is inside it.

## Layout

All numbers little-endian. A text or a block of bytes is its length (4
bytes), then its bytes; texts are UTF-8.

### The file

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | `RLVSAVE`, then 0x1A |
| 0x08 | 4 | The file's version: 1 |
| 0x0C | | The save's name, as the player gave it |
| | | The player's name, whose save it is |
| | | The world's folder in `Data`: `levels/science` |
| | 4 | The player's objective as it was saved: the mission the park's set up for |
| | 4 | The park's day, which its date shows |
| | | The park's state, a block |
| | | The screen's state, a block |

### The park's state

Its version (4 bytes, 4), then each field the park keeps in turn, as
`Park::ioState` lists them: the missions' progress, golden tickets and
rank; the money, the accounts, the loans and shares; the messages
waiting; the challenges' standing, each by its number; the counts for
challenges and awards; the date and the weather; the ground's cells and
the land's, and the fence's corners; the objects (with their tracks), the queues, the litter and stink bombs; the
research projects and what's researched; the staff (each with the
ability they're learning), the candidates and the wage demands; the
visitors; the next numbers to give; and whether an obstacle is being
cleared, a flag, then the staff at it and the days they've worked. A number is
written as it's held: 4 bytes for an `int` or a `float`, 8 for the
park's times in milliseconds, 1 for a flag or a kind; a list is its
count (4 bytes) then its members, each written likewise, field by
field.

### The screen's state

Its version (4 bytes, 1); the Message Center's messages, a list, each
its message as the park's, its day and whether it's read; the advisor's
cues had, a list of numbers.

## In Reluva

`writeSave`, `readSave` and `listSaves` (`src/engines/tpw/saves.cpp`);
`Park::saveState` and `restoreState` (`src/engines/tpw/park/saving.cpp`),
their fields in one list for both; `ParkScreen::saveState` and
`restoreState`. `reluva-tool tpw save-check <game folder> <world folder>
[seconds]` plays a park on, saves it to a file and loads it back over a
new park of its world, and says whether it's the same, then and as both
play on; the game-data test runs it (`tpw.gamedata.park`). The unit
tests (`tpw.unit.park`) save, list, load and refuse cut and other
versions' saves.

## Sources and checks

- **Where the facts come from:** Reluva's own; nothing of the game's.
- **How it was checked:** `save-check` on Land of Invention, Polar Zone
  and Arabian Nights: the state loaded back the same byte for byte, and
  the same after a minute and two minutes more.
