# id Tech 3: Raven's Ghoul2 models (`.glm` meshes, `.gla` skeletons)

> **Status:** decoded · **Code:** `src/engines/idtech3/formats/ghoul2.cpp`, `render/model_cache.cpp`, `formats/player_animation.cpp` · **Checked on:** Jedi Outcast 1.02a's 71 meshes and 21 skeletons (`reluva-tool idtech3 ghoul2-check`, November 2026) · **Confidence:** layout confirmed; how they're posed and drawn published unless marked

## Overview

Jedi Outcast's skeletal models (and Jedi Academy's, said to be the same,
unchecked). A mesh (`.glm`) holds surfaces in levels of detail, each
vertex hung off up to four bones; it names a skeleton (`.gla`) holding
the bones and every animation frame. Many meshes share one skeleton: all
of Jedi Outcast's people use `models/players/_humanoid/_humanoid.gla`
(72 bones, 17,278 frames). A mesh naming `*default` has no skeleton: one
still bone. The game picks each frame and bone on its own (the Ghoul2
calls of its code); the files only hold the shapes and the frames.

All numbers are little-endian; offsets are from the start of the record
they're in unless said.

## Layout

### Mesh (`.glm`)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"2LGM"` |
| 4 | 4 | version, 6 |
| 8 | 64 | name |
| 72 | 64 | its skeleton's name without `.gla`, or `*default` |
| 136 | 4 | 0 (for the game's use) |
| 140 | 4 | bone count, its skeleton's |
| 144 | 4 | levels of detail |
| 148 | 4 | where the first level is |
| 152 | 4 | surface count (the same at every level) |
| 156 | 4 | where the surfaces' names are (164) |
| 160 | 4 | the file's size |

Then, from 164, an offset for each surface (from 164) to its name record:

| Offset | Size | Field |
|---|---|---|
| 0 | 64 | name |
| 64 | 4 | flags: 1 a bolt point (a tag, not drawn), 2 off (not drawn unless the game turns it on) |
| 68 | 64 | shader |
| 132 | 4 | 0 (for the game's use) |
| 136 | 4 | parent surface, -1 for the root |
| 140 | 4 | child count, then each child's index |

Each level of detail: its size (to the next level), then an offset for
each surface (from just after the size) to that surface at this level:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | 0 |
| 4 | 4 | its index |
| 8 | 4 | back to the file's start (negative) |
| 12 | 8 | vertex count, where they are |
| 20 | 8 | triangle count, where they are (three i32 vertex indexes each) |
| 28 | 8 | bone reference count (at most 32), where they are (an i32 bone each) |
| 36 | 4 | its size |

A vertex is 32 bytes: f32[3] normal, f32[3] position, a packed u32 and
four bytes. The packed number holds the weight count less one in its top
two bits; each weight's bone (an index into the surface's bone
references) in five bits from bit 0; and the top two bits of each weight
but the last, from bit 20, whose low eight bits are the four bytes'. A
weight is that 10-bit number / 1023; the last is what the others leave
of 1. The vertices' texture coordinates (f32[2] each) follow them.

### Skeleton (`.gla`)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"2LGA"` |
| 4 | 4 | version, 6 |
| 8 | 64 | name |
| 72 | 4 | f32 the scale it was built at (Jedi Outcast: 1, 0.64, 0.6, 0.5) |
| 76 | 8 | frame count, where the frames are |
| 84 | 4 | bone count |
| 88 | 4 | where the compressed bones are |
| 92 | 4 | where the bones are (100) |
| 96 | 4 | the file's size |

Then, from 100, an offset for each bone (from 100):

| Offset | Size | Field |
|---|---|---|
| 0 | 64 | name |
| 64 | 4 | flags |
| 68 | 4 | parent, -1 for the root |
| 72 | 48 | the base pose, a 3x4 matrix, row by row |
| 120 | 48 | its inverse |
| 168 | 4 | child count, then each child's index |

A bone may come before its parent: the humanoid skeleton's bone 18
(`ceyebrow`) and galak_mech's do.

The frames are a 3-byte index for each frame's every bone (frame by
frame), into the compressed bones, which run from their offset to the
file's end, 14 bytes each: a rotation as a quaternion w, x, y, z (u16
each: / 16383, less 2) and a translation x, y, z (u16 each: / 64, less
512).

## Behaviour

- **Posing.** Each bone's matrix in a frame is its frame bone (the
  quaternion as a rotation, with the translation) placed on its parent's:
  the root's alone (times the root matrix, the identity unless the game
  moves the model's origin), every other bone's parent's times its own.
  Between two frames, the twelve numbers of each frame bone are blended
  before that. A vertex is the sum, over its weights, of each bone's
  matrix applied to it, times the weight; normals the same with the
  rotation only. The skeleton's scale isn't used.
- **Surfaces drawn.** A surface is drawn only if it has no flags (nor any
  the game sets): bolt points and `_off` surfaces aren't. Its shader is
  the skin's for its name if a skin is used (the default shader if the
  skin doesn't name it), else its own.
- **Skins.** `model_default.skin` beside a person's mesh: lines of
  `surface,shader`, as MD3 skins.
- **Animations.** The skeleton's folder holds an `animation.cfg`: lines
  of name, first frame, frame count, looping (-1: it holds its last
  frame, anything else: the whole animation repeats) and frames per
  second (negative: played backwards; 0 taken as 1). The humanoid
  skeleton's has 989.

## In Reluva

`parseGlm` and `parseGla` read the files, checking every offset, count
and index; the compressed bones are kept as the file has them and
unpacked as they're needed. `poseBones` and `skinSurface` pose them on
the CPU. The model cache (`ModelCache`) loads a mesh with its skeleton,
each level of detail as a model of its own, and poses it again for every
draw into a mesh of its own. `reluva-viewer`'s Models page lists every
`.glm`, with its folder's `model_default.skin`, and steps through its
skeleton's animations (`[` and `]`), each played on the whole skeleton.
`reluva-tool idtech3 ghoul2-check` reads them all. A surface a skin
doesn't name keeps its own shader, as with MD3 models, where the
original draws it with its default shader.

Not yet: what the game's code does with them (its Ghoul2 calls: which
animation each part of the skeleton plays, bones turned by the game,
bolts, surfaces turned on and off, several models on one skeleton,
collision against the posed mesh), which comes with the game's code
(Jedi Outcast's plan, phases 6 and 7).

## Sources and checks

- **Where the facts come from:** the layout and the packed weights and
  bones from the GPL Jedi Outcast source (`mdx_format.h`, `matcomp.c`),
  how bones are posed and surfaces chosen from its renderer
  (`tr_ghoul2.cpp`: `G2_TransformBone`, `RenderSurfaces`), the
  animation lines from its game code (`bg_panimate.c`); all checked
  against the files.
- **How it was checked:** `ghoul2-check` reads every mesh and skeleton in
  Jedi Outcast 1.02a (71 and 21), each mesh with as many bones as its
  skeleton (445,610 compressed bones in the humanoid's); a unit test
  (`idtech3.unit.formats`) reads a made-up mesh and skeleton, a bone
  before its parent, and poses them; Kyle, a stormtrooper and the
  protocol droid look right in the Models page, standing and animating. Not yet
  compared with the original running.
