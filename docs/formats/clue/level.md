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

The placements' x and y are in these units. That was measured on a
recording of the original (the longplay listed in
[../../engines/clue.md](../../engines/clue.md)): the grand piano, its
music stand and the harp, found in a frame by matching their images, are
(152, −9) and (102, −18) pixels apart on screen where the level has them
(67, −85) and (33, −69) apart, as x − y and (x + y) / 2 give. A first
reading had them in half units; that drew the house at half its size, its
floors overlapping fourfold (hiding the hall's inlaid "Clue"), which
looked believable until compared.

## `.o`: objects

One record an object (1,148 in all the copies). The archive's size for
each is 2 more than the record's own length.

| Offset | Size | |
|---|---|---|
| 0 | 2 | the record's length |
| 2 | 1 | 1 |
| 3 | 32 | the object's full name, `ballroom:Oblcouch_ad`, NUL-terminated; after the NUL, whatever memory held |
| 35 | 1 | its primary type: `Clue.exe` makes its object by it (below) |
| 36 | 1 | its secondary type (0, or 1 for the wall lamps, 2, 3) |
| 37 | 2 | the state it starts in (Clue.exe makes its object in it, 0x446064; a saved game notes those off it, 0x446766): 0 in all |
| 39 | 1 | the frame it starts on: 0 in all |
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
| 11 | `tilenum`: the numbers the game shows on the squares of a move being planned (1, 2, 3 ...); the level's 9 stand together out of the way, as templates it seems |

State 0 is what an object shows normally. A suspect's states 1-16 are
eight `idle` and eight `mult` animations, a direction each, and 19 its
pawn; other objects' 20-24 are what they do when clicked (the bear in the
billiard room has 0, 20 and 21).

The "timings" are keys: properties by number. Each is 7 bytes: a 2-byte
key, a type byte (0 a whole number; Clue.exe's text form has 1 a decimal
and 2 a string too, 0x448BC0) and a 4-byte value. (Reluva's code still
calls them timings.) Clue.exe looks one up on the placement first, then
the object (0x4280B0, 0x428100). The keys the game has:

| Key | On | Its value | Read at |
|---|---|---|---|
| 1000 | floors, fireplaces, the fountain, the passages | its room: 1-9 the rooms, 10 the hall's corridors; the house keeps a list of each room's objects by it | 0x4290AB |
| 1100 | 17 pieces of the rooms' floors | a click on it from the room it is in is refused (`nogo.wav`) | 0x423B63 |
| 1200 | the secret passages | its room, when it has no 1000 | 0x4290E2 |
| 1300 | the secret passages | the side a suspect steps out on, coming through it: 32 units that way and facing it | 0x425CE1 |
| 1400 | the secret passages | the room it leads to (its passage is that room's first object) | 0x42987B |
| 1500, 1600, 1700 | hot spots | the sound of reaction 20, 21, 22 (from the sound table's 69th entry) | 0x42A3D8 |
| 1800 | the hall's `r_logo` | not read by Clue.exe | |
| 1900 | the fires, the fountain | a sound heard near it: 0 the fire's, 1 the fountain's | 0x42A93A |
| 2000 | the stairway | never drawn see-through | 0x42A973 |
| 2100 | the credits painting's two portraits | reacts to the pointer passing over, not a click | 0x42A05C |

`Oblcouch_ad`'s single key, read once as "after 1.6 s, go to state 19",
is key 1600, value 19: the sound of its bad reaction.

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
| 2 | with 0x02: its primary and secondary types (a byte each), in place of its object's: 3 furniture, 0x103 (259) furniture of type 1 (the wall lamps), 5 a hot spot ... |
| 2 | the object, by its number in `World.dat` |
| 2 | x, in world units (signed) |
| 2 | y, likewise |
| 2 | the height it stands at above the floor: −1 for floors and the outside, 1 for most things, more for what hangs on a wall or tops a door (the doors' lintels 119-121 on their 161-high frames, paintings 57-94, wall lamps 88-97); see below |
| 1 | the layer |
| | with 0x20: a 2-byte count and that many 7-byte timings, as in `.o` |

All 2,601 read to their exact lengths: 1,751 with flags 0x40, 74 with
0x42, 34 with 0x60 and 742 with 0x62.

The layers, by what's on them:

| Layer | |
|---|---|
| 0 | the hall's floor (188 blocks), a few others, the tile numbers |
| 1 | the other floors |
| 3 | walls |
| 4 | the walls nearest the camera: the outer walls on the near sides, and the near walls of the rooms |
| 5 | furniture |
| 6 | the black outside (`ostiles`) |
| 7 | the six suspects, at their starting places |

Without layer 4 the house is a cut-away dollhouse, every room open to
view; with it the near walls hide the rooms. The original draws it solid
and fades ("Translucency" on) or leaves out (off) only what stands in
front of the suspect whose turn it is ([clue.md](../../engines/clue.md)).

The height (the field after the position) lifts a thing off the floor:
`Clue.exe` reads x, y and it as the object's position (0x4465D8) and
places it on the screen at x − y and (x + y) / 2 − height (0x4461C5).
Without it the doors' lintels lie on the floor at their doors and the
wall lamps and pictures at the walls' feet. Most things have 1, the
floors −1.

`Clue.exe` makes each placed object one of its classes by its primary
type, the tag's if it has one (0x4190B0, read through 0x4439A0): 0 Wall,
1 Floor, 3 Passive (furniture), 4 Interactive and 5 InteractiveSeeded
(hot spots), 6 Player (the suspects), 8 SecretPassage, 11 Number (the
move's numbers), 12 StormObject (the drapes); anything else its base
class. (The names are its own, from its run-time class records; its
message "Base object [%s] has primary type %d and secondary type %d"
names the two types.)

`Workspace.dat` (32 bytes): four 4-byte numbers (1210, 540, 16068, 720),
two 8s and a double 1.0. Probably the level editor's view: a position, a
size, an 8 × 8 grid and a zoom.

## Drawing order and the original's look

What stands in front of the suspect whose turn it is is drawn
see-through, and the house dark but near it ("Lighting Effects"): see
[clue.md](../../engines/clue.md).

How `Clue.exe` orders what it draws (read from its code): one list,
each object put in before the first with a greater anchor y, then x,
then height (0x443C20); then, each frame, a pass down it (0x41CBD0):
any object later in the list whose screen rectangle touches an earlier
one's and whose box is behind it (starting before the earlier one's box
ends along x, y and height alike) is moved before it, a guard keeping
two from trading places for ever. Reluva draws the flat things first
(the outside, then the floors by layer, each from the back: they are
below everything, as in its order), then the rest in that order, its
pass run until nothing moves; suspects walking go in by the same test.
Views of the ballroom and the billiard room drawn this way match the
recordings' frames, as the order Reluva used before this was read did.
