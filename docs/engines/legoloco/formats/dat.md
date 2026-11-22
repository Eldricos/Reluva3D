# LEGO Loco: object definitions (`.dat`)

> **Status:** partial · **Code:** `src/engines/legoloco/formats/object_definition.cpp` · **Checked on:** the CD's resource file, version 1.0.18.2: all 627 `.dat` entries read (November 2026) · **Confidence:** from the code unless marked

## Overview

Every piece, person, vehicle, button and window part that the game draws
has a definition: a text file in the [resource file](resource.md) beside
the picture of the same name (`building\igloo.dat` with
`building\igloo.bmp`, and for pieces in the toolbox `building\igloo.but`,
its button). There are 627. They say where a piece stands on the board
and what fills it, and how the picture's frames make its animations.

One `.dat` is something else: `post\edit\colour.dat` is rows of three
numbers, colours for the postcard editor *(guessed)*.

## Layout

Text, in lines ending CR LF. The game reads it word by word, words split
by spaces, tabs and line ends. Each part is a list of keywords, each
followed by its values, and ends with `-9`. After the last `-9` the files
have `//` comments describing the fields, which the game never reads
*(seen, not traced)*. Keywords are matched ignoring case (`0x471480`):
the files have both `Hotspot` and `hotspot`, and `cursor_Frame_set`.

Which parts a file has depends on what it defines *(seen, not traced)*:

| Starts with | Files | What | Parts |
|---|---|---|---|
| `physical_occupancy` | 220 | a piece for the board: buildings, track, roads, scenery | placement, then pictures |
| `sex` | 49 | a minifigure | the person, then pictures |
| `button`, `total_number_of_frames`, others | 357 | a button, a cursor, a window's part | pictures |

### The placement part

Read by `loco.exe` at `0x41ea60`, in a loop until `-9`:

| Keyword | Values, as the game reads them | Notes |
|---|---|---|
| `physical_occupancy` | columns, rows, layers; then the grids ([below](#occupancy-grids)) | the squares the piece fills |
| `bitmap_occupancy` | columns, rows; then one grid | which squares its picture covers *(guessed)* |
| `entry_exit` | four numbers *(seen, not traced)* | where trains or people come and go *(guessed)* |
| `RMBSeq` | signed 16-bit | a resource number *(guessed)*: what a right click starts |
| `ClosedFS` | signed 16-bit | |
| `EEReplayDelay` | 32-bit | |
| `MaxMinifigForResource`, `MaxMobileForResource` | 16-bit, made 5 if larger | the game compares only their first four letters, `MaxM` (`0x41ec01`) |
| `LeisureDestination` | 16-bit | |
| `MaxEmployees` | 16-bit, made 5 if larger | |
| `PossibleEmployees`, `PossibleMinifigs` | signed 16-bit numbers, up to five *(seen, not traced)* | resource numbers of minifigures, -1 for none |
| `Shifts` | four two-digit numbers (`07 17 16 43`) *(seen, not traced)* | working hours *(guessed)* |
| `FreeToRoam` | four 32-bit numbers | a rectangle *(guessed)* |
| `ButtonVisible` | 16-bit | |
| `InsertSeq`, `MobileSeq`, `TotalVisits` | a sequence, each with an `EasterEgg` line after it, read by `0x41f2b0` | |

The files' comments describe the sequences: "Sequence count, id, id etc
where -1 is any and 0 is empty"; `EasterEgg`'s fields as "new
resourceid, new frameset, easter egg res, easter egg frameset, R/W/S, x,
y".

### Occupancy grids

In `loco.exe`: `0x41efa0`.

`physical_occupancy` gives the columns, the rows and the layers, then
the cells: layer by layer, each row by row, each row's columns in order.
`bitmap_occupancy` gives the columns and the rows and one layer's cells.
The game keeps them in a table of at most 13 columns, 9 rows and 7
layers, without checking. The CD's grids hold the numbers 0 to 6.

An igloo (`building\igloo.dat`) fills 3 by 3 squares in its first layer
and four of them in its second:

```text
physical_occupancy
3 3 2
1 1 1
1 1 1
1 1 1

0 1 1
0 1 1
0 0 0
```

### The pictures part

In `loco.exe`: `0x424e34`.

| Keyword | Values |
|---|---|
| `button` | `offset` and three numbers *(seen, not traced)* |
| `hotspot` | two numbers: a point in the picture *(guessed)* |
| `ShadowId`, `ShadowOffset`, `animation`, `semi-transparent`, `shadows`, `must/cant_have`, `MaxInstances` | *(seen, not traced)* |
| `total_number_of_frames` | 16-bit: how many frames the picture holds; 0 is made 1 |
| `number_of_frame_sets` | 16-bit: how many lines the frame-set table has |
| `cursor_frame_set` or `cursor/default_frame_set` | two signed 16-bit numbers, then the frame-set table |

### The frame-set table

In `loco.exe`: `0x42522f`, into 24-byte records.

One line per frame set: a name, then ten numbers. The game reads them in
this order; the names are the files' own comments' ("frameset name,
start frame, end frame, animation delay, split frames? 1 = TRUE, 0 =
FALSE" and "restart delay, next frame Set, sound, replay delay, flip X?
1 = TRUE, 0 = FALSE"), which leave the ninth number out:

| # | Read as | Field |
|---|---|---|
| | word | name (`default`, `walk-left`, `angry`...) |
| 1 | 16-bit | start frame |
| 2 | 16-bit | end frame: below the start, the frames run backwards *(inferred)* |
| 3 | 16-bit | delay between frames; 0 is made 1 |
| 4 | byte | split frames |
| 5 | 32-bit | restart delay |
| 6 | signed 16-bit | next frame set, or -1 |
| 7 | signed 16-bit | sound: a resource number *(guessed)* |
| 8 | 32-bit | replay delay |
| 9 | 16-bit | unnamed |
| 10 | byte | flip X: the frames drawn mirrored *(guessed)* |

A frame set of one frame whose next set is itself is given -1 instead
(`0x425315`). The game also checks that the frames and the next set are
within the counts, and remembers if one isn't (`0x4251fa`); what it does
then isn't known.

A minifigure's (`people\fireman\fireman.dat`):

```text
walk-left   11 0   4 0    0 0     0 0 0    1
walk-right   0 11  4 0    0 2     0 0 0    0
angry       28 31  3 0    1 5     0 0 0    0
```

### The person part

`sex` (`m` or `f`), `walk_speed` (two numbers), `PickUpSoundID`,
`employable`, `ResourceReplayDelay` *(seen, not traced)*.

## In Reluva

- `src/engines/legoloco/formats/object_definition.cpp`:
  `parseObjectDefinition` reads a definition as the game does, word by
  word: the occupancy grids and the frame-set table into their own
  fields (`Occupancy`, `FrameSet`), with the game's corrections (a delay
  of 0, a lone frame naming itself), and every other keyword as a line
  with the words after it on the same line. It refuses a grid larger
  than the game's table, and numbers that aren't numbers.
- `reluva-tool legoloco dat <folder> [name.dat]` prints a definition,
  or reads every one and lists any that fail.

## Sources and checks

- **Where the facts come from:** the keywords and their order from the
  CD's 627 files; how each value is read, and the corrections, from
  `loco.exe`'s readers at the addresses given. The fields' names are the
  files' own comments'.
- **How it was checked:** `reluva-tool legoloco dat` reads all 627
  without a failure; in all 551 with frame sets the table has the number
  of lines it declares, each a name and ten numbers, and ends at `-9`.
  The unit tests (`legoloco.unit.definitions`) read a definition written
  in the files' shape, and refuse damaged ones.

## Open questions

- How a picture is cut into its frames: most pictures are a row of
  frames of one width, but some aren't (`people\police1\police1.bmp` is
  592 pixels wide for 35 frames).
- What the unnamed ninth number of a frame set does, and what most
  placement keywords mean in play.
- What the game does with a frame set out of range.
