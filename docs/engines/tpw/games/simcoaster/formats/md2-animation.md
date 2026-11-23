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
| 0x10 | 4 | How many rotation keys (the low half) and scale keys (the high half) |
| 0x14 | 2 | What it moves: a node of the model, by its record's place: the meshes' records first, then the other nodes' ([md2.md](md2.md#the-nodes)) |
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

How the numbers at 0x14 were found: the octopus's create animation
moves its 1 to (0, −4.5, −0.9), the rest position of its node `octopus`,
which is its second mesh but its third node; its 4 to `Cylinder01`'s,
its fifth mesh; its 20 is `smoke01`, past its 7 meshes. That numbers the
meshes first; the front end's camera path settled the order: its track
for 60 moves `sidewallright` from its rest position, the 61st mesh
record but the 65th node of the tree, and its 145 to 157 move
`CAMERA1`, `Particle Pivot`, `baseobject`, `Dummyx` and `Dummyx01` from
theirs, numbered as their records lie, with `superparent`, out of the
tree, among them. Numbered so, 2,823 of the 6,058 tracks that move
whole nodes start at their node's rest pose, against 2,077 numbering
the meshes, then the other nodes, in the tree's order.

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

As many keys as the record says, 20 bytes each: its frame (2 bytes), a
word not worked out (2 bytes: 0xFFFF in some keys, counting up in
others), then a quaternion, x, y, z, w. In all 3,351 rotation tracks the
keys fit before what follows them, filling the room exactly in 2,885;
the last key's frame is the animation's in 3,312. Every one is of unit length. Read
x, y, z, w, far more rotation tracks start at their node's rest
rotation than read w first (1,129 against 100, counted before the
numbering above was found). They replace the rest pose's
rotation.

### Scales

16 bytes a key: its frame (4 bytes), x, y and z. As many as the record
says.

### Vertices

A mesh's single vertices move by its vertex track: its positions, some
at a time, each set of them with keys of its own. At the record's 0x28:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | 1 or 3 (1,438 and 1,282 of the tracks): not worked out |
| 0x02 | 2 | How many runs |
| 0x0C | 4 | Where the runs' records are: straight after this header, at its 0x34, in all |
| 0x14 | 12 | The offset the keys are from: three floats, the mesh's middle |
| 0x20 | 12 | The size of the keys' steps: three floats |

A run, 20 bytes: how many keys and how many positions (2 bytes each),
then where the positions' numbers, the keys' frames (2 bytes each) and
the keys are, then 0. A key is 4 bytes a position, the key's positions
in the run's order, then the next key's: three signed 10-bit numbers
from the word's bottom, x, y and z, each the position as so many steps
from the offset (−512 to 511 of them). The positions are in the mesh's
own space; they replace the rest pose's, straight between keys
*(inferred)*.

How it was found: read with the numbers unsigned from the offset, the
keys at frame 0 put 4,495 of 75,985 positions at their rest place; the
bunting's misses were off by exactly its mesh's width (29.62 in x, 4.94
in y), the offset in its middle; read signed, 54,632 of the 101,918
positions keyed at frame 0 are at rest (within a step and a half),
about as many as the whole-node tracks starting at rest; read a
position's keys together instead, far fewer. Each track's first run
names two numbers just past its mesh's positions (99 and 100 of the
whale spout's 99): not worked out, left alone.

The camera path's table opens so: its middle's vertices part from frame
500, and the globe rises through the lit well beneath, as the recording
shows ([front-end.md](../systems/front-end.md)).

### Textures

Not read yet. A texture track holds 2 × 2 matrices of floats over
frames: the water rides' flowing textures, perhaps *(guessed)*.

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
| 3 | `l` | all 13 scripts with an `l` play 3; perhaps load *(guessed)* |
| 4 | `s` | all 22 with an `s` play 4; the rides play it before their main loop, a start perhaps *(guessed)* |
| 6 | `e` | all 20 with an `e` play 6; the rides play it after their main loop, an end perhaps *(guessed)* |
| 7 | `u` | 3 of the 5 with a `u`; perhaps unload *(guessed)* |
| 11 | `o` | 14 of 17 *(guessed)* |

A script's second number, the variant, picks among numbered files: the
ATM plays `WAITANIM 5 0` to `WAITANIM 5 14`, and has `atmm1.MD2` to
`atmm15.MD2`, so variant v is the file numbered v + 1; one without a
number, as `cameram.MD2`, is variant 0 *(inferred)*.

### How fast

25 frames a second *(inferred)*: the front end's camera path
([front-end.md](../systems/front-end.md)), played so from 10 s into the
recording, when the advisor's portrait appears, keeps time with it: its
hall at frame 0 is the recording's at 9 s, its doors open (frames 190 to
260) at 20 s, its globe rises (from frame 500) from 32 s.

Some rides' motion isn't in their animations: the twister's arm and
cars don't turn in `twspinrm.MD2`, whose three tracks move the fence
and two helpers not at all. The game moves them itself, it seems
*(inferred)*, as the scripts' ride commands (`TOUR`, `BUMP`) suggest.

## In Reluva

`Animation` (`src/engines/tpw/formats/animation.cpp`) reads the
tracks' positions, rotations, scales and vertex runs, and poses a node
at a frame: Bezier or straight between position keys, the rotations'
shortest turn between keys, the scales straight; and a mesh's positions,
straight between a run's keys (`meshPositions`, in
`src/engines/tpw/ui/model_view.cpp`, gives every mesh's). `Model` keeps its node tree, which
the tracks name. The viewer's Models page plays a model's animations
(N), on and on, or runs the model by its script (R): the script machine
against the stand-in for the park, the model posed by whatever animation
the script plays (`src/engines/tpw/ui/ride_run.cpp`); `reluva-tool tpw
ride-shot <game folder> <model> <out.png> <seconds>` draws a model so
many seconds into its script; `reluva-tool tpw md2-shot ... -anim <file> <frame>`
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
  octopus's raises it and its sign out of the water to where its idle
  animation, played next by its script, takes them on without a jump;
  the camera path's table opens under the globe. The unit tests
  (`tpw.unit.formats`) pose a track built in the test between its keys,
  its vertices among them.

## Open questions

- The texture tracks, the word of flag 0x20000, and the easing runs.
- The vertex track's first word (1 or 3), and the two numbers past the
  mesh's positions in each first run: the mesh's box, perhaps
  *(guessed)*.
- 444 tracks name nodes past their model's count, all of them the end
  feature's (`endm1` to `endm3`, 111 in each world): perhaps they move a
  model other than `end.MD2`, as the rides' supplementary meshes
  (`SupplementalMeshes`, `wr_ring.md2`).
- The numbers 3, 4, 6, 7 and the other endings.
- The rotation keys' second word.
- The root's other words and its list.
