# Jedi Outcast: single player's Ghoul2 instances

> **Status:** partial · **Code:** `src/engines/idtech3/jk2/sp_ghoul2.cpp` · **Checked against:** `jk2sp.exe` 1.02a's and `jk2gamex86.dll`'s code; the first level running in Reluva · **Confidence:** from the code unless marked

## Overview

In single player the game DLL and the engine share Ghoul2 models
directly in memory, unlike multiplayer's numbered handles
([../../../formats/ghoul2.md](../../../formats/ghoul2.md)). The engine
owns an array of instances; the game holds a handle into it (a
`CGhoul2Info_v`, one word, in each entity at 0x160), asks the engine's
array object for an instance's vector of models, and reads and grows
that vector with its own inlined code. The engine's Ghoul2 functions
(slots 42–102 of its table,
[sp-interface.md](sp-interface.md)) take either the handle or one
model's record. So Reluva builds these structures in the game's memory
with 1.02a's layouts, and keeps them as the only copy of each
instance's state.

The GPL Jedi Outcast single-player source (`ghoul2_shared.h`) was read
as a guide to the structures' shape; their sizes and the rules below
were checked in the two programs' code.

## How it works

### The array object

`TheGhoul2InfoArray` (slot 83, which the game calls through 0x200981a0)
gives a C++ object: its first word points to five virtual functions,
called with the object in ECX and popping their own arguments:

| Offset | Function |
|---|---|
| 0 | `New()` → a new handle (0x200334aa) |
| 4 | `Delete(handle)` (0x2009686d: an entity's instance given back) |
| 8 | `IsValid(handle)` |
| 0xc, 0x10 | `Get(handle)` → the instance's vector (the game calls 0x10, 366 times) |

### Vectors and records

Every list is a Visual C++ 6 `std::vector`: 16 bytes, its allocator's
word then its first, last and end pointers (the game reads the first at
offset 4, 0x20011367). The game grows them itself, with its own
allocator: the C runtime it carries allocates from the process heap, so
buffers the engine makes and the game frees (or the other way) must come
from the same heap.

- **A model** (`CGhoul2Info`, 196 bytes: the game's stride, 0x2001135a,
  and the engine's, 0x4514fa): three lists (its surface overrides at 0,
  its bolts at 16, its bone overrides at 32), its model index 48 (-1:
  the place is free), custom shader 52, custom skin 56, the bolt it hangs
  from 60 (-1: none), root surface 64, level of detail bias 68, new
  origin 72 (-1), model handle 76, file name 80 (64 letters), default
  frame 144, the skeleton's and mesh's cached frames 148 and 152 (-1),
  flags 156, then the engine's own pointers, a valid flag at 172 and
  more pointers to 196. A new model's defaults are those the engine sets
  at 0x45153b.
- **A bolt** (16 bytes, 0x455529): its bone, its surface, the surface's
  kind and how many have asked for it.
- **A bone override** (148 bytes, the division at 0x456e05): its bone, an
  angle override's matrix (4, 3 by 4 floats), flags 52, the animation's
  start and end frames 56 and 60, start time 64, pause time 68, speed 72,
  the blend's frame, frame mixed with it, length and start 76–88, the
  angle override's blend length and start 92 and 96, and the matrix made
  from it 100.
- **A surface override** (24 bytes, 0x45a877): its flags, the surface,
  then a generated surface's place (two barycentric coordinates, its
  polygon and level of detail).

### The engine's functions

- **`InitGhoul2Model`** (0x451460): the instance made if it has no
  handle yet; the first model whose index is -1, else a new one, set to
  a new model's defaults, then its file name, model index (its own place),
  skin, shader, flags and bias; the place returned, or -1.
- **`AddBolt`** (0x4554d0): a surface of that name, else a bone; a bolt
  already on it is asked for once more and its place returned; else the
  first free bolt (bone and surface -1), else a new one.
- **`GetBoneIndex`** (0x456db0): the bone's override; to add one, the
  first unused (bone -1) or a new one, zeroed but for its bone.
- **`SetBoneAngles`** (0x452a10, 0x456280): the override found or added,
  its three angle flags replaced, the angle blend's length and start
  kept, the matrix made as multiplayer's (the angles moved onto the axes
  the bone turns about, in the bone's own frame); the model's cached
  skeleton frame made stale (148 set to 0).
- **`SetSurfaceOnOff`** (0x45a7b0): the surface's override (a generated
  surface's, numbered 10000 and up, left alone) takes the off (0x2) and
  no-descendants (0x100) flags given; with none, a new one, the mesh's
  own flags for it so changed.
- **`GetGLAName`** (0x453810) and **`GetAnimFileNameIndex`**: the mesh
  header's skeleton name (offset 0x48).
- **`RE_GetAnimationCFG`** (0x46bda0): a file's text into the buffer
  given (if any) and its length returned, 0 if there's none.

From the GPL source, not traced in 1.02a *(published)*: `SetBoneAnim` as multiplayer's (the blend from
the animation playing, `Ghoul2Instances::makeAnimation`), the pause time
cleared; `GetBoneAnim` the frame an animation is at (a paused one's at
its pause); the ranges, pausing (a paused animation starting later by
the pause's length), stopping animations and angle overrides (the
override given back once it has no flags left), the model's skin,
shader, flags, bias and root surface, a surface's index and flags,
removing bolts and models, attaching a model to another's bolt (the
model's place times 1024 plus the bolt) or an entity (the entity times
2^20 added), and a matrix's origin or axis.

## In Reluva

`SpGameHost` (in Jedi Outcast's mode) makes the array object and its
five functions, and answers the Ghoul2 slots above from the records in
the game's memory, its own vectors' buffers from the process heap.
Each mesh and skeleton is read once (`parseGlm`, `parseGla`) for its
surface and bone names.

**Posing:** for a bolt's matrix (`GetBoltMatrix`), an instance's records
are handed to multiplayer's posing code (`Ghoul2Instances::setInstance`:
each model's file, skin, bolt link, bolts, surface flags, angle
overrides with their matrices, and animations), again only when the
records' bytes have changed, so a frame's many bolt questions pose each
model once. A paused animation is posed as if it went on *(guessed)*.
The client game's `G2_SetModels` (its call 85) gives each model in use
its skin: the skin list's entry at its custom skin (0x453180, into the
model at 0xa8). The custom skin is the game's number for the skin, not
the renderer's, so a model is drawn with the skin at 0xa8 (with the
custom skin, Jan was drawn with Kyle's first-person skin).

**Drawing:** a scene entity's Ghoul2 pointer (164) is the
`CGhoul2Info_v`'s address; the Play page asks the host for that
instance's models posed at the view's time (`SpGameHost::ghoul2Drawn`),
the same posing as the bolts'. The client game sets the pointer for
every entity, one with no Ghoul2 models too (the opening's cockpit, an
MD3): then the entity's own model is drawn, as the original's renderer
draws one whose instance has no models.

Not yet answered (they stop the game, by name): collision, the matrix
overrides, adding and removing surfaces and bones, copying an instance,
the new origin, surface names and parents, and saving and loading
instances.

## Sources and checks

- **Where the facts come from:** `jk2gamex86.dll`'s copy of the table
  (0x200970a9, 103 words) and its use of the array (0x200981a0, the
  virtual calls after it); `jk2sp.exe`'s Ghoul2 functions at the
  addresses above. The GPL Jedi Outcast single-player source as a guide
  to the structures, *(published)* for the functions marked so.
- **How it was checked:** `sp-run` on `kejim_post` runs the game's
  `Init` (its NPCs' models, bolts, bones and surfaces set up through
  these functions), the player joins, and 100 server frames run, the
  level's scripts starting its opening cinematic; with `-cgame` the
  client game's frames place bolts through `GetBoltMatrix`.
