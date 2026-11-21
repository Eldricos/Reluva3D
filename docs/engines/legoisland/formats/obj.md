# LEGO Island: the SI files' 3D objects (`.ani`, `.mod`, `.tex`, `.gph`)

> **Status:** decoded · **Code:** `src/engines/legoisland/formats/wdb.cpp`, `src/engines/legoisland/formats/gph.cpp` · **Checked on:** the English 1.0 CD's 26 SI files, all 1,161 3D objects read to their ends (November 2026) · **Confidence:** layout confirmed; what the fields mean inferred unless marked

## Overview

An SI file's 3D objects (type 11, format `OBJ`: [si.md](si.md)) are the
animations, models, textures and paths its scenes use, each the file it
was made from on the developers' machines, by its name's extension:

| Source file | Presenter | On the CD | What |
|---|---|---|---|
| `.ani` | `LegoAnimPresenter` and others | 1,001 | an animation: of characters and models, perhaps of the camera |
| `.mod` | `LegoModelPresenter` | 10 (6 different) | a model: the four vehicles the player builds, the shark, the big skeleton |
| `.tex` | `LegoTexturePresenter` | 47 | a texture: most of them the decals the player chooses for the vehicles they build |
| `.gph` | `LegoPathPresenter` | 5 | a path: the ground the characters and vehicles move on, in the island, Acts 2 and 3, and the two races |
| `inline` | `LegoModelPresenter`, `LegoActionControlPresenter` | 98 | no data: the extra string names a model of `WORLD.WDB` instead (`DB_CREATE:2x4yel`) |

An object's pieces, joined, are its file, and then one more byte, 0 in
all 1,161.

The animations, models and textures are laid out as `WORLD.WDB`'s are
([wdb.md](wdb.md)), and share its readers.

## Layout

All values little-endian; floats are 32-bit; strings as in
[wdb.md](wdb.md#layout), a 32-bit length then the letters.

### An animation (`.ani`)

| Size | Field |
|---|---|
| 4 | 17 in every one: a version, presumably *(guessed)* |
| 4 | a distance, and |
| 12 | a point, x, y, z: where it plays *(inferred)*; 0 for some |
| 4 | 1 if it moves the camera (49), else 0 |
| 4 | 1 in every one |
| 4 | actors |
| each | an actor: its name in capitals (`INFOMAN`, `2X4RED`, `*CAPDB01`), and its kind (4), 1 to 5 as in the worlds' lists ([dta.md](dta.md#a-model)) |
| 4 | its length, in milliseconds |
| ... | if it moves the camera: three lists of keys, below |
| ... | a tree of nodes, as a model's animation in [wdb.md](wdb.md#a-model) |

A node is named after the actor or the part of it it moves, with a `-`
in front of the root's (`-SJS012IN`, `-ELEV1-3`). The camera's lists
are each a 16-bit count and its keys: where the camera is (x, y, z), what
it looks at (x, y, z) *(inferred)*, and a third list, empty in all 49.

`WORLD.WDB`'s models hold the same from the 1 on: 1, no actors, a
length of 0, and the tree.

For 364 of the worlds' 664 listed animations ([dta.md](dta.md)), the
list's point and distance are this animation's, exactly.

### A model (`.mod`)

A model as `WORLD.WDB` holds one ([wdb.md](wdb.md#a-model)): its
version (19), the offset of its textures from its start, its animation,
its ROIs and its textures. Its ROIs take parts' LODs from `WORLD.WDB`
by name (`shark`'s `shkbody` and `shkhead`, the parts `shkbody` and
`shkhead`).

### Textures (`.tex`)

| Size | Field |
|---|---|
| 4 | textures: 1 in every one |
| each | a texture, as in [wdb.md](wdb.md#a-texture-list) |

The name here is a field of 51 to 59 bytes: the name (`chwind.gif`), a
NUL, and old bytes after it, from the program that wrote it.

### A path (`.gph`)

A path is a graph of faces, the edges between them, and the points at
their ends; triggers on its faces start things as the player passes.

| Size | Field |
|---|---|
| 2 | kinds of trigger |
| 2 | points |
| 2 | edges |
| 2 | faces |
| each | a kind of trigger: a byte's length and its name (`TRC`, `TRS`, `TRE`, `TRH`, `TRM`, `TRD`, `TRW`), then 4 bytes: `ff ff ff` and 0x11, 0x1F, 0x21, 0x31 or 0x3F |
| 12 each | the points: x, y, z |
| each | an edge, below |
| each | a face, below |

An edge:

| Size | Field |
|---|---|
| 2 | 15 if it bounds two faces, 4 if one |
| 2 + 2 | the points it runs from and to |
| 2 | a face it bounds |
| 2 + 2 | the next edge and the one before round that face *(inferred)*: each shares one of its points |
| 6 | if it bounds two faces: the other, and its edges round it, as above |
| 12 | its direction, from its first point to its second, of length 1 |
| 4 | its length |

Every edge's direction and length are the ones from its first point to
its second, and every face an edge names lists that edge among its own.

A face:

| Size | Field |
|---|---|
| 1 | edges: 3 to 13 |
| 2 each | its edges |
| 1 | 0 to 7 |
| 1 | 1 to 8 |
| 1 + name | its name: `EDG10_01` |
| 16 | its plane: a normal of length 1 (nearly always up, 1,795 of 1,804), and a distance |
| 16 each | for each edge, a plane through it: a normal and a distance |
| 12 + 4 | its centre and radius |
| 1 | triggers: 0 to 17 |
| 10 each | a trigger: the kind (2, an index into the kinds), a value (4: 1, 200...), and a float |
| 12 | only if it has triggers: a direction, of length 1 |

## In Reluva

`parseAnimation`, `parseModel` and `parseTextures`
(`src/engines/legoisland/formats/wdb.cpp`) read the first three, and
`GphFile` (`src/engines/legoisland/formats/gph.cpp`) a path;
`objectData` and `objectKind` (`src/engines/legoisland/formats/si.cpp`)
give an object's file and what it is. The data viewer's SI page says
what each 3D object holds, and draws a texture; it doesn't draw the SI
files' models or play their animations yet.

- `reluva-tool legoisland si-objects <file.si> [object id]`: each 3D
  object read, and what it holds.
- `reluva-tool legoisland si-extract <file.si> <out folder> [object id]`
  writes them as `.ani`, `.mod`, `.tex` and `.gph` files.

## Sources and checks

- **Where the facts come from:** the objects alone, with what
  [wdb.md](wdb.md) had found. An animation's start was read by hand
  beside the smallest (an elevator's, 147 bytes) and one with actors
  (`sjs012in`), then all 1,001 read by a script; a model and a texture
  were seen to be `WORLD.WDB`'s layouts. A path's counts, points and
  edges were read beside the smallest path (the jet ski race's), each
  edge checked against its points, and its faces worked out until all 5
  paths read to their ends; then every edge's and face's indices were
  checked against each other.
- **How it was checked:** `reluva-tool legoisland si-objects` reads
  every 3D object of the 26 SI files, none failing: 1,001 animations, 10
  models, 47 textures, 5 paths. The data viewer's SI page draws the
  textures as their build screens show them (the copter's `POLICE`
  decal). `legoisland.unit.obj` reads an animation (with and without the
  camera), textures, a model and a path built in the test.

## Open questions

- What the animation's 1, and the camera's third list, are.
- Which of the edge's flag bits says it bounds two faces: they are
  always 15 or 4.
- What a face's two bytes, a trigger kind's top byte, and a trigger's
  value and float mean.
- Why the worlds' lists give a different point and distance from 300 of
  their animations'.
