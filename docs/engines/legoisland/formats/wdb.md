# LEGO Island: the world database (`WORLD.WDB`)

> **Status:** decoded · **Code:** `src/engines/legoisland/formats/wdb.cpp` · **Checked on:** the English 1.0 release's `WORLD.WDB` (9,944,860 bytes), every part and model read to its end, and the models drawn (November 2026) · **Confidence:** layout confirmed; what the fields mean inferred unless marked

## Overview

`WORLD.WDB` ("world database") holds most of the game's 3D models and
many of its textures *(published)*. It's one of the few files the
installer copies to the hard disk, in `lego\data\`
([systems/game-files.md](../systems/game-files.md)), and the game reads
it from there rather than streaming it as it does the SI files
*(published)*: it has none of their chunks. The 1.0 release has one, of
9.9 MB.

It lists the game's 20 worlds (`ACT1`, `HOSP`, `Isle`, `TestWorld`...),
each with the parts and models it loads. Then come 43 textures and one
part every world shares (the characters' faces, bodies and hats, and the
plants), and then each part's and model's data, which the worlds point
to.

A **model** is a thing in a world: a building, a vehicle, a brick. It
holds an animation, a tree of **ROIs** ("real-time object instances"),
and its own textures. An ROI is one thing drawn: its **LODs** (levels
of detail), least detailed first, each a set of points and the **meshes**
drawn with them, one colour or texture each. A **part** holds ROIs that
models share (a steering wheel, a door, a hat), without children
*(published)*.

## Layout

All values little-endian; floats are 32-bit. A string is a 32-bit
length and that many bytes: in the worlds' list its last byte is a NUL,
counted in the length; elsewhere there's no NUL.

```text
u32 worlds, and each world             the list of worlds
u32 size, then the shared textures     a texture list
u32 size, then the shared part         a part
each part's and model's data           where the worlds say
```

The worlds' list and the two shared blocks take the file's first
1,182,363 bytes; the parts' and models' data follow, one after another,
to the end of the file.

### A world

| Size | Field |
|---|---|
| string | name: `ACT1`, `BLDD`, `HOSP`, `Isle`... |
| 4 | parts |
| each | a part: its name (string), its data's size and its offset in the file (4 each) |
| 4 | models |
| each | a model, below |

| Size | Field |
|---|---|
| string | name: `ambul`, `Pizza`, `isle`... |
| 4 | its data's size |
| 4 | its offset in the file |
| string | the class that presents it: `LegoEntityPresenter` (409 entries), or empty (76) |
| 12 | location: 3 floats |
| 12 | direction: 3 floats |
| 12 | up: 3 floats |
| 1 | 1 for the island's buildings, terrain and race tracks (168 entries), 0 for the rest (317) |

The location, direction and up vector place the model in its world
*(published)*. Worlds share models and parts: the 20 worlds list 485
models and 107 parts, which are 197 models' names (191 sets of data:
six, such as `Jail` and `jail`, are the same data under two spellings)
and 58 parts.

### A texture list

A count, then each texture:

| Size | Field |
|---|---|
| string | name, as meshes give it: `dogface.GIF`, `^ROCKX.GIF` |
| 4 | width, in pixels |
| 4 | height |
| 4 | colours in the palette: 2 to 256 on the CD |
| 3 each | the palette: red, green, blue |
| width × height | the pixels: an index into the palette each, the top row first |

The textures are 8-bit pictures with their own palettes *(published)*.
Every pixel's index is below the palette's count. They are 4 by 4 to
256 by 256, each side a power of two. A texture whose name starts with
`^` (19 on the CD, all of the ground: rock, sand, grass, water, the
cave's walls) has a second picture after the first, with no name: its
width, height, palette and pixels, as above. Each second picture has 50
colours, and is the first's size or twice it: `^ROCKX.GIF` is 128 by
128, then 256 by 256. Meshes name them without the `^` (`ROCKX.GIF`).

The shared textures' block holds 43, each 4 by 4 to 128 by 128: the
characters' faces (`dogface.GIF`, `Infoface.GIF`) and shirt fronts
(`Nickchst.GIF`). Its size counts the count and the textures.

### A part

Offsets in a part are from its start (for the shared part, from after
its size).

| Size | Field |
|---|---|
| 4 | the offset of its texture list |
| 4 | ROIs: 1 in every world's part, 58 in the shared part |
| each | an ROI: its name (string), its LODs' count (4), the offset of the end of its LODs (4), and its LODs |
| ... | its texture list, at that offset |

### An ROI's LODs

Each LOD:

| Size | Field |
|---|---|
| 4 | 8 in the first LOD of 71 ROIs, all of them characters' parts (heads, hats, bodies); 0 in every other |
| 4 | meshes. A LOD of no meshes (41, all in models: cameras, targets) ends here |
| 2 | vertices |
| 2 | normals, twice over: always even |
| 4 | texture coordinates |
| 12 each | the vertices: x, y, z |
| 12 each | the normals: x, y, z, of length 1 or 0 |
| 8 each | the texture coordinates: u, v |
| each | a mesh, below |

The points are in Direct3D's left-handed space, in the ROI's own
coordinates.

### A mesh

| Size | Field |
|---|---|
| 2 | triangles |
| 2 | corners: different (vertex, normal) pairs the triangles use |
| 4 × 3 each | the triangles' corners, below |
| 4 | texture coordinate indices: 0, or one for each triangle corner |
| 4 each | those indices, below |
| 3 | colour: red, green, blue. `ff ff ff` in most textured meshes |
| 4 | transparency, a float: 0 in most; 0.6 in coloured "glass" materials, 0.8 in white ones, 0.34 in 12, 1.0 in 3 named `POLICE` |
| 1 | shading: 0 flat, 1 smooth. Every material whose name ends in ` F` or `FLAT` has 0 |
| 1 | 10 in 842 meshes, all of plain materials (`LEGO BLUE`, `LEGO WHITE`...), else 0 |
| 1 | 1 in 310 meshes, else 0 |
| 1 | 1 in the 208 meshes whose material is named `INDIR...`, else 0 |
| string | the texture's name, or empty |
| string | the material's name: `LEGO RED`, `INH LEGO RED F`, `PIZZA PIE`, `WINDSHLD`... |

A triangle corner is two 16-bit values. With bit 15 of the second set,
it's a new corner: the first is its vertex and the second's other bits
its normal, and the mesh's corners are numbered in the order they come.
With it clear, it's a corner already given, the first value its number;
the second is the same number again. On the CD every new corner's vertex
and normal are in range, every back-reference is to a corner already
given, and the new corners are as many as the mesh says.

A textured mesh's texture coordinate indices run beside its triangle
corners, in the same order: for a new corner, the index of its texture
coordinates in the LOD's list; for a back-reference, the number of the
corner it names, again. So each corner has its own texture coordinates.
All 404 textured meshes are laid out this way: their 8,223 new corners
point within their LODs' lists, and their 11,001 back-references repeat
the corner's number. Read as one index for each new corner from the
list's start instead, 330 of them would point past it.

The transparency is drawn as one minus an alpha (the windscreens of the
vehicles come out see-through), and the shading, the material names and
the texture coordinates each fit how the models look drawn so, in the
data viewer *(inferred)*.

### A model

Offsets in a model are from its start.

| Size | Field |
|---|---|
| 4 | 19 in every model: a version, presumably *(guessed)* |
| 4 | the offset of its texture list |
| 12 | 1, 0 and 0 in every model: as an animation's header, with no actors and a length of 0 ([obj.md](obj.md#an-animation-ani)) |
| ... | its animation: a tree of nodes, below |
| ... | its ROI, and its children's, below |
| 4 | at the texture list's offset: textures |
| 4 | 0 in every model |
| each | a texture, as in a [texture list](#a-texture-list) |

An animation node:

| Size | Field |
|---|---|
| string | the name of the ROI it moves, in capitals: `DUNEBLD`, `PLATFORM` |
| 2, then 16 each | moves: a key, then x, y, z |
| 2, then 20 each | turns: a key, then a quaternion (w, x, y, z), of length 1 in all 168 |
| 2, then 16 each | scalings: a key, then x, y, z |
| 2, then 5 each | showings: a key, then 1 or 0, shown or hidden |
| 4 | children, each a node |

A key is 32 bits: its time in its low 24 bits, in milliseconds
*(inferred)*, and flags in its top 8: 1 in most, 0, 4 and 5 in a few.
Nearly every model's animation is a single key at time 0 for some of its
nodes, which places them: `Dunebld` (the room where the dune buggy is
built) moves `SHELF01` up to its place and its pieces onto it.

An ROI in a model:

| Size | Field |
|---|---|
| string | name: `ambul`, `dbbkxlY0` |
| 16 | its bounding sphere: centre x, y, z, and radius (0.001 for one with nothing of its own) |
| 24 | its bounding box: least x, y, z, then most; 888888 to -888888 for one with nothing of its own |
| string | `indir-f-` and its name in lower case in 41 ROIs, else empty |
| 1 | 1 if it takes a part's LODs, else 0 |
| 4 | LODs; only when the byte before is 0 |
| 4 | the offset of the end of its LODs; only when there are any |
| ... | its LODs |
| 4 | children, each an ROI as this |

An ROI that takes a part's LODs (152 on the CD, such as `sterb01` in
`carcomp`) names it: its name is the part's, or the part's with digits
after it (`sterb` for `sterb01`). Every one of them names one of the
worlds' parts so.

A model's root ROI is named after the model and has nothing of its own;
its ROIs, its children, are flat under it. Each is placed by the
animation node of the same name, in any case: that node's first move,
then its first turn, after its parent nodes' *(inferred: the models
drawn so come out whole, their parts where they belong)*. An ROI whose
name no node has stays where its points are.

## In Reluva

`WdbFile` (`src/engines/legoisland/formats/wdb.cpp`) reads the worlds,
the shared textures and the shared part when it opens the file, and a
part or a model when asked. A mesh's triangles come back as indices
into its corners, each corner with its vertex, normal and texture
coordinates. The data viewer has a page for the textures (each with its
palette) and one drawing the models and parts in 3D, each ROI placed by
its model's animation, those that take a part's LODs drawn with them,
with any LOD.

- `reluva-tool legoisland wdb-info <WORLD.WDB>`: the worlds, and every
  part and model read, with their counts.
- `reluva-tool legoisland wdb-list <WORLD.WDB>`: each world's parts and
  models, and where it puts the models.
- `reluva-tool legoisland wdb-show <WORLD.WDB> <part or model|shared>`: a part's or
  model's ROIs, LODs, meshes, animation and textures.
- `reluva-tool legoisland wdb-textures <WORLD.WDB> <out folder>`: every
  texture as a PNG file, named after the part or model it's in.

## Sources and checks

- **Where the facts come from:** the
  [legoisland.org wiki](https://legoisland.org/wiki/WORLD.WDB) for what
  the file is and holds: worlds listing models and parts, a texture list
  and a part after them, textures with palettes, models placed by a
  location, direction and up vector, ROIs nested in models but not in
  parts *(published)*; it gives no layout. The layout was worked out from
  the file: its start read by hand, then every record of a kind walked
  by short scripts, and each field's values tabulated over all of them
  (the counts on this page are theirs). Each structure was taken as
  known once the offsets the file gives (a part's textures, an ROI's
  end, a model's textures, each part's and model's size) were all met
  exactly. The texture coordinate indices were read beside the triangle
  corners after the textures of a few small meshes (`pizhat`'s pizza,
  `racestnd`) fitted one rule.
- **How it was checked:** `WdbFile` reads every part and model on the CD
  to its end, meeting every offset (`reluva-tool legoisland wdb-info`:
  58 parts and 191 models, 770 ROIs, 1,083 LODs, 3,578 meshes, 277,033
  triangles, 205 textures, and 19 second pictures); the textures written
  out as pictures look as they should (faces, the jukebox, signs that
  read the right way round); and the models drawn by the data viewer
  come out whole: the ambulance, the pizzeria with its signs, the island
  in its sea, the dune buggy's pieces on their shelves.
  `legoisland.unit.wdb` reads a file built in the test, and fails on
  ones that are cut short or whose offsets are wrong.

## Open questions

- What the LOD's 8, the mesh's three bytes after the shading, the
  model's 1 and the 0 before its textures, and the world entry's last
  byte mean to the game.
- What the `indir-f-` names are for.
- What a `^` texture's second picture is for: the textures for close
  up, perhaps *(guessed)*.
- The keys' flags, and how the game plays a model's animation beyond its
  first keys.
- A transparency of 1.0 (the three `POLICE` meshes) would make them
  invisible: what the game does with it.
