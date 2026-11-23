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
| 4 | 3 | 178, 178, 178 in most, alike in groups; not the picture's colour, and not read by the software program |
| 7 | u8 | animated: how many pictures, from this one on (0x433cb7) |
| 8 | u8 | animated: the game's ticks to a picture; the one shown is (the tick ÷ this) mod the count (0x4351cd) |
| 9 | u8 | 0 |

The program builds its own table of the pictures' corners from these
(0x433bd0), and looks each polygon's up as it draws it (0x435090).

- **Flags:** 0x04, animated (set exactly when bytes 7 and 8 aren't 0,
  34 records); 0x20, see-through, the picture's keyed colour not drawn
  (995 of the 1,054 records with it have the 3dfx picture's green, 6 of
  the 2,483 without); 0x10 and 0x40 set a field of the polygon's draw
  record to 2 or 0 (1 otherwise), whose use wasn't found.
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
| 24 | u16 | bits the AI tests for a gap between the set ones, where it keeps to the middle of its side (0x43a9a0, 0x439b92); what each bit means isn't worked out |
| 26 | u16 | from the point to the road's left edge, in 256ths of the unit |
| 28 | u16 | to its right edge |
| 30 | u8 | what's past each edge: the high 4 bits the left's, the low the right's; 0 nothing, a wall otherwise |
| 31 | u8 | the lanes each way: the high 4 bits to the left, the low to the right |
| 32 | u8 | the left lanes' width, in halves of the unit |
| 33 | u8 | the right lanes' |
| 34 | u16 | 0 |

- **The edges** are where the ground's quads across the road end: within
  0.3 units of a vertex of the point's cross-section ([trk.md](trk.md),
  Extrablocks, 6) at 14,159 of the points on the left, 14,066 on the
  right. A car is kept a quarter of a unit inside an edge with a wall
  (0x456dd0). The walls' kinds, 1 to 7 (1 the most common), matter to
  the crashes (0x457228) and, kind 2, to the camera and the AI
  (0x470fdb, 0x43ee8e); what each is isn't worked out.
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
(`pictureCorners`), and keys out the see-through ones' green.

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

- The road points' bits at 24, and the walls' kinds.
- The texture flags 0x10 and 0x40, and bytes 4 to 6.
