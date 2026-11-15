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
  the root's alone (times the root matrix: in the game a quarter turn,
  see Animation; the Models page leaves it out), every other bone's
  parent's times its own.
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

### Animation

How a game's instance of a model is posed, as Jedi Outcast's engine does
it for its modules (`jk2/ghoul2_instances.cpp`):

- **An instance** is a list of models (a body, its weapon, its saber),
  each a mesh with its skeleton. A model can hang from a bolt of another
  in the same instance (`SetBoltInfo`: `(model << 10) | bolt`): a weapon
  from the hand.
- **Animations are set on bones** (`SetBoneAnim`: first frame, end frame,
  flags, speed, start time). A bone with an animation of its own takes
  its frame from it; every other bone takes its parent's frame, the root
  its first. So the legs' animation on the root moves the whole body
  unless the torso's, set on the lower spine, overrides that part.
- **Timing:** frame = first + (time - start) / 50 ms x speed, between two
  frames blended by the fraction. Past the end frame less one, a looping
  animation (0x10) comes round again from the first; any other holds its
  last frame. A negative speed plays it backwards.
- **Blending** (flag 0x80, with a blend time in ms): if the bone was
  already playing an animation (0x8 or 0x10), the new one fades in from
  where the last one was over the blend time. Where it was is its frame
  then (a looping one come round, a frozen one (0x40) on its last frame,
  any other that has ended taken as frame 0) and the frame after it (the
  first if it loops, else held to the last). The two are mixed with
  their weights the other way round from the fraction: at frame 10.3,
  frame 10 by 0.3 and 11 by 0.7, so a whole frame comes out as the one
  after it. The bone's pose is then that by what's left of the blend,
  the new animation's by how far it's gone. Set again at the same time,
  only the blend's length changes; on a bone with nothing playing, the
  flag is dropped. Bones without an animation of their own blend with
  their parent.
- **Angle overrides** (`SetBoneAngles`: angles, flags, then which of the
  bone's axes each angle turns about) make a rotation from the angles
  (pitch about the "right" axis, yaw about "up", roll about "forward",
  some 180 degrees round), taken into the bone's own frame (its base
  pose, the rotation, the base pose's inverse). Turning after (0x2) puts
  it after the bone's placement; turning before (0x1) puts it in place of
  the frame's.
- **A bone's placement** is its parent's times its frame's (blended); a
  vertex is placed by its bones' placements, weighted. The root bone's
  "parent" is the original's root matrix, which its renderer calls the
  identity but is a quarter turn about the vertical (rows 0 −1 0, 1 0 0,
  0 0 1: the skeleton's x along the entity's y). So a model drawn by its
  entity's axes faces the way they say; without the turn, every player
  stands a quarter turn to the side. A model hung from another's bolt is
  placed on the bolt instead.
- **Bolts:** a name is looked for among the mesh's surfaces first (tag
  surfaces are named with a `*`: `*r_hand`, `*head_top`, `*flash`), then
  the skeleton's bones. A bone's bolt is its placement times its base
  pose. A tag surface's is its triangle as placed: the third corner the
  origin, the axes from its longest and shortest sides and their cross
  product.
- **In the world** (`GetBoltMatrix`): the bolt placed by the instance's
  angles and origin (each axis of the bolt's origin scaled, if a scale is
  given), and its axes turned a further 270 degrees of yaw, as the
  original's are: with the root's quarter turn, a bolt on a bone in its
  base pose comes out with the world's axes.
- **Surfaces** turned on or off by name (`SetSurfaceOnOff`, 2 off), or all
  but one and those under it (`SetRootSurface`); tag surfaces are never
  drawn.

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

The game's Ghoul2 calls are answered by `jk2::Ghoul2Instances`
(Animation, above). Not yet: collision against the posed mesh (the
traces' Ghoul2 records), and single player's calls (Jedi Outcast's
plan, phase 7).

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
- **Animation** comes from the GPL Jedi Outcast multiplayer source's
  Ghoul2 code (`G2_bones.cpp`, `G2_misc.cpp`, `G2_API.cpp`) and its
  renderer's `G2_TransformBone` and surface bolts, read as documentation.
  A unit test (`idtech3.unit.formats`, "ghoul2 instances") poses the
  made-up skeleton by an animation (looping and held), an angle override
  and a model hung from another's bolt. In 1.02a's multiplayer, Kyle
  standing in third person fills 64 units from his feet up, his pistol
  hung from his hand at hand height beside him (`game-run -cgame -entities`). Live, a bot
  followed in third person faces the way she runs (November 2026; before
  the root's quarter turn, players stood side-on, as seen against the
  retail game over the network). The same
  test blends one animation into the next: from a frozen one, at a
  fraction of the way, and from one that had ended.
