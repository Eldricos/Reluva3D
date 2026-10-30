# Clue: Murder at Boddy Mansion: the board

Source: `clue.lvl` ([level.md](level.md)), read for the board the game is
played on (October 2026), checked against the classic board and the game's
own overhead map (`UI:Aoverhead`). No file holds the board as such: it is
the mansion's floor. `reluva-tool clue-board <clue.lvl>` draws what Reluva
reads from it.

## Squares

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

## Doors

Each door has a lintel, a wall object with `drtop` in its name
(`wall:Ohwdrtop_na`, `wall:Obldrtopw`, 17 of them). It lies on the line
between a room's square and a corridor's: `..._n...` lintels along x
(between two rows), `..._w...` along y (between two columns); those with
neither (`wall:Oktdrtop`, `wall:Obrdrtop`) either way. The door is the
pair of squares on either side of the line, at the lintel's column (or
row), with a room on one side and a corridor on the other.

## Starting squares and passages

The suspects start where `clue.lvl` stands them (`character:Oscarlet`
and the others, layer 7): Miss Scarlet (29, 11), Colonel Mustard (36, 18),
Mrs. White (27, 35), Mr. Green (22, 35), Mrs. Peacock (13, 29), Professor
Plum (13, 16).

The rooms with a `secretpass:` object (the kitchen, lounge, study and
conservatory) have the passages; each leads to the one of them furthest
away, in the opposite corner.

## In a room

Where suspects stand in a room isn't in the level. Reluva puts them on
the room's squares nearest its middle, those without furniture (no
layer-5 object placed in them) first, in the suspects' order.

## Moving

As the game's texts say (`Character Movement.txt` and the others): steps
go side to side, never diagonally; a room is one step from the square
before its door, and its doors' squares one step from it; no square
twice in a move, none another suspect stands on; a room ends the move,
and the one left can't be entered again in the same turn.
