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
| 2 | u16 | not worked out |
| 4 | 3 | not worked out |
| 7 | 3 | not worked out |

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
| 12, 15, 18 | 3 × 3 | s8 vectors, 125 to 127 long: up (mostly: 13,843 of 14,214), toward the next point (14,208), and across, at right angles to both (14,080) |
| 21 | 1 | 0 |
| 22 | u16 | the block, rising to the track's last |
| 24 to 30 | 4 × u16 | not worked out; 26 and 28 are non-zero in every point |
| 32 | u32 | not worked out |

Successive points are 2 to 5 apart.

## In Reluva

Read as `Collision` (`src/engines/nfs/nfs2se/formats/col.cpp`);
`isNfs2Collision` tells it from NFS III's by its 10-byte texture records,
and `reluva-tool nfs survey` reads each with the reader for its game. Not
drawn yet.

## Sources and checks

- **Where the facts come from:** the files, read with survey scripts; no
  published description of NFS II's `.col` is used.
- **How it was checked:** survey scripts over the 8 files (November
  2026): the blocks, the records' sizes, the meshes' indices, the road's
  zero byte, vectors, blocks and spacing, the texture places against the
  `.qfs` files; the reader on all 8; the unit tests (`nfs.unit.nfs2se`).

## Open questions

- The road points' words at 24 to 32.
- The texture records' bytes after the picture.
