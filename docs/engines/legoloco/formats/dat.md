# LEGO Loco: object definitions (`.dat`)

> **Status:** partial · **Code:** `src/engines/legoloco/formats/object_definition.cpp` · **Checked on:** the CD's resource file, version 1.0.18.2: all 627 `.dat` entries read (November 2026) · **Confidence:** from the code unless marked

## Overview

How the game cuts a picture into the frames counted here, and draws them,
is on [drawing.md](../systems/drawing.md).

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
| `RMBSeq` | signed 16-bit; 0 if not given | what a right click on the piece does (`0x411281`): 0, its next frame set, round to the first; above 0, the piece becomes that resource; below 0 (the CD's 54 `-1`s), nothing *(from the code)* |
| `ClosedFS` | signed 16-bit; -1 if not given | the frame set shown outside its `Shifts`, by the computer's clock: the game checks the hours each time (`0x458940`) and puts the default set back when they come round *(from the code)*; the CD's shops close so |
| `EEReplayDelay` | 32-bit; 0 if not given | seconds before the piece's sequences' surprises can play again; below 0, never again (`0x434dd3`) *(from the code)* |
| `MaxMinifigForResource`, `MaxMobileForResource` | 16-bit, made 5 if larger; 0 if not given | the game compares only their first four letters, `MaxM` (`0x41ec01`), so they're one value: a piece made gets a random 1 to this many visitors (`0x458103`) *(from the code)* |
| `LeisureDestination` | 8-bit, 0 if not given | 1: a place minifigures go to in their free time (`0x42cfe4`, `0x4342a2`) *(inferred)*; the CD's 37 give 1 |
| `MaxEmployees` | 16-bit, made 5 if larger | how many may work there: a minifigure is taken on while fewer do and its resource is among `PossibleEmployees` (`0x433d26`) *(from the code)* |
| `PossibleEmployees`, `PossibleMinifigs` | signed 16-bit numbers, up to five *(seen, not traced)* | resource numbers, -1 for none; an odd number naming a minifigure is made -1 in `PossibleEmployees` (`0x41ecea`). `PossibleMinifigs` also names vehicles and animals: the supermarket's van and milk float, the fire station's engine, a field's cow |
| `Shifts` | four numbers: the opening hour and minute, the closing hour and minute (`09 30 17 30`); read as `%ld %ld %ld %ld` into two times of day | when it's open, by the computer's clock, as `ClosedFS` uses it *(from the code)*; the CD's 96 `00 00 23 59` never close |
| `FreeToRoam` | four 32-bit numbers; empty if not given (`SetRectEmpty`) | a rectangle: left, top, right, bottom, in the picture's pixels (`0 30 16 32`); where its minifigures wander *(guessed)* |
| `ButtonVisible` | 8-bit; 1 if not given | 0: no Toybox button for the piece, unless `Exe\LEGO.INI`'s `[EasterEggs]` lists its resource number (keys `1`, `2` and on, to the first missing; `0x41f7e0`) *(from the code)*. The CD gives 0 to its 58 seasonal pieces only (the desert's, Easter's, Halloween's, winter's and Christmas's trees, ponds and flowers, and roads), which the load events put in place of the usual ones ([ee-ini.md](ee-ini.md)) |
| `InsertSeq`, `MobileSeq`, `TotalVisits` | a sequence, each with an `EasterEgg` line after it, read by `0x41f2b0` | surprises: `TotalVisits` counts the vehicles that pass, keeping the count with the piece in a board ([sav.md](sav.md#pieces)), and plays its surprise when it passes the sequence's count, starting again from 0 (`0x434f21`) *(from the code)* |

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

| # | Read as | Kept at | Field |
|---|---|---|---|
| | word | | name (`default`, `walk-left`, `angry`...) |
| 1 | 16-bit | `+0x00` | start frame |
| 2 | 16-bit | `+0x02` | end frame: at or below the start, the frames run backwards |
| 3 | 16-bit | `+0x04` | delay: ticks of 28 ms a frame; 0 is made 1 |
| 4 | byte | `+0x17` | split frames: the frames go two at a time, even ones only |
| 5 | 32-bit | `+0x08` | restart delay: seconds the last frame is held before the next set |
| 6 | signed 16-bit | `+0x0c` | next frame set, or -1 |
| 7 | signed 16-bit | `+0x0e` | sound, played as the set starts: a resource number, 0 for none |
| 8 | 32-bit | `+0x10` | replay delay: the sound again after a while, in seconds (below) |
| 9 | 16-bit | `+0x14` | the sound's category: which of four volumes it plays at |
| 10 | byte | `+0x16` | flip X: the frames drawn mirrored *(guessed)* |

How the game plays them, from its code:

- **Stepping** (`0x405c40`), once a tick of its 28 ms timer
  ([drawing.md](../systems/drawing.md#animation)): a counter goes up one
  (two with split frames), and the frame is the start plus the counter
  over the delay, or the start less it when the end is at or below the
  start; with split frames, rounded down to even. A set of one frame, or
  at its end, with no next set, stays.
- **At the end:** past its end frame the set holds the end, and the game
  notes the C runtime's `time()`, in seconds, plus the restart delay;
  once that's passed it starts the next set (`0x405d9c`, `0x405db2`).
- **Its sound** (`0x405ab0`), as a set starts: the sound of that number,
  played at the object's place. With no replay delay it's played once,
  so (the last argument 1); with one above 0, again after a random 1 to
  that many seconds, and so on; below 0, after `rand() % (2 − r) + r`
  seconds, a second or so (348 sets have -1).
- **The category** is kept on the voice playing it (`0x40f070`), where
  it picks one of four volumes the sound player keeps (`+0x28` to
  `+0x34`), and where a new sound looks for a voice to take over it's
  compared (`0x413447`). The CD's sets use 1 (scenery, animals, 109
  sets), 2 (people, aeroplanes, the zooms, 257), 3 (the pointers, the
  crashes, 14) and 4 (the Toybox's buttons and the loading screen, 25):
  ambient sounds, voices, effects and the interface's *(inferred, from
  which use them)*; 0 in the rest, taken as 3's volume (`0x40f10d`).

A frame set of one frame whose next set is itself is given -1 instead
(`0x425315`). The game also checks that the frames and the next set are
within the counts, and remembers if one isn't (`0x4251fa`). Asked to
show a set beyond its count, or below 0, a piece keeps the set it has
(`0x405a50`); one being made with such a set isn't made at all
(`0x4059f7`) *(from the code)*.

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
  `loco.exe`'s readers at the addresses given; what a frame set's fields
  do from the code that steps an animation (`0x405c40`) and starts its
  sound (`0x405ab0`), and the values' spread over the CD's 1,595 sets. The fields' names are the
  files' own comments'.
- **How it was checked:** `reluva-tool legoloco dat` reads all 627
  without a failure; in all 551 with frame sets the table has the number
  of lines it declares, each a name and ten numbers, and ends at `-9`.
  The unit tests (`legoloco.unit.definitions`) read a definition written
  in the files' shape, and refuse damaged ones.

## Open questions

- What `entry_exit` and `FreeToRoam` do in play, and how the sequences'
  `EasterEgg` fields are used.
