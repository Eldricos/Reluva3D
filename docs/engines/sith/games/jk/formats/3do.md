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
  - `GEOMETRYMODE`: 4 (textured), 3 (a plain colour), 0 (not drawn), or
    1 (points) in `sphere2.3do`'s two meshes; 2 (lines) in none: see
    [Geometry modes](#geometry-modes). `LIGHTINGMODE`: 3, 2 or 0.
    `TEXTUREMODE`: 0 or 1;
  - `VERTICES <n>`: `<i>: <x> <y> <z> <intensity>`; the intensity is 0
    in all but 58 vertices of `01frt.3do`, `16grnd.3do` and `boost.3do`
    (0.3 to 1): see [Vertex intensity](#vertex-intensity);
  - `TEXTURE VERTICES <n>`: `<i>: <u> <v>`, in texels of the face's
    material, past its edges to repeat it (`00crte.3do`'s sides run 0 to
    256 across a 128-texel texture, and v from 0 down to -191);
  - `VERTEX NORMALS`: one for each vertex, no count;
  - `FACES <n>`: `<i>: <material> <type> <geo> <light> <tex> <extralight>
    <n>` then `n` corners, each `<vertex>, <texture vertex>`, with or
    without a space after the comma. The type is flags in hexadecimal:
    see [Face flags](#face-flags). Geo is 4, 3, or 1 in `sphere.3do`'s
    faces;
  - `FACE NORMALS`: one for each face.

A mesh drawn without a texture (geometry mode 3) has no texture
vertices, and its faces name texture vertex 0 all the same.

### HIERARCHYDEF

`HIERARCHY NODES <n>`, then a node a line:

| Column | What |
|---|---|
| flags, type | In hexadecimal; type marks the body's part: `0x1` torso, `0x2` left arm, `0x4` right arm, `0x8` head and neck, `0x10` hips, `0x20` left leg, `0x40` right leg, by the nodes' names *(inferred)*. An animation's `TYPE` is matched against it ([key.md](key.md#priorities-and-body-parts)) |
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

### Geometry modes

A mesh is drawn with each face's geometry, lighting and texture modes
cut to the mesh's, and to the renderer's own limits (`0x4465a0`); a
mesh of geometry mode 0 isn't drawn at all (`0x44612c`) *(from the
code)*. A face's texture mode is drawn as a level surface's: 0 affine,
1 and above corrected for perspective, as *Setup*'s "Texture" detail
allows ([jkl.md](jkl.md#texture-mode)). The software renderer draws them so (`0x43c46d`, and `0x43cf89`
in 16-bit colour):

| Mode | Drawn as | Confidence |
|---|---|---|
| 0 | Nothing | from the code |
| 1 | Points: a pixel at each corner, in the colour at byte 4 of the face's cel ([mat.md](mat.md)) (`0x44e150`) | from the code |
| 2 | Lines: the face's outline, corner to corner, in that colour (`0x44e200`) | from the code |
| 3 | A plain colour | confirmed |
| 4 | Textured; as 3 where the face's cel is a colour, not a texture (`0x43c440`) | from the code |

The Direct3D renderer draws only modes 3 and 4 (`0x43b6f1`), so points
and lines show only in software *(from the code)*. No retail model is
drawn in them: `sphere.3do` and `sphere2.3do` are named by no level or
template.

### Face flags

The faces having each flag are counted in the retail files; what it
does is read from the code:

| Flag | Faces | What | Confidence |
|---|---|---|---|
| `0x1` | 3,508 in 123 models | Drawn from behind too. Without it, a face turned away from the camera is skipped while the renderer's culling setting (`0x889f00` bit `0x1`) is on (`0x446548`) | from the code |
| `0x2` | 14 in 4 models | See-through: drawn at alpha 90 of 255 in Direct3D (`0x43b428`), and by the software renderer's translucent routines (`0x43c559`, `0x43c59a`) | from the code |
| `0x4` | 129 in 8 models: `tieb.3do` 57, `slab1.3do` and `slab2.3do` 15 each, `e2x4.3do` 12, `00tabl.3do` and `01tabl.3do` 11 each, `e4x4_2.3do` 6, `14d8x8.3do` 2 | Kept with the face, and read by nothing in the game: only the format's writer (`0x4450a1`), which nothing in `JK.EXE` calls, reads it. A level's surfaces have it cleared on loading ([jkl.md](jkl.md#face-flags)); a model's keep it. What it meant to the tool that wrote the files can't be told from the game, and changes nothing in it | from the code |

The flags are kept at byte 4 of the face (`0x4446a6`) and go with it to
the renderer (`0x446857`), whose drawing routines read only `0x2`
(`0x43b428` Direct3D; `0x43c559` to `0x43ce31` in 8-bit colour and
`0x43d069` to `0x43d941` in 16-bit); collision with a model's faces
(`0x508400`, `0x508d20`) reads only their corners and normals *(from the
code)*.

### Vertex intensity

The light a vertex starts with, before the lights near it are added
(`0x44b750`), in lighting mode 3 (`0x44639a`, a light for each vertex,
blended across the face) *(from the code)*. Lights stop being added
once the sum reaches 1 (`0x44b825`). No other lighting mode reads it:
mode 2 lights the face as a whole from the lights near it (`0x4466be`)
*(from the code)*.

## In Reluva

`parseModel` (`src/engines/sith/formats/model.h`) reads one into a
`Model`. `poseNodes` and `drawModel` (`src/engines/sith/render/draw3d.h`)
place the nodes and draw the first geometry set in software, textured
through a colormap's light levels, lit from one direction; faces turned
away left out unless double-sided (`0x1`), and see-through ones (`0x2`)
drawn at 90/255 over the rest. The data
viewer's Models page shows every model. `reluva-tool sith formats
<install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 477 files and their column comments;
  the editing community's published write-ups of the format, which agree;
  the models drawn in the data viewer; `JK.EXE`'s loader (`0x443e80`,
  which reads the faces' type with `%x` and the vertices' intensity as
  their fifth number), its mesh and face drawing (`0x446110`,
  `0x446580`), its renderers (`0x43b1c0` Direct3D, `0x43c3c0`
  software), its collision with models' faces (`0x508400`), and a
  writer of the format (`0x444b90`) that nothing calls.
- **How it was checked:** every model read by `reluva-tool sith
  formats`; the characters drawn with their joints meeting;
  `sith.unit.models` on a model made by the test, with each quirk above;
  the modes, face flags and intensities counted across all 477 files
  with a script, and the files naming `sphere.3do` searched for in the
  archives. Readers of the face flag `0x4` were searched for across the
  whole program: every test of a mask with `0x4` on a field at byte 4,
  each reader of the faces' array (drawing, collision, the writer) and
  of the renderer's copy of the flags; none was found, and no call to or
  pointer at the writer.

## Open questions

None.
