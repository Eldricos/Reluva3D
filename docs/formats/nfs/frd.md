# Need for Speed III: `.frd` track geometry

Status: **implemented** (`src/engines/nfs/frd.cpp`). All 9 retail tracks
read completely, with every byte accounted for (`reluva-tool frd-check`),
and draw correctly on the Tracks page.

## How it was worked out

October 2026, with survey scripts over the nine tracks:

- **Starting point.** The overall order (track blocks, then polygons,
  then objects, then textures) and several field names follow the NFS
  modding community's descriptions of NFS III's FRD, as T3ED used them.
- **Track blocks.** Each field was found on the data:
  - the block count is one less than the blocks: block 0's neighbour list
    names block 119 of tr00's "119";
  - the record sizes come from a least-squares fit of each block's size
    over its eight counts, on all 1,583 blocks with a following block:
    8, 8, 12, 20, 20, 16 and 16 bytes, exactly.
- **Polygon section.**
  - An empty list has no repeated count: block 1's first list is `0`,
    followed by the next list's count.
  - There are four object layers: block 0's trailing `0` and `10, 3` fit
    only as its layers 1-3.
  - Checks on every block of every track:
    - each list's count matches its repeat;
    - each layer's total matches its objects' polygons;
    - list 4's count equals the block's `nPolygons`, in all 1,592 blocks.
- **Placed objects.** Type 3's key count is the short at +32: 17 keys of a
  spinning object on Hometown, after which the usual vertices and
  polygons follow.
- **Texture table.** The bytes left after the objects divide exactly by
  47 on all nine tracks.
- **By eye.** The tracks drawn on the Tracks page:
  - roads, lane lines, fences, poles, trees and buildings are where they
    belong;
  - billboard text reads the right way round (with z mirrored, as
    models are, [fce.md](fce.md)).

All values are little-endian. Floats are 32-bit IEEE.

## Layout

| Part | Contents |
|---|---|
| Header | 28 bytes: `35 01 EF BE`, then three Windows FILETIMEs (1998); u32 number of blocks − 1 |
| Track blocks | one per block, below |
| Polygons | per block: seven lists and four object layers |
| Placed objects | 4 × blocks + 1 groups |
| Textures | u32 count, then 47-byte entries, to the end of the file |

### Track block

| Offset | Size | Meaning |
|---|---|---|
| 0 | 3 floats | centre |
| 12 | 4 × 3 floats | the block's outline on the ground |
| 60 | 6 × u32 | vertices; high-res, low-res and medium-res counts; a duplicate of the vertex count; object vertices |
| 84 | 12 × n | vertices |
| | 4 × n | colours, `0xAARRGGBB` (baked lighting: white, or blue-grey in shade; alpha 255 in all but 98 of 414,641) |
| | 300 × (s16, s16) | neighbours: block numbers (−1: none), with a second value always −1 so far |
| | 8 × u32 | start position, then the counts of the next seven record lists |
| | 8 each | positions |
| | 8 each | per road polygon (as many as list 4) |
| | 12 each | virtual road vectors |
| | 20 each | references to placed objects |
| | 20 each | polygon objects |
| | 16 each | sounds |
| | 16 each | lights |

The vertices come in order:

1. the objects' vertices (the object count);
2. the road's (the high-res count);
3. the rest, which only the lane list uses.

The records after the counts are kept as stored. Lights and sounds are
also decoded (October 2026, from a survey of all nine tracks):

- **Lights** (16 bytes): a position, 3 × s32 in 16.16 fixed point, then
  a u32 type. The positions are on the tracks where lights would be:
  `trk000`'s type 1 lights sit on its town centre's street lamps. The
  types run 0 to 15 and differ by track (`trk000` 1 and 7, `trk004`
  13, `trk007` 3 and 13...). Every type is drawn with `glw0` of
  `Render/pc/sfx.fsh`; the type (& 15) picks a colour, a size and a
  blink from a table in nfs3.exe (`0x55e3dc`, 12 bytes each, used by
  `0x491190`). A blinking light is hidden while
  ((t + phase) >> shift) & 1, t the tick count (64 a second):

  | Type | Colour (ARGB) | Size | Blink: shift, phase |
  |---|---|---|---|
  | 0, 14, 15 | `FFDEEAEB` | 5 | |
  | 1 | `B9FFFFFF` | 4.5 | |
  | 2 | `FFFFFFD2` | 5 | |
  | 3 | `FF80E5F0` | 4.5 | |
  | 4 | `FFD9C45E` | 5 | |
  | 5 | `FFDF1616` | 5 | 6, 0 |
  | 6 | `FFDF1616` | 5 | 5, 27 |
  | 7 | `FFFF0000` | 3.125 | 6, 0 |
  | 8 | `FFA3B1BE` | 3.75 | |
  | 9 | `FFDF1616` | 3.125 | |
  | 10 | `BADF1616` | 2.5 | |
  | 11 | `FF1E95E3` | 2.5 | |
  | 12 | `FF1E95E3` | 3.125 | 6, 0 |
  | 13 | `FFE0E027` | 3.75 | |
- **Sounds** (16 bytes): a position as above, a u16 slot of the track's
  placed-sound bank (`Audio/SFX/tr3dNN.bnk`, or `...n`, `...w`, `...nw`
  by sky variant) and a u16 kind (`0x41d210`): 0 a loop, the first
  within reach playing on one channel; 1 a one-shot, fired at random
  moments on another. The volume falls with the square of the
  distance. The game passes only a block's first sound record, so the
  second of the one block that has two is never heard. The slots fit
  the banks: `trk000`'s go up to 7 of 32 slots, `trk005`'s to 20 of
  21. `trk008` uses slot 12 of a 12-slot bank, so not every one can be
  heard.

The rest (positions, the road polygons' records, the virtual road,
object references and polygon objects) is still kept as stored. Object
references begin with a 16.16 position near their block. The positions
are one record per slice of the track (8 per block; the block's first
slice is its start position): a u16 first polygon and a u8 count, the
run of high-detail road polygons (list 4) across that slice, then a
u8 and −1. The game finds which road polygon a car is on by starting
from the run's middle one and walking sideways (`0x41e990`). The byte
at 3 is the music zone, 4-9: the game
sends it to the Pathfinder map as an event when the car nears a slice
where it changes ([mus.md](mus.md), The events the game sends).

The Tracks page shows each light as a `glw0` glow facing the camera, in
its type's colour and size and blinking as the table above has it, and each sound as a cyan diamond. It plays the sounds as the game does:
only each block's first, louder by the game's inverse square and panned
by side; the nearest loop within 300 units (the game takes the first of
the blocks seen) and one-shots when the timer picks a frame, counting
the page's frames as the game counts its own (`0x434a73`). A loop plays
looped whatever its bank says. The overlay names the nearest light or sound; `M` hides them all.

### Polygons

A polygon is 14 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 × u16 | vertices, of its block (or its object). A quad; a triangle repeats one. |
| 8 | u16 | an entry of the texture table |
| 10 | u16 | the animation's first texture entry (as at 8 when animated; 0 except on lane markings, where nothing found reads it) |
| 12 | u8 | flags: `0x10` two-sided, `0x04` animated, `0x01` (none in the files); `0x20` and `0x40` are common but nothing found reads them |
| 13 | u8 | the animation: delay in ticks in the top 5 bits, frames in the low 3 (`0xF9`: 31 ticks, 1 frame, still) |

An animated polygon's texture entry is the one at 10, plus
(t / delay) mod frames, t the game's tick count (64 a second;
`0x41b52b`). The track's animated polygons have 28, 84 or 86: 3 or 10
ticks, 4-6 frames.

Each block has seven lists. Each list is a u32 count, then (if it isn't
0) the count again and the polygons:

| List | Contents |
|---|---|
| 0, 2, 4 | the road at low, medium and high detail |
| 1, 3, 5 | roadside polygons at the same levels, on three tracks only (`trk000`, `trk004`, `trk008`) |
| 6 | lane markings: all their textures are lane textures |

Then come four object layers. Each layer is a u32 polygon total, then
(if it isn't 0):

- a u32 number of objects;
- for each object, a u32 type. Type 1 is followed by a u32 count and the
  polygons; type 4 has nothing more.

A type 3 entry appears once on each of two tracks, also without data. These
objects use the block's vertices.

### Placed objects

Each group is a u32 count, then the objects. Each object starts with
3 × u32: type, a number, and a heading. The number is an entry of the
block's object references (20 bytes each; `0x41cac6`): the game keeps a
state for each (standing, knocked down, or moving), and the reference's
byte 19 = 2 makes it one a car can hit (`0x41d403`). The heading's low
byte, × 256 / 65536 of a turn, sets the turn about the vertical of an
object knocked over (`0x41d5a0`): −32, ±64 and −128 are 45°, 90° and
180°. Then, by type:

- **Type 4** (still, 2,046 of them):
  - 3 floats, the reference point;
  - a u32.
- **Type 3** (moving, 20 of them):
  - 12 bytes the game doesn't read (room for a reference point, which
    type 4 uses; here leftovers);
  - 8 bytes: a u32 size of the data, then a u16 size again, a u8 3 and
    the object's index among the block's moving ones (0, 1, 2...);
  - at +32, a u16 number of keys;
  - a u16: the key delay, in game ticks (64 a second). Hometown's
    17-key spinner (delay 6) turns once in 1.5 s, and its 141-key loop
    (delay 25) takes 55 s.
  - the keys, 20 bytes each: 3 × s32 position in 16.16 fixed point and
    4 × s16 rotation in 2.14 fixed point, a quaternion by its unit length
    (`0x4000` = 1).

Then, for both types:

- u32 number of vertices;
- the vertices, relative to the reference point (type 4) or to the key
  (type 3);
- their colours;
- u32 number of polygons;
- the polygons.

The keys are world positions. Hometown spans x −758 to 911 and z −779
to 661: its spinning object sits at (−563, 2, 442), and its other object
flies a loop 52 m up. The same loop is in the `.col` file
([col.md](col.md)).

**How the game moves them** (`nfs3.exe`, `0x40f4b0`; worked out from
its code). From the game's tick count t (64 a second, `0x7d3684`):

- the delay d is the key delay if 1 to 400, else 6;
- the key is (t / d) mod (keys − 1): the last key is the first again
  (Hometown's loop: they differ by 1/65536 in one coordinate);
- it moves from that key towards the next by (t mod d) / d, the
  positions straight and the quaternions blended (`0x4b70f0`);
- the quaternion's four values are taken in the file's order, each
  × 4 / 65536, and made a matrix by `0x4b7590`. Run on sample values,
  that gives the matrix Reluva's `keyMatrix` builds (x, y, z, w, the
  game's vectors being rows), so the rotation sense is the game's.

The Tracks page moves them so: 64 ticks a second, keys − 1 keys round,
the same blend (normalised rather than spherical, which differs little
between keys this close).

### Textures (47 bytes)

| Offset | Size | Meaning |
|---|---|---|
| 0 | u16 | width |
| 2 | u16 | height |
| 4 | u32 | room for the picture's handle: the game writes it at load (`0x419b4d`); the files hold leftovers |
| 8 | 8 floats | u, v of polygon corners 0 to 3 |
| 40 | u8 | flags: `0x02` a see-through surface, drawn half grey and half clear and sorted by depth (`0x41b596`); `0x04` left out of a second pass over the road (`0x41ae90`, not identified). Values 0, 4, 6 |
| 41 | 3 | not read (byte 43 often `0xFF`) |
| 44 | u8 | 1: a lane texture |
| 45 | u16 | the picture's number |

The picture is the shape named by its number (`0042`) in the track's
sheet, `trNN0.qfs` (or `trNN1.qfs`, an alternative set that differs in a
few pictures). For a lane texture it's `linN` in
`GameData/Render/pc/sfx.fsh`.

Every polygon's texture is in the table, and every picture they use is
in its sheet. Some entries name pictures past the sheet's end, but no
polygon uses those.

## Tools

- `reluva-tool frd-check <file.frd>...` reads tracks and counts what's in
  them.
- `reluva-tool frd-obj <file.frd> <out.obj> [detail]` writes the road and
  objects as OBJ, mirrored as Reluva draws them.
