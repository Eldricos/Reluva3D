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
| 13 | 1 | What's built: 0 nothing, 1 path, 2 a queue; 1 on the entrance's path and on the recording's, 2 on a short run to a ride beside the path *(inferred)* |
| 21 | 1 | On path: 0x5A or 0xB4 (90, 180), an angle perhaps *(guessed)* |
| 46 | 1 | The cell's tile of the world's attribute map *(confirmed)* |
| 73 | 1 | 1 on the park's own land, it seems *(guessed)* |

The rest vary but aren't worked out: some trace the river, the lake and
the obstacles.

The prebuilt park's cells: 76 path, 5 queue, its tiles Land of
Invention's. Its path runs two cells wide from the entrance's two cells
up to y 42, with branches, as the recording's new park has it as its
briefing closes (2:34), the same shape *(confirmed)*. Its queue leads
to a ride beside the path that the recording shows standing there at
the start, among the sections not read.

## In Reluva

`SavedPark` (`src/engines/tpw/formats/saved_park.cpp`) reads a save's
version, inflates its state and reads the grid: each cell's kind, flags,
tile and what's built on it. `Park` lays the prebuilt park's path and
queue over a new park of the world it matches
([paths.md](../systems/paths.md)).

`reluva-tool tpw tpws-check <game folder> [save]` reads the prebuilt
park, or another save, and says what's in its grid and which world's it
is; the game-files test runs it (`tpw.gamedata.saves`). The unit tests
(`tpw.unit.formats`) read a save built in the test, and fail a cut one.

## Sources and checks

- **Where the facts come from:** the one save, `prebuilt.TPWS` of the US
  release (November 2026): its chunk inflated; the grid found by the
  period of its records; its fields by drawing each byte of every cell as
  a picture of the park, and setting them beside the world's attribute
  map and the recording.
- **How it was checked:** `tpws-check`: 16,384 cells read to the first
  section, every tile the attribute map's; the path laid in a new park
  drawn beside the recording at 2:35.

## Open questions

- Everything else in the state: the records before the grid, the
  sections after (the objects, the money, the visitors), the cells'
  other fields and their entries.
- Whether every new park of Land of Invention starts from it, and what
  the other worlds start from.
- Saving a park: a save of the game's own, to set beside this one.
