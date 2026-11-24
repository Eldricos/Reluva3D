# SimCoaster: saved parks (`.TPWS`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/saved_park.cpp` · **Checked on:** the US release's one save, `Data/PreBuilt/prebuilt.TPWS` · **Confidence:** inferred unless marked

## Overview

A park saved by the game is a `.TPWS` file. The game ships one,
`Data/PreBuilt/prebuilt.TPWS` (37,718 bytes), a Land of Invention park
with a network of path and a queue, which is the path a new park there
starts with ([paths.md](../systems/paths.md#where-path-can-go)). After a
short header the park's whole state is one zlib stream, 1,692,641 bytes
inflated: much of it the game's structures written as they lay in
memory, padding and all (runs of 0xCD, a debugging build's fill for
memory never set). Of the state, only the park's grid of cells is
worked out, and of each cell only a few fields.

## Layout

All numbers little-endian.

### The file

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | 500 |
| 0x04 | 4 | 0x1301 |
| 0x08 | 1 | 0 |
| 0x09 | | "Version: Beta_21", then CR LF and two zeros |
| 0x1D | | A copyright notice in UTF-16, to its two zero bytes, then zeros |
| 0x61D | 8 | Not worked out |
| 0x625 | | The state's chunk |

The state's chunk:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `BILZ` |
| 0x04 | 4 | The state's size, inflated *(confirmed)* |
| 0x08 | 4 | The chunk's size, from its tag: to the file's end *(confirmed)* |
| 0x0C | 16 | 15, 9, 0, 0: not worked out |
| 0x1C | | A zlib stream (`78 9C`), which inflates to the state's size *(confirmed)* |

### The state

The first 7 KB or so aren't worked out: lists of small records, some
holding park cells (`0F 2F 16` then `14 2F 16`: cell (47, 22)). Then
the grid, then sections each with its size and a four-letter tag stored
backwards: `EATC`, then `LDMT` and `EJBO` pairs ("TMDL", "OBJE": the
park's objects, it seems), `HJBO`, `STUC`, `QHHC`, `SMIU`, `KNAB` and
more, none worked out.

### The grid

128 × 128 cells, column by column of the park's cells: cell (x, y) is
the record y × 128 + x, (x, y) as the settings number them *(confirmed:
the tiles below are the world's attribute map's, every one)*. The grid
has no tag; it ends where the first tagged section (`EATC`, with its
size) begins, and starts at the first byte from which 16,384 whole
records run on to there.

A record is 86 bytes of the cell's own, then −1 (`FF FF`). Thirteen of
them have an entry of 10 bytes before the 86: (1, −1), (1, 0) or
(3, −2) and zeros, heights perhaps *(guessed)*; those cells are kind 7,
the rest kind 3.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Its kind: 3, or 7 with an entry before |
| 1 | 1 | Flags, not worked out (0x01, 0x10, 0x40 in some) |
| 2 | 1 | Bits, 0x40 or 0xC0 outside the park: not worked out |
| 8 | 2 | On path: nine bits, the cell and its eight neighbours, which are path, it seems (0x1FF in a path's middle) *(inferred)* |
| 4 | 1 | On a queue: its cells' order, counting up from the ride (0x36 to 0x3A) |
| 13 | 1 | What's built: 0 nothing, 1 path, 2 a queue; 1 on the entrance's path and on the recording's, 2 on a short run to a ride beside the path *(inferred)* |
| 17 | 1 | The piece it's drawn as. On a queue: 2 straight, 4 a bend, 5 where it meets the path. On path, the game's own tile: 9 a corner, 10 and 20 an edge, 11 and 12 a middle *(inferred)* |
| 21 | 2 | That piece's angle, degrees: 0, 90, 180 or 270. At 0 a path's corner opens east and south, at 90 south and west: an angle turns east (+x) towards south (+y), as every corner of the prebuilt park's does *(inferred)* |
| 25 | 1 | What fills it: 0 bare land, 1 path, 3 a queue, 4 an object, 9 and 10 an attraction's entrance and exit (its shape's `2` and `N`), 5 a margin kept clear about the lake, the river, the park's edges and the attractions behind hoardings, 7 outside the park, as they lie drawn as a picture of the park *(inferred)*; 8, 21 and 30 not worked out |
| 46 | 1 | The cell's tile of the world's attribute map *(confirmed)* |
| 51 | 1 | On an object's first cell, a number of its, not worked out (0x21 to 0x25 for the robot, fountain and rocks) |
| 73 | 1 | 1 on the park's own land, it seems *(guessed)* |

The rest vary but aren't worked out: some trace the river, the lake and
the obstacles.

The prebuilt park's cells: 76 path, 5 queue, its tiles Land of
Invention's. Its path runs two cells wide from the entrance's two cells
up to y 42, with branches, as the recording's new park has it as its
briefing closes (2:34), the same shape *(confirmed)*. Its queue leads
from the path at (47, 24) along to (43, 24), turns, and ends at the
entrance of the Giant Robot Swing, a ride the recording shows standing
there at the start.

### The sections

After the grid, each section has its size, then its tag, backwards; an
object's record is `LDMT` with a second tag. In order: `EATC`, the
objects (`EJBO`, 23 of them, then `HJBO`), `STUC`, `QHHC`, `SMIU`,
`KNAB` ("BANK"), `TAEW` ("WEAT", the weather, it seems), `RNRT`,
`BALR`, `KRAP` ("PARK", 0x20EE bytes), `SGAT`, `IVDA` ("ADVI", the
advisor), `QHEM` and `FATS` ("STAF", the staff). None for visitors: the
prebuilt park has none.

The `BANK` section holds the park's balance, 47,375, twice, and
−2,625, then a table of loans of 20 bytes each: an amount, a rate and
months, then a word not worked out; the first three the general
settings' `LoanInfo`, 100,000, 50,000 and 25,000 at 20% over 36 months,
the same numbers *(inferred)*, then 10,000 likewise and 18,000 at 23%
over 24. A new park's money is the settings'
50,000 whatever the save's, as the recording's is.

### The objects

Each object the park has is a record of the section tagged `LDMT` then
`EJBO` ("TMDL", "OBJE"), after the grid. From the `LDMT`:

| Offset | Size | Field |
|---|---|---|
| 8 | 2 | Its first cell's x, 8.8 fixed point: the cell and a half |
| 10 | 2 | Its first cell's y, likewise |
| 16 | 4 | Its angle, degrees |
| 20 | 2 | Its number: its settings' `Info.Id` (`rides/robot/robot.sam`'s 1101), as `Data/Objects.txt` allots them (1100s rides, 1400s features, 1700s obstacles), each an object of the world's *(confirmed)* |
| 22 | 2 | 2000 in all |

Then its name, at 0x3C: UTF-16 characters four bytes apart, with older
text between them where the buffer was written twice ("Giant " and
"Swing" alternating, then "Robot"), so not to be read as it is; and
much more, not worked out. The prebuilt park has 24: 14 placed,
among them the Giant Robot Swing at (42, 26), the Electric Fountain at
(49, 23), three rocks, the mission's fence (1700) at (41, 48) and four
more obstacles, and four rides in the park's other areas; ten kept
aside at (0.5, 0.5), the gates (1601) and others the game places
itself, it seems *(inferred)*.

An object's first cell is the corner its model is laid from: its model
covers x and z from 0 to 10 a cell of its shape (the robot's 40 by 50,
its `Info.Shape` four cells by five) *(confirmed)*. The robot's cells
in the grid are its shape turned end over end, its last row, `*2N*`,
at y 26: its first cell is that row's first, its entrance `2` at
(43, 26) by the queue's end.

## In Reluva

`SavedPark` (`src/engines/tpw/formats/saved_park.cpp`) reads a save's
version, inflates its state and reads the grid (each cell's kind,
flags, tile, what's built on it and fills it, its piece and angle) and
the objects. `Park` lays the prebuilt park's path, queue and objects
over a new park of the world it matches
([paths.md](../systems/paths.md)), and `ParkScreen` draws them.

`reluva-tool tpw tpws-check <game folder> [save] [-objects]` reads the
prebuilt park, or another save, and says what's in its grid, how many
objects it has (and, with `-objects`, lists them) and which world's it
is; the game-files test runs it (`tpw.gamedata.saves`). The unit tests
(`tpw.unit.formats`) read a save built in the test, and fail a cut one.

## Sources and checks

- **Where the facts come from:** the one save, `prebuilt.TPWS` of the US
  release (November 2026): its chunk inflated; the grid found by the
  period of its records; its fields by drawing each byte of every cell as
  a picture of the park, and setting them beside the world's attribute
  map and the recording; the objects by their numbers, against the
  world's objects' settings and models' sizes.
- **How it was checked:** `tpws-check`: 16,384 cells read to the first
  section, every tile the attribute map's; the path, queue and objects
  laid in a new park drawn beside the recording at 2:35: the robot left
  of the path beyond its cross-piece, the fountain right, the rocks where
  the recording's are.

## Open questions

- Everything else in the state: the records before the grid, the
  sections' fields (the bank's other numbers, the park's, the staff's),
  the objects' other fields, the cells' other fields and their entries.
- A save the game wrote of a park played, with visitors and money
  spent, to set the sections against.
- Whether every new park of Land of Invention starts from it, and what
  the other worlds start from.
- Saving a park.
