# LEGO Loco: boards (`.sav`)

> **Status:** partial · **Code:** `src/engines/legoloco/formats/board.cpp` · **Checked on:** the CD (version 1.0.18.2): its 8 layouts in `SAVEGAME`, 4 screen saver boards in `ScrSaver` and 17 area layouts in the resource file, all read (November 2026) · **Confidence:** layout from the code; meanings guessed unless marked

## Overview

A board is everything on the play area: the squares, the pieces on them
with the minifigures inside, and the trains. The same format holds:

- the ready-made layouts in `art-res\SAVEGAME` (`SNOWBALL.SAV`,
  `MOONBASE.SAV`...), where the player's own saves go too;
- the screen saver's boards in `art-res\ScrSaver`, one per screen width
  (`SS800.sav` to `SS1280.sav`);
- the 17 area layouts in the [resource file](resource.md)
  (`layouts\area1.sav`...);
- `art-res\~curr`, the board as it was last left: on the CD, a copy of
  `FOREST.SAV` *(seen, not traced)*.

`loco.exe` reads a board at `0x447ba0`, from the resource file or from a
file on disk, and places its pieces at `0x41d65d`.

## Layout

All values little-endian. The parts follow one another with nothing
between them.

### The header

276 bytes (`0x114`):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | 8 in every file *(seen, not traced)*: a version *(guessed)* |
| 2 | 2 | columns: 64, or 72 and 80 for the screen saver's wider boards |
| 4 | 2 | rows: 48, or 54 and 64 |
| 6 | 2 | 0 in every file |
| 8 | 4 | the number of pieces |
| 12 | 2 | the number of trains |
| 14 | 262 | the backdrop: a picture's name in `art-res\backdrop` (`WINTER`, `MOON`), or empty; ending in a zero, the rest zero |

### The squares

Columns × rows bytes, row by row. The CD's boards hold 0, 2, 3, 5, 6 and
7; drawn as text, the values trace the layouts' track, roads and
buildings, but which value is which isn't known.

### Pieces

As many as the header counts, 128 bytes each, read one at a time at
`0x447db0`:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | the piece: its resource number |
| 2 | 2 | its column |
| 4 | 2 | its row |
| 6 | 2 | 0 in every file *(seen, not traced)* |
| 8 | 4 | 0 to 5 on the CD; given to the piece through a method of its own (at its table's `+0x1c`), skipped for resource 2130 (`0x852`) and some others |
| 12 | 4 | kept by the piece (at its `+0xbc`) |
| 16 | 12 | its name, ending in a zero: `Building` until the player names it (`Hospital`, `Police`), or empty |
| 28 | 100 | five minifigure slots, 20 bytes each ([below](#minifigure-slots)) |

The game places each piece at its column and row plus half the
difference between its own board's size and the file's, so a smaller
layout lands in the middle of a larger board (`0x41d693`).

### Minifigure slots

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | the minifigure's resource number, or 0 for an empty slot |
| 2 | 2 | 0 *(seen, not traced)* |
| 4 | 4 | kept by the minifigure (at its `+0x94`) |
| 8 | 12 | its name (`Rachel`, `Gribbet`), ending in a zero; the game gives it to the minifigure unless it's `PARTY` |

### Trains

As many as the header counts, after the pieces, 44 bytes each, read at
`0x447df0`:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | not yet known |
| 32 | 12 | its name, ending in a zero |

### Boards with fewer pieces than counted

Every board in `SAVEGAME` and `ScrSaver` is exactly as long as its
counts say. The 17 area layouts in the resource file count 1 to 4 more
pieces than they hold. The game reads pieces until the file ends, and a
read past the end gives nothing, so it places those there are and no
trains.

## In Reluva

- `src/engines/legoloco/formats/board.cpp`: `parseBoard` reads a board
  into `Board`: the header, the squares, and the pieces and trains there
  are, as the game does for a board that ends early. It fails only for
  a file that ends inside its header or squares.
- `reluva-tool legoloco sav <file.sav>` prints a board: its squares as
  text, its pieces with their minifigures, and its trains;
  `reluva-tool legoloco sav <game folder> [name.sav]` does the same for
  a board in the resource file, or reads every board of a copy.

## Sources and checks

- **Where the facts come from:** the sizes and order of the parts from
  `loco.exe`'s reader (`0x447ba0` reads the header and the squares,
  `0x447db0` a piece, `0x447df0` a train) and from the code that places
  them (`0x41d65d`), which reads the counts, the backdrop, and the
  pieces', minifigures' and trains' fields at the offsets given. The
  values and names were seen by comparing the CD's 29 boards.
- **How it was checked:** `reluva-tool legoloco sav` reads all 29 of the
  CD's boards; each layout and screen saver board is exactly 276 bytes,
  plus its squares, plus 128 bytes for each piece and 44 for each train
  it counts, and the area layouts are whole pieces short. The unit tests
  (`legoloco.unit.boards`) read a board built in the test, one with
  fewer pieces than it counts, and refuse a short one.

## Open questions

- What the squares' values mean, and the pieces' values at 8 and 12.
- The trains' first 32 bytes.
- How the player's own saves differ, if they do: none was studied yet.
