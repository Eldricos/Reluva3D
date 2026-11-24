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
| 0 | 2 | the version: 8; the game loads no other (`0x448030`), and its load panel offers no other *(from the code)* |
| 2 | 2 | columns: 64, or 72 and 80 for the screen saver's wider boards |
| 4 | 2 | rows: 48, or 54 and 64 |
| 6 | 2 | 0 in every file |
| 8 | 4 | the number of pieces |
| 12 | 2 | the number of trains |
| 14 | 262 | the backdrop: a picture's name in `art-res\backdrop` (`WINTER`, `MOON`), or empty; ending in a zero, the rest zero |

### The squares

Columns × rows bytes, row by row: what kind of piece stands on each
square. The game writes each square's from the kind of the piece on top
there (the byte at `+8` of its definition, `0x457080`), and never reads
them back as it loads a board: it places the pieces, which fill their
own squares (`0x41d65d`). A network game keeps each area's squares, read
from its layout (`0x43d77f`) and sent between the players (`0x43f5f7`)
*(from the code)*, for the map *(guessed)*. Counting the squares under
each piece's `physical_occupancy` on the CD's layouts and screen saver
boards:

| Value | Under |
|---|---|
| 0 | nothing |
| 2 | scenery (79% of scenery's squares) |
| 3 | buildings (68%) |
| 5 | track (93%) |
| 6 | roads (all) |
| 7 | paths (95%) |

The rest are where pieces overlap, or where a piece's grid has a 0.

### Pieces

As many as the header counts, 128 bytes each, read one at a time at
`0x447db0`:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | the piece: its resource number |
| 2 | 2 | its column |
| 4 | 2 | its row |
| 6 | 2 | 0 in every file *(seen, not traced)* |
| 8 | 4 | the frame set it was showing ([dat.md](dat.md#the-frame-set-table)): a point's way, a tree's noisy or quiet set; given back to it through a method of its own (at its table's `+0x1c`), but for resource 2130 (`0x852`) and some others; the CD's, 0 to 9, are each within their pieces' sets |
| 12 | 4 | how many vehicles have passed it towards its `TotalVisits` surprise ([dat.md](dat.md#the-placement-part)), kept at its `+0xbc` (`0x434f21`) *(from the code)*; 0 in every file, as only one piece has the surprise |
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
| 0 | 4 | its engine: a resource number (6148, 6150, 6152 on the CD) |
| 4 | 12 | up to three wagons, a resource number each, 0 for none |
| 16 | 16 | a number for the engine and each wagon: 2 for engines and carriages, 3 for the freight wagon and tanker, 4 for the mail wagon, 0 for none *(seen, not traced)*; not read back: the game works each wagon's out from its number (`0x40eb60`) |
| 32 | 12 | its name, ending in a zero |

The game makes the train from its engine (`0x44be50`) and adds each
wagon (`0x44c220`), at the place `0x41e1f0` finds, then gives it its
name (`0x41d852`, `0x44dd8b`).

### Boards with fewer pieces than counted

Every board in `SAVEGAME` and `ScrSaver` is exactly as long as its
counts say. The 17 area layouts in the resource file count 1 to 4 more
pieces than they hold. The game reads pieces until the file ends, and a
read past the end gives nothing, so it places those there are and no
trains. A network game's area board, as the game keeps it in
`art-res\~curr`, came out the same in Reluva: 59 pieces counted, 57
there. The reason is in the game's writer (`0x41da3f`): the header
counts every piece the board holds, but only those standing in place
(their `+0xc0` 1) are written (`0x41db1f`) *(from the code)*; which
pieces aren't, when a board is saved, isn't known.

### A player's own boards

Layouts saved by the game in Reluva (`art-res\savegame\<name>.sav`, in
the save folder) are as the CD's: version 8, 64 by 48, every part as
long as its counts *(seen, not traced)*.

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

- **Where the facts come from:** the pieces' and minifigures' numbers
  are resources' ([resource-numbers.md](resource-numbers.md)). The sizes
  and order of the parts from
  `loco.exe`'s reader (`0x447ba0` reads the header and the squares,
  `0x447db0` a piece, `0x447df0` a train) and from the code that places
  them (`0x41d65d`), which reads the counts, the backdrop, and the
  pieces', minifigures' and trains' fields at the offsets given; a
  train's cars from the code that makes it (`0x44dc10`), and their
  numbers from the CD's 14 trains; a player's own boards from layouts
  the game saved in Reluva. The
  values and names were seen by comparing the CD's 29 boards.
- **How it was checked:** `reluva-tool legoloco sav` reads all 29 of the
  CD's boards; each layout and screen saver board is exactly 276 bytes,
  plus its squares, plus 128 bytes for each piece and 44 for each train
  it counts, and the area layouts are whole pieces short. The unit tests
  (`legoloco.unit.boards`) read a board built in the test, one with
  fewer pieces than it counts, and refuse a short one.

## Open questions

- Which pieces a board holds but doesn't write (their `+0xc0` not 1).
- Whether the map draws a network area's squares.
