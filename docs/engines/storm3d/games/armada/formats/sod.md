# Star Trek: Armada: models (`.sod`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/sod.cpp` · **Checked on:** the English retail 1.0 install's 273 models, every byte · **Confidence:** layout confirmed; meanings inferred unless marked

## Overview

Every 3D thing in the game is a model in `SOD/`: the ships and stations,
planets, moons and asteroids, weapons' effects, the starfield. 265 are in
`SOD/`, and 8 in `SOD/Software/`, for the software renderer by the
folder's name *(guessed)*.
A model is a tree of nodes: some are meshes, others mark places on it
(hardpoints, lights, the ship's systems) or hold the levels of detail.

The executable calls the renderer that draws them Storm3D. The files
come in seven versions, under two headers:

| Header | Version | Models |
|---|---|---|
| `StarTrekDB` | 1.0 | 1 (`Wviracod.SOD`) |
| `StarTrekDB` | 1.2 | 11 |
| `StarTrekDB` | 1.4 | 14 |
| `StarTrekDB` | 1.5 | 4 |
| `Storm3D_SW` | 1.6 | 118 |
| `Storm3D_SW` | 1.7 | 60 |
| `Storm3D_SW` | 1.8 | 65 |

They hold 7,680 nodes, 1,797 meshes and 125,803 triangles in all.

## Layout

Little-endian. A string is a 2-byte length and its characters, with no
zero after them. A transform is 12 floats: a 3 × 3 matrix by rows, then a
translation.

### Header

10 bytes, `StarTrekDB` or `Storm3D_SW`, then the version as a float
(`1.8` is `0x3fe66666`).

### Texture table (before 1.7)

A 2-byte count, then for each: a name, a texture's name, a float and 3
bytes. Of its 1,953 entries, the float is 0 in 1,923 and the bytes
are `02 01 02` in 1,811. What the table does isn't known: the meshes name their textures
themselves.

### Lighting materials

A 2-byte count, then for each: a name, 10 floats and a byte. The floats
look like three colours and a power: the ambient, the diffuse, the
specular, and a power from 0 to 300, mostly 50 *(inferred)*; the byte is
0, 1 or 2. A
mesh's groups of faces name them (`base_borg`, `team_Bglow`); the
executable says it reads "a lighting material palette (material.sod)".

### Nodes

A 2-byte count, then for each (from 1.2):

| Field | |
|---|---|
| 2 bytes | Its type |
| string | Its name |
| string | Its parent's name, or empty for a root |
| 12 floats | Its transform in its parent |
| by type | Its data |

| Type | Nodes | What, by their names | Data |
|---|---|---|---|
| 0 | 3,453 | Structure: `root`, `geometry`, levels of detail (`lod0`...), hardpoints (`hp20`), the ship's systems (`damage`, `crew`, `engines`, `life`, `sensors`, `shield`) | none |
| 1 | 1,795 | Meshes | [the mesh](#meshes) |
| 3 | 2,267 | Lights and markers: `redstrobe`, `whitestrobe`, `crew1`, `asteroid1`: the sprite tables' `@sprite_node` names ([spr.md](spr.md)) | none |
| 12 | 163 | Particle emitters: `steammed`, `plasmasml`, `contrail` | a string: a sprite table's `@emitter` name |

### Meshes

From 1.4, a mesh is:

| Field | |
|---|---|
| string | From 1.7: how it's drawn: `default` (871 meshes), `additive` (63), `alphathreshold` (4), `wormhole` (4), the sprite tables' `@tmaterial` names |
| string | Its texture, without folder or extension: `Bbuildng` ([textures.md](textures.md)) |
| 2 bytes × 3 | Its vertices, its texture coordinates, its groups |
| 12 bytes each | The vertices: x, y, z |
| 8 bytes each | The texture coordinates: u, v |
| per group | 2 bytes, its faces; a string, its lighting material; then its faces, each three corners of a vertex index and a coordinate index, 2 bytes each |
| 1 byte | 0 or 1 |
| 2 bytes, then that many × 2 bytes | A list of the mesh's vertices: empty but in `Zentity.SOD`, whose 28 small meshes each list every vertex once |

Before 1.4 (versions 1.2 and 1.0) a mesh is its texture's name, in 1.2
a lighting material's name, then three counts (vertices, polygons,
coordinates), the vertices, the coordinates, and polygons of 3 or 4
corners, each a 2-byte count and its corners' indices. Nothing follows.

### Animations (from 1.4)

A 2-byte count, then for each: a node's name, a 2-byte count of
keyframes, a length in seconds, 2 bytes (0 in all), and the keyframes,
each a transform. 301 have keyframes: a station building itself
(`Bconst.SOD`'s `poly1`, 100 keyframes over 3.3 seconds), shields, the
Omega particle.

### Texture animations (from 1.6)

A 2-byte count, then for each: a byte (4 in all 41), a node's name, a
sprite table's animation (`tex1x4`, `shldieanm`), and from 1.8 a float.

### Version 1.0

`Wviracod.SOD` lays its parts out otherwise: empty texture and material
tables, then its meshes, each with its name first, then a tree of nodes
each with a type, a name, a parent, a 4 × 4 matrix by rows (the
translation in its last row *(inferred)*), and its mesh's name.

## Behaviour

- **Transforms.** A node's 3 × 3 matrix applies to a point as a row, `p ×
  M`, then the translation; its parent's transform after that. The sprite
  nodes (lights and markers) should sit on their models: where this and
  the transposed reading place 828 of them more than 5% of the model's
  size apart, this reading puts them nearer its mesh in 630; the other in
  198, some as far as 1.4 times the model's size outside it.
- **Levels of detail.** The nodes named `lod<n>` hold the levels: in all
  83 models that have them, the higher the number, the more vertices.
- **Faces' winding,** which side faces out, isn't known yet.

## In Reluva

`SodModel` (`src/engines/storm3d/formats/sod.h`) reads every layout
above, and refuses a file with bytes left over or a face naming a vertex
its mesh hasn't.

- **The data viewer's Models page** (`ModelPage`,
  `src/engines/storm3d/pages/model_page.h`) draws each model turning, its
  most detailed level, with its textures and the lighting materials'
  diffuse colours, lit from one direction; `additive` and `wormhole`
  meshes are added over the rest. Both sides of every face are drawn,
  while the winding isn't known. Sprite nodes and emitters aren't drawn.
- `reluva-tool storm3d sod <game folder> <name.sod>`: a model's
  materials, nodes, meshes and animations.
- `reluva-tool storm3d sod-check <game folder>`: reads every model whole.

## Sources and checks

- **Where the facts come from:** the 273 models, read byte by byte, a
  version at a time: each field's place found from small files
  (`Wovremit.SOD`, 94 bytes) and checked on all; the node types' meanings
  from the names of the nodes that have them; the transforms' reading
  from where the lights land. Published readers of the format were not
  needed.
- **How it was checked:** `sod-check` reads all 273 to their last byte,
  every face's indices within its mesh. Models of each kind were drawn
  through the 3D renderer (Mesa's software Vulkan): a Borg starbase, a
  Federation and a Klingon battleship, the Kahless sword at each level,
  and a 1.2 model. The unit tests (`storm3d.unit.sod`) write models of
  1.0, 1.2, 1.6 and 1.8 and read them, and check the placing and the
  levels.

## Open questions

- The texture table's use, the lighting materials' byte, the meshes'
  flag and list of vertices, the animations' 2 bytes, and the texture
  animations' byte and float.
- 14 textures that meshes name aren't in the install (`nebu4`, `soft`,
  `Mplanet`), and 134 meshes name none: the game may give those their
  textures otherwise.
- Which way faces wind, so which side the game culls.
