# Clue: Murder at Boddy Mansion: objects and the mansion

Source: the CD's, the install's and the 2.2 patch's `.o` files and
`clue.lvl`, studied byte by byte (October 2026). Each layout was guessed
from a few records, then checked by a program against all of them; the
field meanings come from which objects have which values, and from
drawing the mansion with them (it comes out whole, every room furnished).
`reluva-tool clue-check` repeats the checks; `clue-level -v` lists the
placements.

Both are `DDRV` archives ([res.md](res.md)). Numbers are little-endian.

## The world

Places are in world units on the floor: x and y, and z up. The camera
looks from beyond +x and +y, from above:

- screen x = x − y
- screen y = (x + y) / 2 − z

So a 16 × 16 square of floor is a 32 × 16 diamond on screen, the size of
`ostiles`' tile. Each thing fills a box in the world, given by its
animation's `ISOA` frames ([res.md](res.md)), and stands at the box's back
corner (its least x and y).

## `.o`: objects

One record an object (1,148 in all the copies). The archive's size for
each is 2 more than the record's own length.

| Offset | Size | |
|---|---|---|
| 0 | 2 | the record's length |
| 2 | 1 | 1 |
| 3 | 32 | the object's full name, `ballroom:Oblcouch_ad`, NUL-terminated; after the NUL, whatever memory held |
| 35 | 1 | its type |
| 36 | 4 | not understood (0, or 1-3 in objects with more than one state, but not their number) |
| 40 | 2 | number of states |
| 42 | 34 each | the states: a 2-byte number and a 32-byte animation name, NUL-terminated |
| | 2 | number of timings |
| | 7 each | the timings |

The types, by the objects that have them:

| Type | Objects |
|---|---|
| 0 | walls, door frames, wall lights (`wall`) |
| 1 | floors (`r_*`) and the black outside (`ostiles`) |
| 3, 5, 9, 12 | furniture and things in the rooms |
| 6 | the six suspects (`character`) |
| 8 | the secret passages |
| 11 | `tilenum`: numbers the level editor shows, not the game |

State 0 is what an object shows normally. A suspect's states 1-16 are
eight `idle` and eight `mult` animations, a direction each, and 19 its
pawn; other objects' 20-24 are what they do when clicked (the bear in the
billiard room has 0, 20 and 21).

A timing is 7 bytes: a 2-byte time in milliseconds, a byte (0), a 2-byte
state, 2 bytes (0). `Oblcouch_ad`'s single one is 1,600 ms and state 19
(`40 06 00 13 00 00 00`): perhaps "after 1.6 s, go to state 19". To be
confirmed by clicking things in the original.

## `clue.lvl`: the mansion

Its entries: `/World.dat`, `/Info.dat` (2 bytes, 0), `/Objects.dat` and
`/Workspace.dat`.

`World.dat`: a 40-byte title (`Unnamed`), a 2-byte count, and that many
object names, each a byte's length and the name. 423 in the game's.

`Objects.dat`: a 2-byte count, and that many placements (2,601), each
starting with its own 2-byte length. The archive's directory gives
`Objects.dat` a size that runs past the end of the file; the placements
end where `Workspace.dat` starts.

A placement:

| Size | |
|---|---|
| 2 | its length |
| 1 | flags: 0x40 always; 0x02 adds the tag; 0x20 adds timings |
| 2 | with 0x02: a tag, not understood (1, 3, 261, 1030 ...) |
| 2 | the object, by its number in `World.dat` |
| 2 | x, in half world units (signed) |
| 2 | y, likewise |
| 2 | not understood: −1 for floors and the outside, 1 for most things, others for a few (doors 120, paintings 57-94) |
| 1 | the layer |
| | with 0x20: a 2-byte count and that many 7-byte timings, as in `.o` |

All 2,601 read to their exact lengths: 1,751 with flags 0x40, 74 with
0x42, 34 with 0x60 and 742 with 0x62.

The layers, by what's on them:

| Layer | |
|---|---|
| 0 | the hall's floor (188 blocks), a few others, the editor's tile numbers |
| 1 | the other floors |
| 3 | walls |
| 4 | the walls nearest the camera: the outer walls on the near sides, and the near walls of the rooms |
| 5 | furniture |
| 6 | the black outside (`ostiles`) |
| 7 | the six suspects, at their starting places |

Without layer 4 the house is a cut-away dollhouse, every room open to
view; with it the near walls hide the rooms.
The readme's options include "Walls" and "Translucency", so the game
presumably hides or fades them; which, and when, is to be seen in the
original.

`Workspace.dat` (32 bytes): four 4-byte numbers (1210, 540, 16068, 720),
two 8s and a double 1.0. Probably the level editor's view: a position, a
size, an 8 × 8 grid and a zoom.

## Drawing order

The original's isn't known yet. Reluva draws the flat things first (the
outside, then the floors by layer, each from the back), then sorts the
others by their boxes: of two whose images overlap, the one wholly beyond
the other along x, or else along y, is in front; boxes overlapping on the
floor go by their centres. This puts every object of the mansion in a
believable place; it's to be compared with screenshots of the original.
