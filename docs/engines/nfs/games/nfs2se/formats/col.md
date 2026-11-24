# Need for Speed II SE: textures, objects and the virtual road (`.col`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/col.cpp` · **Checked on:** all 8 `.col` files on the US CD · **Confidence:** confirmed unless marked

## Overview

`GameData/Tracks/se/trNN.col`, one per track ([trk.md](trk.md)): the
track's texture table, the objects the whole track shares, and its
virtual road. The container is NFS III's ("COLL", version 11, the same
block ids: [col.md](../../nfs3/formats/col.md)); every record in it is
different.

## Layout

### Header

`COLL`, u32 11, u32 the file's size, u32 the number of blocks (2 or 4),
then each block's offset from byte 16. A block: u32 size, u16 id, u16
record count, the records.

### Textures (block 2)

10 bytes each:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | the picture's place in the track's `.qfs` |
| 2 | u8 | flags (below) |
| 3 | u8 | how the picture is turned on its quads (below) |
| 4 | 3 | an RGB colour: 178, 178, 178 in 2,132 of the 2,537, 0, 0, 0 in 160, the others shared by records of one material and often unlike the picture (some pure red); neither program reads it (the corner builder takes byte 7 as the top of a 32-bit word, 0x433bd0): the maker's *(inferred)* |
| 7 | u8 | animated: how many pictures, from this one on (0x433cb7) |
| 8 | u8 | animated: the game's ticks to a picture; the one shown is (the tick ÷ this) mod the count (0x4351cd) |
| 9 | u8 | 0 |

The program builds its own table of the pictures' corners from these
(0x433bd0), and looks each polygon's up as it draws it (0x435090).

- **Flags:** 0x04, animated (set exactly when bytes 7 and 8 aren't 0,
  34 records); 0x20, see-through, the picture's keyed colour not drawn
  (995 of the 1,054 records with it have the 3dfx picture's green, 6 of
  the 2,483 without); 0x10, two-sided: its polygons are never culled
  by the software program, as the cars' two-sided ones (the draw record's word at 10 set to 2,
  0x4351a1; tested at 0x46f836 and the renderer's three other culls):
  fences, and many flat quads; 0x40 sets that word to 0 (1 otherwise,
  0x10 winning), which both programs draw as 1: the 3dfx program culls
  only the cars' polygons, by the word being 3 (`NFS2SEA.EXE` 0x429be5),
  and never a track's. 0x40 is on 168 records, never
  the road's: almost all its quads lie off the drivable road (bits at
  24, below).
- **Turning** (0x433c34, 0x4735a4): 0x10 mirrors the picture left to
  right, 0x40 top to bottom; then 0x02 turns it a quarter, 0x04 a half,
  0x08 three quarters, the quarter turns the other way round once
  mirrored. 0x01 is set whenever neither mirror is, and isn't read.
  Unturned, a quad's vertices 0, 1, 2 and 3 take the picture's top left,
  top right, bottom right and bottom left. The road's surface is mostly
  0x01, its sides 0x09 or 0x03.

A polygon's texture number is a place in this table. The pictures are
counted by place, not name: on `TR06` and `TR08` the 3dfx set's names
repeat. The largest place each table names is its `.qfs`'s last
picture; the software set (`Tracks/pc`) has one entry more, its `!pal`.

### Meshes and placements (blocks 8 and 7)

As a track block's ([trk.md](trk.md), Extrablocks), in 6 of the 8 tracks:
objects that aren't any one block's. Their placements are of types 1 and
3 (12 animated ones).

### The virtual road (block 15)

36 bytes a point, about 8 to a block (14,222 in all):

| Offset | Size | Field |
|---|---|---|
| 0 | 3 × 4 | the point, 16.16 |
| 12, 15, 18 | 3 × 3 | s8 vectors, 125 to 127 long: up (mostly: 13,843 of 14,214), toward the next point (14,208), and across, at right angles to both (14,080), toward the right |
| 21 | 1 | 0 |
| 22 | u16 | the block, rising to the track's last |
| 24 | u16 | where across the road a car can drive (below) |
| 26 | u16 | from the point to the road's left edge, in 256ths of the unit |
| 28 | u16 | to its right edge |
| 30 | u8 | what's past each edge, its wall's kind (below): the high 4 bits the left's, the low the right's |
| 31 | u8 | the lanes each way: the high 4 bits to the left, the low to the right |
| 32 | u8 | the left lanes' width, in halves of the unit |
| 33 | u8 | the right lanes' |
| 34 | u16 | 0 |

- **The edges** are where the ground's quads across the road end: within
  0.3 units of a vertex of the point's cross-section ([trk.md](trk.md),
  Extrablocks, 6) at 14,159 of the points on the left, 14,066 on the
  right.
- **Where a car can drive** (the word at 24): a bit for each slot a
  lane's width across: bit 7 the first right of the point, 6 the next
  out; bit 8 the first left, 9 the next out (0x42a450, 0x42a4f0). Set
  where the road can be driven, clear off it or at a gap, an island or
  a divider *(inferred)*: 0x42a450 turns a car's place into its slot.
  Laid against the point's cross-section, the quads within the set bits
  are road (flags 0 or 0x10; 4 see-through of 76,132), and those out of
  them mostly see-through or 0x40. Most two-way roads with a lane each
  way are 0x3C0. 272 points, in short runs on all but `TR03` and `TR04`,
  have a gap among the set bits. The drivers choose a slot by them,
  10 against a clear one at the car's point and 40 for each clear on
  the way ahead (0x426540), and the lanes (31, below) by 1,000 against
  a slot outside them (0x4276c0): the lanes where a car should drive,
  these where it can. Where there's a gap, some keep to their side
  (0x43a9a0, 0x439bc1).
- **The walls** (the byte at 30). A car is kept a quarter of a unit
  inside an edge whose kind isn't 0 (0x456dd0), bounced (0x457190), and
  the hit given to two tables by kind (0x43e31d): an effect at the
  point of contact (0x4c34dc, 0x460c20) and an event with the hit's
  force (0x4c34fc), the crash's look and sound *(inferred)*:

  | Kind | Points, left / right | Does |
  |---|---|---|
  | 0 | 55 / 130 | no wall: the car isn't held, and the drivers keep no margin (0x438bbf) |
  | 1, 4 | 6,719 / 8,037, 3,580 / 2,602 | a wall, as each other |
  | 2 | 1,691 / 1,731 | a wall, and a tunnel *(inferred)*: a roof 10 units over the road (14 and 24 along stretches of `TR04`, 20 on `TR08`: 0x44c660), a sound turned up inside (0x42f91f), the camera's own setting (0x470fdb), a car put back on the road dropped from half the height (0x43ee8e), and no rain (0x473cc0) |
  | 3, 7 | 798 / 456, 51 / 60 | a wall, with an effect and an event of their own |
  | 5 | 1,060 / 1,020 | a wall, with another of each |
  | 6 | 268 / 186 | no bounce: a car past a certain force is shaken instead (0x457231) |
  | 7 | | for some cars, above a speed of 10, a wreck: the car's damage raised, and past 200 it's wrecked (0x456f8b) |

  Most tracks have one kind for most of their walls: `TR04` 5, `TR02`
  and `TR07` 4, the others 1. Kind 2 is almost always on both sides.
  On `TR06` and `TR07` long stretches take kind 7 whatever the file
  says once cars have hit each other hard (0x440b78). The materials the
  kinds stand for aren't known.
- **The lanes:** the program puts the cars in them by the point, the
  across vector, and the lanes' count and width (0x4366c0, 0x436840).
  They fit inside the edges at all but 3 points; a side without lanes
  has width 0. `TR00`, `TR06` and `TR08` are one way (no lanes to the
  left); most others have a lane each way.

Successive points are 2 to 5 apart.

## In Reluva

Read as `Collision` (`src/engines/nfs/nfs2se/formats/col.cpp`);
`isNfs2Collision` tells it from NFS III's by its 10-byte texture records,
and `reluva-tool nfs survey` reads each with the reader for its game. The
Tracks page turns each quad's picture as its record says
(`pictureCorners`), keys out the see-through ones' green, and runs the
animated ones' pictures at their own rate, at 64 ticks a second as the
hosted game's race clock runs.

## Sources and checks

- **Where the facts come from:** the files, read with survey scripts; no
  published description of NFS II's `.col` is used.
- **How it was checked:** survey scripts over the 8 files (November
  2026): the blocks, the records' sizes, the meshes' indices, the road's
  zero byte, vectors, blocks and spacing, the texture places against the
  `.qfs` files; the reader on all 8; the unit tests (`nfs.unit.nfs2se`).
- **The fields worked out later** (November 2026) from `nfs2sen.exe`'s
  loading, drawing, driving and AI code, disassembled and read, and
  checked over the 8 files as given above. The turning was checked on
  the Tracks page against the hosted game: signs read, and trees stand,
  only as described.

## Open questions

- What the walls' effects and events look and sound like.
