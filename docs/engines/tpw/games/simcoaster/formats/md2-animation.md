# SimCoaster: model animations (`.MD2`, flag 0x20)

> **Status:** partial · **Code:** `src/engines/tpw/formats/animation.cpp` · **Checked on:** the US release's 1,209 animations · **Confidence:** confirmed unless marked

## Overview

1,209 of the `.MD2` files are a model's animations, not models
([md2.md](md2.md)): header flag 0x20, no offsets in the header's table.
Each sits beside the model it moves, its name the model's with a letter
or two added: `wateride.MD2`'s are `waterideb`, `wateridec`, `waterider`,
`wateridem1` and `wateridem2`. Each moves the model's nodes over its
frames (100 for most; 1 to 920): whole parts, by their position,
rotation and scale, and single vertices and textures. No description of
the format had been published; all that follows was worked out from the
files.

## Layout

All numbers little-endian. After the model's 0xB8-byte header (its
counts copied, flag 0x20, its table of offsets 0), the tracks' data,
then a record for each track, then a root, the last 0x48 bytes:

| Offset in the root | Size | Field |
|---|---|---|
| 0x08 | 4 | The animation's length, in frames |
| 0x2C | 4 | Where the first track's record is; the records run on to the root |
| 0x38 | 4 | Where a list of 16-bit numbers is: not worked out |

Its other words are not worked out. In all 1,209 files the records end
where the root begins.

### A track's record

0x40 bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Its number, from 0 |
| 0x04 | 4 | What it has (below) |
| 0x0C | 4 | Its length, in frames |
| 0x10 | 4 | The high half: how many scale keys |
| 0x14 | 2 | What it moves: a node of the model, by its place in the tree (each node before its children, children before the node's next) for whole parts; a mesh, by its number, for vertices and textures *(inferred)* |
| 0x18 | 4 | Where its position track is |
| 0x1C | 4 | Where its rotation keys are |
| 0x20 | 4 | Where its scale keys are |
| 0x28 | 4 | Where its vertex tracks are |
| 0x2C | 4 | Where its texture tracks are |
| 0x30 | 4 | Where a word for it is (flag 0x20000): not worked out |
| 0x34 | 4 | Where bytes rising from 0 to 255, 8 a run, are: easing, it seems *(guessed)*, not used yet |

What it has, by the bits of 0x04, each over its own pointer in all
10,075 records: 0x1 a position track; 0x8 or 0x10 rotations; 0x80 or
0x100 scales; 0x1000 vertex tracks; 0x10000 texture tracks; 0x20000 the
word at 0x30.

### Positions

A header of 16 bytes: its kind (4 bytes), how many values and how many
keys (2 each), where the values are and where the keys' frames are (4
each). The keys' frames are 4 bytes each; the values three floats.

- **Kind 0x12**, a Bezier curve (1,854 tracks): 3 × (keys − 1) + 1
  values, each key then the curve's two control points to the next,
  which lie near it (the advisor's keys at frames 0, 6, 17, 37, 54 and
  75).
- **Kind 0x18**, straight lines (2,484 tracks): a value for each key.

The values are the node's position in its parent's space, not offsets
from it: they replace the rest pose's.

### Rotations

20 bytes a key: its frame (2 bytes), a mark (2 bytes: 0xFFFF on the
last), then a quaternion, x, y, z, w. Every one is of unit length. Read
x, y, z, w, 1,129 of the 3,273 rotation tracks start at their node's
rest rotation, against 100 read w first. They replace the rest pose's
rotation.

### Scales

16 bytes a key: its frame (4 bytes), x, y and z. As many as the record
says.

### Vertices and textures

Not read yet. A vertex track is records of 20 bytes, each how many keys
and how many of the mesh's vertices it moves (2 bytes each), then where
the vertices' numbers, the keys' frames and the keys are; a key is 4
bytes a vertex, three 10-bit fields near 512 (offsets, it seems, scaled
by floats in the track's header) *(guessed)*. A texture track holds 2 ×
2 matrices of floats over frames: the water rides' flowing textures,
perhaps *(guessed)*.

## Behaviour

### Which animation is which

The scripts play a model's animations by number (`WAITANIM 5 0`); the
files' names end in a letter, as the shipped `test.rss` names them
(`ANIM_Create`, `ANIM_Main`, `ANIM_Break`, `ANIM_Repair`). Set beside
the numbers every script uses:

| Number | File's end | How it is known |
|---|---|---|
| 0 | `c`, create | 172 of the 173 scripts that play 0 have one; played first |
| 5 | `m` or `m1`, main | the loop scripts go back to after a repair (`LOOPANIM 5`) |
| 9 | `b`, break | played after `COPY VAR_BROKEN 1` |
| 10 | `r`, repair | played after `COPY VAR_BROKEN 0` |
| 2 | `i` | 37 of the 43 scripts that play it have one *(guessed)* |
| 11 | `o` | 14 of 17 *(guessed)* |

Numbers 3, 4, 6 and 7, and the endings `l`, `s`, `e`, `u` and the
numbered `m`s after the first, are not matched yet. How fast the frames
run isn't known: Reluva plays 25 a second *(guessed)*.

Some rides' motion isn't in their animations: the twister's arm and
cars don't turn in `twspinrm.MD2`, whose three tracks move the fence
and two helpers not at all. The game moves them itself, it seems
*(inferred)*, as the scripts' ride commands (`TOUR`, `BUMP`) suggest.

## In Reluva

`Animation` (`src/engines/tpw/formats/animation.cpp`) reads the
tracks' positions, rotations and scales, and poses a node at a frame:
Bezier or straight between position keys, the rotations' shortest turn
between keys, the scales straight. `Model` keeps its node tree, which
the tracks name. The viewer's Models page plays a model's animations
(N), on and on; `reluva-tool tpw md2-shot ... -anim <file> <frame>`
draws one frame. The script machine's stand-in for the park waits for
an animation as long as its file's frames take, where its number is
known ([scripts.md](../systems/scripts.md)). `reluva-tool tpw
anim-check` reads every animation and sets it against its model; the
game-files test runs it (`tpw.gamedata.animations`).

## Sources and checks

- **Where the facts come from:** the US release's animations (November
  2026): the root found by following every pointer in a file to the
  one nothing points at; the records, flags and pointers counted over
  all 10,075; the kinds of track by their counts; the quaternions'
  order by the rest poses; the animations' numbers by the scripts.
- **How it was checked:** all 1,209 read with `reluva-tool tpw
  anim-check`; animations drawn frame by frame: the research centre's
  `c` grows it from the ground and brings its lightning bolt in, the
  octopus's swings it to its platform. The unit tests
  (`tpw.unit.formats`) pose a track built in the test between its keys.

## Open questions

- The vertex and texture tracks, the word of flag 0x20000, and the
  easing runs.
- 466 tracks name nodes past their model's count: perhaps they move a
  model other than the one their name starts with.
- The numbers 3, 4, 6, 7 and the other endings; the frame rate.
- The root's other words and its list.
