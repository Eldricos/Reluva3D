# Need for Speed II SE: tracks (`.trk`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/trk.cpp` · **Checked on:** all 8 tracks on the US CD · **Confidence:** confirmed unless marked

## Overview

A track's ground and the objects on it, in `GameData/Tracks/se/trNN.trk`
on the CD: `TR00`, `TR02` to `TR08`. The road is cut into **blocks** of
about 32 units, eight to a **superblock**; each block has its ground at
three levels of detail, and extrablocks for what's on and around it. Its
textures are numbers into the `.col` file's table ([col.md](col.md)),
which gives each its picture in the track's `.qfs`.

Points are x, height, y. Positions are 16.16 fixed point; the unit looks
like a metre, as the cars' sizes do ([geo.md](geo.md)) *(inferred)*.

## Layout

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `TRAC` |
| 4 | u32 | 22 in every track |
| 8 | 4 × u32 | not worked out (different on each track) |
| 24 | u32 | number of superblocks |
| 28 | u32 | number of blocks |
| 32 | 4 × superblocks | each superblock's offset in the file |
| | 12 × blocks | each block's reference point: three 16.16 |
| | 2 × blocks | each block's superblock (u16), then 0-2 bytes to 4 |

### Superblocks

A u32 size, a u32 block count (8, fewer in the last), a u32 0, then each
block's offset from the superblock's start.

### Blocks

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | size, and again at 4 |
| 8 | u16 | number of extrablocks |
| 10 | u16 | 8 times the block's number, in every block |
| 12 | u32 | the block's number |
| 16 | 4 × 12 | four 16.16 points: a flat rectangle around the block (47 across on `TR00`'s first) |
| 64 | u32 | where the extrablock table is, from this field |
| 68 | 4 × u16 | vertex counts: those from the next block, then the low, medium and high levels' |
| 76 | 3 × (u16, u16) | each level's two polygon counts, low to high |
| 88 | 6 each | the vertices |
| | 8 each | the polygons, then 0 or 2 bytes to 4 |
| | 4 × extrablocks | each extrablock's offset from the block's start |

- **Vertices** are three s16, in 256ths of the unit. The first ones,
  as many as the first count, are from the **next** block's reference
  point (the first block's, after the last), and lie on that block's own
  vertices (23,667 of 23,878 within 2 of one): they join the blocks. The
  others are from this block's. A level uses those from the next block
  and its own count of them; the levels share them, the high one using
  all.
- **Polygons**, 8 bytes, always quads: u16 texture, a u16 not worked out
  (`0xFFFF` on many), four vertex indices. Each level has its first
  count's polygons, then its second count's; the second counts are 0, or
  small (2, 4, 8 most often) in 539 of the 1,780 blocks. What sets the second group
  apart isn't known.

The counts at 76 are pairs of u16, not u32: in 539 blocks the second of
a pair isn't 0, and only adding both puts the extrablock table after the
polygons, as every block has it.

### Extrablocks

Each: a u32 size, a u16 id, a u16 record count, the records. Seen:

| Id | Blocks with one | Records |
|---|---|---|
| 4 | all | u16 block numbers, the first the block's own in every block |
| 5 | all | 2 bytes for each of the high level's first-count polygons (as many in every block) |
| 6 | all | 8 bytes, not worked out |
| 7, 18, 19 | 1,047, 591, 26 | placements of meshes (below) |
| 8 | 1,275 | meshes (below) |
| 9 | 862 | 4 bytes, not worked out |
| 13 | all | 12 bytes: two s16 vectors about 32,767 long (5,322 of the 5,338 checked) |
| 10, 11, 17 | 13, 383, 17 | not worked out |

- **Meshes** (8): a u32 size, u16 vertex count, u16 polygon count, the
  vertices (three s16, 256ths of the unit, from where the mesh is
  placed), the polygons as the ground's, 0 or 2 bytes to 4.
- **Placements** (7, 18, 19): u16 size, u8 type, u8 mesh index. Type 1
  (16 bytes): a 16.16 point. Type 4 (20 bytes): the point, then a u32 0
  or 1, not worked out. Type 3: a u16 key count, a u16 not worked out,
  then 20 bytes a key: a 16.16 point and four s16 not worked out.

## In Reluva

Read as `Track` (`src/engines/nfs/nfs2se/formats/trk.cpp`): the blocks,
their vertices made absolute, the levels' polygons, the meshes and
placements, and every extrablock as it is. `reluva-tool nfs survey`
reads all 8 tracks; `reluva-tool nfs trk-obj` writes one as an OBJ.

The data viewer's Tracks page (`src/engines/nfs/nfs2se/pages/track_page.cpp`)
draws a track at a level, its objects placed (an animated one where its
animation starts), with the 3dfx program's pictures, from points of the
virtual road ([col.md](col.md)). Each quad has its picture laid across
it whole, its corners taking the picture's top left, top right, bottom
right and bottom left in turn, as the files give no texture
coordinates; which way round the game lays them isn't checked
*(guessed)*. Both faces are drawn, the background is the 3dfx sky's
colour toward the sun, and there's no fog or lighting.

## Sources and checks

- **Where the facts come from:** the tracks themselves, read with survey
  scripts; no published description of NFS II's tracks is used.
- **How it was checked:** survey scripts over the 8 tracks (November
  2026): every size, offset and count consistent, every polygon's
  vertices inside its level, the first vertices against the next
  block's; then the reader on all 8; the unit tests (`nfs.unit.nfs2se`).

## Open questions

- The header's words at 8 to 20, and the polygons' second word.
- What the second polygon count of each level counts.
- Extrablocks 10, 11 and 17, and type 4 placements' last word.
