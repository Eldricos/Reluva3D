# Clue: Murder at Boddy Mansion: the board

> **Status:** decoded · **Code:** `src/engines/clue/game/board.h` · **Checked on:** `clue.lvl`, against the classic board, the game's own overhead map and `Clue.exe` 2.2's code · **Confidence:** confirmed unless marked

## Overview

The board the game is played on, its squares, doors, starting squares and
passages, as read from `clue.lvl` ([level.md](level.md)), the mansion's
floor; and how suspects stand in rooms and move on it.

## Layout

### Squares

The floors are 64 × 64 world units, their corners at 16 + 64 n in x and
y: a square's column and row are ⌊(x − 16) / 64⌋ and ⌊(y − 16) / 64⌋. The
suspects stand 24 units in from a square's corner, as `clue.lvl` places
them.

- **Corridors**: the squares of `r_hwfloor` on layer 0, 188 of them.
  (The stairway's middle has `r_hwfloor` on layer 1 as well; it isn't
  played on.)
- **Rooms**: every other floor square (`r_...` on layers 0 and 1), joined
  where they touch side to side: ten parts, the nine rooms and the
  stairway's middle. A part is the room whose furniture stands in it
  most (`study:...` on layer 5 and so on), each room to one part, the
  strongest claim first: the dining room has some of the billiard room's
  furniture, which a plain vote gets wrong. The part with no furniture is
  the middle.

| Room | Squares | Columns | Rows | Doors | Secret passage to |
|---|---|---|---|---|---|
| Hall | 48 | 22-27 | 10-17 | 3 | |
| Lounge | 80 | 30-39 | 9-16 | 1 | Conservatory |
| Dining Room | 67 | 29-38 | 20-26 | 2 | |
| Kitchen | 72 | 31-38 | 29-37 | 1 | Study |
| Ballroom | 64 | 21-28 | 28-37 | 4 | |
| Conservatory | 66 | 9-18 | 30-37 | 1 | Lounge |
| Billiard Room | 40 | 11-18 | 23-27 | 2 | |
| Library | 46 | 10-19 | 17-21 | 2 | |
| Study | 60 | 10-19 | 9-14 | 1 | Kitchen |

The doors are those of the classic board.

### Doors

Each door has a lintel, a wall object with `drtop` in its name
(`wall:Ohwdrtop_na`, `wall:Obldrtopw`, 17 of them). It lies on the line
between a room's square and a corridor's: `..._n...` lintels along x
(between two rows), `..._w...` along y (between two columns); those with
neither (`wall:Oktdrtop`, `wall:Obrdrtop`) either way. The door is the
pair of squares on either side of the line, at the lintel's column (or
row), with a room on one side and a corridor on the other.

### Starting squares and passages

The suspects start where `clue.lvl` stands them (`character:Oscarlet`
and the others, layer 7): Miss Scarlet (29, 11), Colonel Mustard (36, 18),
Mrs. White (27, 35), Mr. Green (22, 35), Mrs. Peacock (13, 29), Professor
Plum (13, 16).

The rooms with a `secretpass:` object (the kitchen, lounge, study and
conservatory) have the passages; each leads to the one of them furthest
away, in the opposite corner.

### What `Clue.exe` keeps of it

Read from `Clue.exe` 2.2's code (November 2026), which confirms all of the
above. Its board is the floors' timings (each placement's, else its
object's; time and a 4-byte value, read at 0x42AD90): time 1000 gives
the square's zone, 1 to 9 the rooms (hall, lounge, dining room, kitchen,
ballroom, conservatory, billiard room, library, study, as above), 10
the corridors, 0 the stairway's middle: 188 corridor squares and each
room's count as in the table above. Time 1100 marks the 17 squares just
inside the doors (`Ot_..dr_...`), its value the way out (0 north, 2
east, 4 south, 6 west): the same doors as the lintels give. The secret
passages (`secretpass:` objects) have 1000 their room's zone, 1400 the
zone they lead to (lounge and conservatory, kitchen and study, as
above) and 1300 the way they face.

## Behaviour

### In a room

Where a suspect stands in a room isn't kept anywhere: `Clue.exe` looks
for a place (0x41C480) on its walk grid, cells 32 units square, those
under furniture (but the wall lamps), hot spots, secret passages and
walls on the floor blocked (0x427FC0), and those other suspects stand
on taken. From a point, it tries the points 32 apart within 128 of it,
then 160 ... 288, each time from the largest y down and, in each row,
from the largest x down: the first whose cell is free and whose square
(the one at the cell's corner plus 32) is in the room and not a door's
is where the suspect goes. The point is the middle of the door's
square, for a suspect walking in; 32 units in front of the room's
passage, for one coming through it (0x425CC0); where the suggester
stands, for one brought in by a suggestion (0x41BEA0). So suspects
gather at the rooms' front edges, nearest the camera, as recordings
show: entering by each door, the first places are

| Room | Door's square | First places (their squares) |
|---|---|---|
| Hall | (24, 17), (25, 17) | (26, 17), (27, 17) |
| Lounge | (30, 16) | (32, 16), (31, 16) |
| Dining Room | (30, 20), (29, 23) | (32, 22), (31, 25) |
| Kitchen | (32, 29) | (34, 31), (33, 31) |
| Ballroom | four | (23, 32), (24, 30), (27, 30), (27, 32) |
| Conservatory | (17, 30) | (18, 32), (17, 32) |
| Billiard Room | (14, 23), (18, 26) | (13, 25), (18, 27), (17, 27) |
| Library | (16, 21), (19, 19) | (18, 21), (17, 21) |
| Study | (19, 14) | (18, 14), (17, 14) |

Two recorded games played on the board from above (the VM recording,
5:00-7:40, and "The Classic", C 33:00-56:00) agree: the squares pawns
rested on in rooms were the hall's (27, 17) (26, 17), the lounge's
(32, 16), the conservatory's (18, 32), the billiard room's (18, 27)
(17, 27), the library's (18, 21) (17, 21), and in the dining room and
ballroom their neighbours. (Reluva placed suspects on those recorded
squares before this was read; `Board::standing()` keeps a list of the
kind for the rules' own use.)

### Moving

As the game's texts say (`Character Movement.txt` and the others): steps
go side to side, never diagonally; a room is one step from the square
before its door, and its doors' squares one step from it; no square
twice in a move, none another suspect stands on; a room ends the move,
and the one left can't be entered again in the same turn.

## Sources and checks

- **Where the facts come from:** `clue.lvl` ([level.md](level.md)), read for the board the game is
  played on (October 2026), checked against the classic board and the game's
  own overhead map (`UI:Aoverhead`). No file holds the board as such: it is
  the mansion's floor. `reluva-tool clue board <clue.lvl>` draws what Reluva
  reads from it.
