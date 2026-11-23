# Jedi Knight: models (`.3do`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/model.cpp` · **Checked on:** US retail, all 477 models · **Confidence:** confirmed unless marked

## Overview

The game's 3D models, in `Res2.gob`'s `3do\`: characters, weapons,
doors, lifts, crates, pickups. Their materials are in `3do\mat\` (and
`mat\`). Plain text, its comments naming the columns; the comment at the
top says which tool wrote it ("ver4.63").

## Layout

Line by line; a `#` starts a comment to the line's end. Four sections, in
this order, each starting `SECTION: <name>`.

### HEADER

`3DO 2.1` in every file.

### MODELRESOURCE

`MATERIALS <n>`, then `<i>: <file.mat>` for each. One file has no space
after the colon (`0:STATPOLEFRONT.MAT`).

### GEOMETRYDEF

- `RADIUS <r>`: the model's.
- `INSERT OFFSET <x> <y> <z>`.
- `GEOSETS <n>`: 1, or 4 in 53 models (the characters'): the same meshes
  at less detail, the first the most detailed *(inferred)*.
- For each, `GEOSET <i>`, `MESHES <n>`, then each mesh:
  - `MESH <i>`, `NAME <name>` (to the line's end: one has a space,
    `salad bar`), `RADIUS <r>`;
  - `GEOMETRYMODE`, `LIGHTINGMODE`, `TEXTUREMODE`: 4 (textured), 3 (a
    plain colour), 0 (not drawn), 1 or 2 in a few;
  - `VERTICES <n>`: `<i>: <x> <y> <z> <intensity>`;
  - `TEXTURE VERTICES <n>`: `<i>: <u> <v>`, in texels of the face's
    material, past its edges to repeat it (`00crte.3do`'s sides run 0 to
    256 across a 128-texel texture, and v from 0 down to -191);
  - `VERTEX NORMALS`: one for each vertex, no count;
  - `FACES <n>`: `<i>: <material> <type> <geo> <light> <tex> <extralight>
    <n>` then `n` corners, each `<vertex>, <texture vertex>`, with or
    without a space after the comma;
  - `FACE NORMALS`: one for each face.

A mesh drawn without a texture (geometry mode 3) has no texture
vertices, and its faces name texture vertex 0 all the same.

### HIERARCHYDEF

`HIERARCHY NODES <n>`, then a node a line:

| Column | What |
|---|---|
| flags, type | In hexadecimal; type marks the body's parts (`0x10` hips, `0x20` left leg, `0x40` right leg...) *(inferred)* |
| mesh | Into the first geometry set's meshes; -1 for none (the dummy nodes at the top) |
| parent, child, sibling | Other nodes; -1 for none |
| numChildren | How many children |
| x, y, z | Where the node is, in its parent's space |
| pitch, yaw, roll | How it's turned, in degrees |
| pivotx, pivoty, pivotz | Where its mesh is, about the node: the joint it turns on |
| hnodename | To the line's end (one has a space) |

## Behaviour

A node is placed at its position in its parent's space, then turned:
roll about y first, then pitch about x, then yaw about z. Its mesh's
vertices are moved by the pivot before that. Kyle's, the stormtroopers'
and the other characters' meshes all meet at their joints this way, and
come apart otherwise: drawn in the data viewer.

## In Reluva

`parseModel` (`src/engines/sith/formats/model.h`) reads one into a
`Model`. `poseNodes` and `drawModel` (`src/engines/sith/render/draw3d.h`)
place the nodes and draw the first geometry set in software, textured
through a colormap's light levels, lit from one direction. The data
viewer's Models page shows every model. `reluva-tool sith formats
<install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 477 files and their column comments;
  the editing community's published write-ups of the format, which agree;
  the models drawn in the data viewer.
- **How it was checked:** every model read by `reluva-tool sith
  formats`; the characters drawn with their joints meeting;
  `sith.unit.models` on a model made by the test, with each quirk above.

## Open questions

- The faces' type flags, the vertices' intensity, and the geometry modes
  1 and 2.
