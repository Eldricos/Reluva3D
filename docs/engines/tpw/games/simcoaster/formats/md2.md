# SimCoaster: models (`.MD2`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/md2.cpp` · **Checked on:** the US release's 1,980 `.MD2` files · **Confidence:** confirmed unless marked

## Overview

Everything in the park the game draws in 3D is a model: rides, shops,
features, the advisor and the worlds' terrain (`levels/<world>/terrain/base.MD2`,
1.6 MB). They are inside the archives, a folder each beside the
model's settings (`.sam`), scripts (`.RSE`) and textures, as
`levels/arabian/features/atmdesk/atmdesk.MD2`. The format is Bullfrog's
own and shares nothing with Quake II's `.md2` but the extension.
No description of it had been published: OpenTPW's page for it is
empty. All that follows was worked out from the files.

Of the 1,980 files:

| Kind | How many | What it is |
|---|---|---|
| A model | 698 | Its meshes, textures and node tree, below |
| An animation | 1,209 | Keyframes for a model: header flag 0x20, and the same counts as the model it moves (`waterideb.MD2` and `wateridec.MD2` for `wateride.MD2`): [md2-animation.md](md2-animation.md) |
| A header alone | 71 | The header and its box, no tables: mostly 256 bytes, beside a model whose name they carry with an `m` added (`atmdeskm.MD2` for `atmdesk.MD2`) |
| Another version | 2 | `generic/dynamic/garrow.MD2` and `rarrow.MD2`: version 24, laid out otherwise; not read |

## Layout

All numbers little-endian; offsets are from the start of the file;
floats are 32-bit.

### The header

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | 0x1CD15D46 |
| 0x04 | 4 | The version: 221 (24 for the two arrows) |
| 0x08 | 4 | 203: not worked out |
| 0x0C | 8 | Not worked out: differ from file to file |
| 0x14 | 4 | Not worked out: a date, perhaps *(guessed)* |
| 0x18 | 20 | The file's name, as `atmdesk.MD2` |
| 0x2C | 4 | Near the file's size: up to 18 bytes short. Not worked out |
| 0x30 | 4 | Flags: 0x20 an animation; 1, 9, 13, 0, 3 and 5 for the others, not worked out |
| 0x34 | 2 × 12 | Counts for the whole file: at 0x36 the textures; 0x38 the blocks of positions (below); 0x3A the vertices, in fours; 0x3C the groups; 0x3E the faces; 0x44 the meshes; 0x4A the normals. The rest not worked out |
| 0x50 | 4 × 12 | Where tables start: at 0x50 the textures; 0x58 the first mesh's positions; 0x70 the first node. 0 in all of them for an animation or a header alone. The rest name the first mesh's tables again, and some not worked out |
| 0x80 | 24 | The whole model's box: least x, y, z, then most |

The header's counts are the sums of the meshes' own: its faces and
normals agree for 629 of the 698 models, its meshes for 625.

### The textures

At the header's offset (0x50), usually straight after the header
(0xB8), sometimes 4 bytes on (0xBC), for each texture 8 bytes: a flags
word, not worked out (1 and 3 most often, with 0x10, 0x20 and 0x40 set
on some), then 4 zero bytes. Then, for each, its file's name in 20
bytes, as `deskpillars.tga`.

### Finding the textures

The names end `.tga`, but a texture is most often an `.fsh` bank of the
same name. Of the 9,529 textures the models name, Reluva finds:

| Where | How many |
|---|---|
| The model's own `textures/` folder (`levels/arabian/features/atmdesk/textures/deskpillars.fsh`) | 3,826 |
| Its world's shared `sharetex/` (`levels/arabian/sharetex/`) | 5,059 |
| The model's `stexture/` | 58 |
| Its world's `ssharete/` | 3 |
| Only in another model's or another world's folders, as `generic/sweather/lightning.fsh` | 423 |
| Nowhere | 160 |

and looks in that order, a bank before a `.tga` of the name. `stexture/`
and `ssharete/` hold the same pictures smaller ([fsh.md](fsh.md)): for
a lower detail setting *(guessed)*, so last. Where the game looks for
the 583 not found so is not worked out; Reluva draws them grey.

### The nodes

The models are made of a tree of nodes, from the one at the header's
0x70. Each starts:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Its type: 1 or 0x401 a mesh; 0x200 a helper (`baseobject`, `Head01`, a camera); 4, 0x208, 0x300 and 0x600 others, not worked out |
| 0x04 | 4 | Its parent node, or 0 |
| 0x08 | 4 | The next node of the same parent, or 0 |
| 0x0C | 4 | Its first child, or 0 |
| 0x10 | 64 | A 4 × 4 matrix placing it in its parent's space: the translation in the last four floats, so as column-major and applied to a column vector |

A helper then has its number (4 bytes) and where its name is (4 bytes):
0x58 bytes in all.

The 281 nodes of type 0x401 are meshes as type 1's are, laid out the
same and drawn: the front end's hall has its right wall so
(`sidewallright`, [front-end.md](../systems/front-end.md)). What the
0x400 says isn't worked out.

The nodes' records lie in two arrays: the meshes' (0xA0 bytes each),
then the others' (0x58 bytes each), every node of the 698 models on its
array's stride. The animations number the nodes by their places in these
([md2-animation.md](md2-animation.md#a-tracks-record)). Not every record
is in the tree: the front end's model has a helper, `superparent`, that
its root nodes name as their parent but no node links to.

A mesh's place in the model is its matrix times its parent's, and so on
up the tree: placed so, the meshes of 646 of the 698 models fill the
header's box to within 1% of its size, against 343 placed by their own
matrices alone. Of the
others, some boxes have their axes swapped (`levels/science/features/water/water.MD2`)
and some are slightly off; what makes them differ is not worked out.

### A mesh

After its 0x50 bytes of node, a mesh has, from the node's start:

| Offset | Size | Field |
|---|---|---|
| 0x50 | 4 | A number, 0 upwards, as a helper has: what it counts is not worked out |
| 0x54 | 4 | Where its name is |
| 0x58 | 2 | How many positions |
| 0x5A | 2 | How many groups |
| 0x5C | 2 | How many faces |
| 0x5E | 2 | How many vertices |
| 0x60 | 4 | Where its positions are |
| 0x64 | 4 | Its normals |
| 0x68 | 4 | Its vertices' texture coordinates |
| 0x6C | 4 | Its groups |
| 0x70 | 4 | Its faces |
| 0x74 | 4 | 0 in all |
| 0x78 | 24 | Its positions' box, in its own space: least x, y, z, then most |
| 0x90 | 4 | 0 in all |
| 0x94 | 4 | Its vertices' positions |
| 0x98 | 8 | 0 in all |

0xA0 bytes in all.

- **Positions**: four to a block of 48 bytes, the four's x, then their
  y, then their z, the last block padded with zeros: laid out for
  four-at-a-time arithmetic, it seems *(guessed)*.
- **Vertices**: a vertex is a corner as drawn, with its own texture
  coordinates and normal; vertices at the same point share a position.
  For each, its position's number (2 bytes), and, in the other table,
  its texture coordinates: four vertices' to a block of 32 bytes, the
  four's u, then their v, as the positions are laid out. v runs up the
  texture: from its last row to its first.
- **Normals**: three floats each: one for each vertex, then one for each
  face. 27 meshes number their faces' normals past the end of this
  table, into what follows it.
- **Faces**: 8 bytes each: its normal's number, then its three
  vertices'.
- **Groups**: 16 bytes each, a run of faces drawn with one texture:
  where the texture's entry is (0 for none), the first face (2 bytes),
  how many (2 bytes), the lowest and highest vertex they use (2 bytes
  each), and a flags word: 1 on some groups of see-through textures
  (`glowinside.tga`), not worked out. One mesh's groups
  (`pbobsl.MD2`'s `Loft01`) cover fewer than its faces.

A few meshes have texture coordinates that can't be meant (not numbers,
or millions): 3 of the 7,262.

## In Reluva

`Model` (`src/engines/tpw/formats/md2.cpp`) reads a model's textures and
meshes, each placed by its nodes' matrices; it tells an animation and a
header alone from a model but reads neither. `reluva-tool tpw md2-check
<game folder> [model]` reads every model, or lists one's meshes and
their groups' textures, and counts the models whose meshes fill their
header's box. The game-files test runs it (`tpw.gamedata.models`).

`findTexture` (`src/engines/tpw/textures.cpp`) finds a texture as above.
The viewer's Models page draws each model in 3D (`src/engines/tpw/ui/model_view.cpp`,
in software): its meshes placed, textured, lit by each face's own
slope, the texels whose alpha is under half left out. The game's y is
up; the viewer's world has z up, and swapping the two axes also turns
the game's space, left-handed as Direct3D's, into a right-handed one:
drawn so, the front end's hall matches the recording, its paintings the
right way round ([front-end.md](../systems/front-end.md)). `reluva-tool tpw md2-shot <game folder> <model>
<out.png> [yaw pitch]` writes a model drawn so to a PNG.

## Sources and checks

- **Where the facts come from:** the US release's models (November
  2026). The node tree and tables by following their offsets across all
  1,980 files; the blocks of positions by the padding zeros at every
  fourth float of a mesh's last block; the vertices' position table by
  its values, never past the positions' count; the matrices' order by
  the header's box, which the meshes fill only when each is placed by
  its parents' matrices too; the texture coordinates' blocks by the
  faces' shapes: their edges' lengths in the texture against their
  lengths in the model vary less read in blocks in 4,215 meshes, read as
  pairs in 182 (838 can't tell), and read so, the hall's carpet is
  square tiles where pairs make streaks; v's direction by the hall's
  paintings, seen in the recording. Drawn flat-shaded from these, the ATM desk,
  the water ride with its statue, its ring and the bobsled with its rider
  come out whole, every face in place; drawn textured, the shops of the
  Arabian world are a chip box, a drinks bottle, a hot dog, a magic lamp
  and a balloon stand, on sand; the front end's hall matches the
  recording's frame.
- **How it was checked:** `reluva-tool tpw md2-check` reads all 698
  models with every index in range. The unit tests
  (`tpw.unit.formats`) read a model built in the test, and fail cut or
  broken ones.

## Open questions

- The animations' tracks of vertices and textures ([md2-animation.md](md2-animation.md)).
- The header's fields not named above, and its flags.
- The textures' flags, and the groups'.
- The node types other than meshes and helpers, and what type 0x401's
  0x400 says.
- Why some models' meshes don't fill the header's box.
- Where the game finds the textures Reluva doesn't.
- What the headers alone are for: a model's size before it loads,
  perhaps *(guessed)*.
