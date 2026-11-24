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
| 8 | u32 | the largest superblock's size |
| 12 | u32 | the largest block's size |
| 16 | u32 | the most that a block's neighbourhood (extrablock 4) adds up to |
| 20 | u32 | 19,800 to 38,800 more than 16; neither program reads it, nor 16 (the header is kept at 0x4ffeac, these two never read): another buffer's size *(guessed)* |
| 24 | u32 | number of superblocks |
| 28 | u32 | number of blocks |
| 32 | 4 × superblocks | each superblock's offset in the file |
| | 12 × blocks | each block's reference point: three 16.16 |
| | 2 × blocks | each block's superblock (u16), then 0-2 bytes to 4 |

The words at 8 to 16 are exact on all 8 tracks: room to keep in memory,
for a loader that streams the track. The program reads the track whole
(0x46d7c0), checks that the version is 22, and throws away what it reads
of 8 and 12 (0x436013); 16 and 20 it doesn't read.

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
- **Polygons**, 8 bytes, always quads: u16 texture, a u16 of shades,
  four vertex indices. Each level has its first count's polygons, then
  its second count's; the second counts are 0, or small (2, 4, 8 most
  often) in 539 of the 1,780 blocks.
- **The second group** is the see-through upright things: fences, trees,
  signs. 10,164 of its 10,788 polygons have see-through pictures (9% of
  the first group's), and 5,861 of the high level's 6,065 stand upright.
  The program draws the first group as the ground, and the second in a
  list of its own with the block's objects (0x434690, 0x434c48).
- **Shades:** above the low 4 bits, four 3-bit values, one for each of
  vertices 1, 0, 3 and 2 from the low end, the order the program draws a
  quad's vertices in (0x435090). A vertex gets the same value from every
  high-level quad that has it in 96.6% of vertices; no other order gets
  above 78%. 7 is the most common, all four 7 being `0xFFFF`: a shade or
  light baked by corner. The software program copies them into its draw
  list (0x4351b8) but never draws with them. The 3dfx program makes each
  a grey for its vertex, 24 × the value + 50, 50 to 218 (`NFS2SEA.EXE`
  0x429cea): 7 is the picture as it is, 0 the darkest, the shadows; an
  animated picture's are all taken as 7 (0x4468d7). A car takes the mean
  of the shades under it, and in the 3dfx program is drawn that much
  darker (0x45a490, 0x46dcb0; [A] 0x47e720). The low 4 bits are `0xF`
  (139,230 of the high level's), 1 (33,347) or 2 (3,930, all four values
  0 with it), on the same pictures in the same blocks; neither program
  reads them: the lighting tool's mark *(guessed)*.

The counts at 76 are pairs of u16, not u32: in 539 blocks the second of
a pair isn't 0, and only adding both puts the extrablock table after the
polygons, as every block has it.

### Extrablocks

Each: a u32 size, a u16 id, a u16 record count, the records. Seen:

| Id | Blocks with one | Records |
|---|---|---|
| 4 | all | u16 block numbers, the first the block's own in every block: its neighbourhood |
| 5 | all | 2 bytes for each of the high level's first-count polygons (as many in every block) |
| 6 | all | the road's cross-sections (below) |
| 7, 18, 19 | 1,047, 591, 26 | placements of meshes (below) |
| 8 | 1,275 | meshes (below) |
| 9 | 862 | painted lines on the road (below) |
| 10 | 13 | 16 bytes: a 16.16 point, 8 to 68 units off the road, and a u32 of 1, 2 or 4 (`TR04` one, `TR05` five, `TR08` ten); neither program reads it beyond keeping where it is (0x435a8a): effects' places, by what's there *(inferred)*: `TR08`'s 1s on the axes of its chimneys (`BOIL`), a little below their tops, its 2s by its flames (`SFL1`, `MFL1`, `LFL1`), `TR05`'s by its waterfalls; smoke, fire and spray for another version |
| 11 | 383 | obstacles (below) |
| 13 | all | 12 bytes: two s16 vectors about 32,767 long (5,322 of the 5,338 checked) |
| 17 | 17 | sounds (below) |

The program finds an extrablock by its id (0x45ffd0) as it readies a
block to draw (0x435910).

- **Cross-sections** (6), 8 bytes, one for each virtual road point of
  the block (14,222, as the `.col` has): u16, the first of the high
  level's first-group quads across the road there; u8, how many (the
  rows cover the group in order, in all 1,780 blocks); u8, 2 to 6, the
  music's section for that stretch, looked up ahead of the car by its
  speed (0x436440) and handed to the music player *(inferred)*; two u16
  road points, of a stretch of the road running beside this one on the
  left and on the right, `0xFFFF` for none (81 of the 14,222 have one,
  never both, each with its partner pointing back: `TR03`, `TR05`, and
  `TR08`'s hairpin). A car keeps the row it's on (0x4378f0); one off its
  row is put on the nearer of these roads, if there's one (0x437270),
  so a car leaving the road where another runs alongside is taken up by
  that one.
- **Painted lines** (9), 4 bytes: a vertex of the block, a road point
  (from the block's first), the line's type, and a quad of the high
  level. Each two records make a quad, from the vertex and the vertex
  moved by the road point's across vector (half a unit to the right),
  each end pushed out along the road by half its forward vector, so the
  pieces overlap (0x4353fc); type `0xFF` ends a line. The records run
  forward along the road, every one of them. Drawn at the high level only (0x435260) with the
  pictures `lin0` to `lin7`; on `TR00`, `TR02`, `TR03` and `TR05`.
  Types 0 to 4 are yellow, 5 to 9 white, each drawn as the type 5 below
  (0x448bcc); up close, each has its own shape (0x448a24), far off one
  plain quad (beyond 0x6E0000 deep, 0x448a93). The shapes are as the
  code builds them *(inferred)*:

  | Type | Line | Records |
  |---|---|---|
  | 0, 5 | solid, 5/16 of the quad's width in from each side | 2,525, 11,464 |
  | 1, 6 | broken: each piece from halfway to its end | 344, 5,022 |
  | 2, 7 | two solid | 355, 1,112 |
  | 3, 8 | solid on the right, broken on the left (0x448c50): no passing going forward | 268, 337 |
  | 4, 9 | broken on the right, solid on the left (0x4491d0) | 288, 181 |

  Types 3 and 4 are only on `TR03`, 8 and 9 only on `TR02`, all on the
  middle of two-lane roads; right and left as a driver going forward
  sees them, the across vector pointing right *(inferred)*.

  The picture among the worn ones is chosen by the vertex's place
  (0x4c412c). In the 3dfx program the lines take the road's shades,
  a little darker (0x446ec3).
- **Obstacles** (11), 20 bytes, what a car can hit (0x44042c): a 16.16
  point (its type 4 placement's, 784 of 784), u16 its radius in 256ths
  of the unit (the mesh's, across), u16 its number over the track (its
  slot in the program's table of their states), u8 twice its height
  rounded down, a 0, u8 its placement's place in its extrablock (774 of
  784), u8 1 if it's fixed or 2 if it's knocked over when hit (0x433650,
  which plays an animation script: [can.md](can.md)).
- **Sounds** (17), 16 bytes: a 16.16 point, u16 the sound's place in the
  track's `TRAMBnn.BNK`, u16 1 to play it now and then, 0 to loop it.
  The program sets each one's volume by its distance and its pan by its
  direction (0x434030). The banks agree: every looped one is a looping
  sound.

- **Meshes** (8): a u32 size, u16 vertex count, u16 polygon count, the
  vertices (three s16, 256ths of the unit, from where the mesh is
  placed), the polygons as the ground's, 0 or 2 bytes to 4.
- **Placements** (7, 18, 19): u16 size, u8 type, u8 mesh index. Type 1
  (16 bytes): a 16.16 point. Type 4 (20 bytes): the point, then a u32,
  its obstacle's place in the block's extrablock 11 (0x4343af), drawn
  turned as the obstacle is knocked over. Type 3: a u16 key count, a u16
  not worked out, then 20 bytes a key: a 16.16 point and four s16 not
  worked out. The program draws each extrablock's in a list of its own
  (18 with the ground, 7 with the second group, 19 last).

## In Reluva

Read as `Track` (`src/engines/nfs/nfs2se/formats/trk.cpp`): the blocks,
their vertices made absolute, the levels' polygons, the meshes and
placements, the cross-sections, painted lines, obstacles and sounds,
and every extrablock as it is. `reluva-tool nfs survey`
reads all 8 tracks; `reluva-tool nfs trk-obj` writes one as an OBJ.

The data viewer's Tracks page (`src/engines/nfs/nfs2se/pages/track_page.cpp`)
draws a track at a level, its objects placed (an animated one where its
animation starts), with the 3dfx program's pictures, from points of the
virtual road ([col.md](col.md)). Each quad has its picture laid across
it whole, turned as its texture's record says ([col.md](col.md),
Textures); unturned, its vertices take the picture's top left, top
right, bottom right and bottom left. The signs' lettering reads as in the
hosted game. Both faces are drawn. The quads take the 3dfx program's
greys from their shades (S turns them off). The painted lines are drawn
at every level, where the game draws them at the high one only, each
type's stripes as the program draws them close up, 2 cm above the road where the game
draws them after it (L turns them off); their far-off single quads
aren't. The placed sounds play from the track's `TRAMBnn.BNK` as the
game plays them, by the camera's place: the volume and pan by its
rule, the nearest loop where the game takes the first it comes to, and
the frame count the one-shots wait on stepping 30 times a second.

## Sources and checks

- **Where the facts come from:** the tracks themselves, read with survey
  scripts; no published description of NFS II's tracks is used.
- **How it was checked:** survey scripts over the 8 tracks (November
  2026): every size, offset and count consistent, every polygon's
  vertices inside its level, the first vertices against the next
  block's; then the reader on all 8; the unit tests (`nfs.unit.nfs2se`).
- **The fields worked out later** (November 2026) from `nfs2sen.exe`'s
  loading and drawing code, and `NFS2SEA.EXE`'s, disassembled and read;
  the two programs' track code is the same but for the 3dfx drawing (the
  shades, the lines' shading): the 3dfx program reads no field the
  software one doesn't, each checked over the
  8 tracks as given above; the pictures' way round against the hosted
  game's frames.

## Open questions

- What the header's word at 20 measures: no sum or largest of the
  blocks' sizes and counts searched matches it on all 8 tracks.
- What extrablock 10's 4, at `TR04`'s rope bridge, would have been.
