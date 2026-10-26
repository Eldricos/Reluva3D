# LEGO Alpha Team: `.FIN` models

Source: the 147 `.FIN` files in the English retail `Fin.pac` (October 2026).
This is how the layout was worked out:

- **Splitting the files into objects.** The objects were found by their
  headers: a class name, then a value used once as an id and linked to
  from elsewhere. Every candidate for the 27 class names in the table
  below behaves that way; the other `Ni…`/`DD…` strings are object names.
- **Each class's fields.** These were found by comparing many objects of
  the class and decoding a few by hand: the 4-object `OG9997.FIN` (a box)
  first, then larger ones.
- **The rule for the field before the collision volume.** It came from
  where 1 to 5 fields failed to fit.
- **Testing each hypothesis.** Each one was tested by whether every object
  of the class then ended exactly where the next began. All 20 classes that
  are read now do so for every object in every file. `reluva-tool fin-check`
  repeats the check, and also checks that every link points at an object of
  the right kind and that every image named is in the archives.

The NifTools project's `nif.xml` (GPL-3), which describes later NetImmerse
and Gamebryo files and some of these early versions, was used for the
meaning of fields once their place was found: the names of the
bounding-volume types and of the alpha blend factors. Where it disagrees
with these files (a capsule is 28 bytes here, not 32, for example), the
files win.

`src/engines/netimmerse/fin.{h,cpp}` reads them.

## The file

- The line `Dweezil 23` and a newline, where NetImmerse's own files have
  their own header text.
- Then the objects, one after another, each:
  - its class name: a 32-bit length, then the characters;
  - a 32-bit id: the object's address in memory when the file was saved,
    unique within the file. Links between objects are these ids, and 0
    means none;
  - its fields, with no padding and no size, so a reader has to know
    every class to find the next one.
- The objects with no parent are each preceded by the string
  `Top Level Object` (length-prefixed like a class name). These are the
  model's roots, usually one.
- The file ends with the string `End Of File`.

All numbers are little-endian; floats are 32-bit IEEE.

"A pointer" below means a 32-bit word that was a memory address when the
file was saved: 0 means the data after it is left out, anything else that
it follows.

## Shared fields

**NiObjectNET** (every class read except `DDActorSharedData`):
- the name (a length and characters, often empty);
- the number of extra-data objects, then each written in place: its class
  name and its fields. Two kinds are seen:
  - `Ni3dsPropAnimExtraData`, on materials: a number and a link to its
    animator;
  - `TexturePropExtraData`, on textures: a number, a link to its
    `NiFlipTextures`, and a number;
- a byte: 0, but 1 on `NiImage`.

**NiAVObject** (nodes and shapes), after those:

| Field | Size |
|---|---|
| translation | 3 floats |
| rotation, row by row; applied to column vectors | 9 floats |
| scale | float |
| velocity | 3 floats |
| number of properties, then their links | 4 + 4 each |
| a number, 1 to 5; meaning unknown | 4 |
| a pointer to the collision volume; 0: none | 4 |
| the collision volume, if there is one | below |

An object's place in its parent is `translation + rotation × (scale ×
point)`. This was checked by drawing the characters, whose parts only fit
together this way.

**Collision volumes:** a 32-bit type, then:

| Type | Name (nif.xml) | Fields |
|---|---|---|
| 0 | sphere | centre, radius (4 floats), a byte |
| 1 | box | centre, three axes, three extents (15 floats), a byte |
| 2 | capsule | 7 floats, a byte |
| 4 | union | a count, then that many volumes |
| 5 | half-space | a plane (4 floats) |
| 6 | (not in nif.xml) | a count, then that many volumes |

The levels' `DDEnv` roots hold most of them: unions of up to 80 boxes and
capsules, which are the level's walls and floors.

## Nodes

**NiNode:** NiAVObject's fields, then two numbers and a byte (always 2, 0
and 1 or 0; meaning unknown), the children (a count and links), and the
effects (a count and links; always 0 here).

What each subclass adds after NiNode's fields:

| Class | Count | Adds |
|---|---|---|
| `NiBillboardNode` | 909 | a number (1): the billboard mode |
| `NiLODNode` | 64 | 5 bytes, the number of levels (one per child), 20 bytes for each level (its distances), a byte |
| `Ni3dsAnimationNode`, `Ni3dsBone` | 860, 213 | 48 bytes of animation settings |
| `DDUnit` | 93 | a link, *guess*: to the model's `DDActorSharedData` |
| `DDEnv` | 54 | a link, as `DDUnit`'s; the root of every level |

A level's sky is under a node named `Exterior` (holding `Backdrop`).

## Shapes

**NiTriShape** (13,226) and **NiEnvMappedTriShape** (13, which adds
nothing): NiAVObject's fields, then the geometry, in this order:

| Field | |
|---|---|
| number of vertices | 16 bits |
| a pointer, then the vertices | 3 floats each |
| a pointer, then the normals | 3 floats each |
| the bounding sphere: centre and radius | 4 floats |
| number of triangles | 16 bits |
| number of texture coordinate sets | 16 bits |
| a pointer, then each set's coordinates | 3 floats (u, v, w) per vertex |
| a pointer, then the vertex colours | 4 floats (r, g, b, a in 0..1) per vertex |
| a pointer, then a plane per triangle | 4 floats each |
| the triangles | three 16-bit indices each |

Every index is below the number of vertices, in every file. A level's
shapes have two coordinate sets: the first for the texture, which tiles,
and the second, in 0..1, for the lightmap. Texture coordinates are used
as they are: v runs down the image, as the TGA files are decoded (the
minifigures' faces show the right way up).

**DDCorona** (215): a glow. A shape's fields up to the vertex colours
(5 vertices: a square and its centre), then a link and 9 floats of its own
instead of planes and triangles.

## Properties

Each is NiObjectNET's fields, then:

| Class | Count | Fields |
|---|---|---|
| `NiMaterialProperty` | 3,728 | ambient, diffuse, specular, emissive (3 floats each), glossiness, alpha |
| `NiAlphaProperty` | 2,278 | a byte (blending on), source and destination factors (6 and 7 in every file: source alpha and one minus source alpha) |
| `NiTextureProperty` | 2,068 | a number (0), the number of images, their links: one, or several frames for a `NiFlipTextures` to cycle |
| `NiMultiTextureProperty` | 582 | a count (1), a link to an image (a lightmap from `LMap.pac`), 6 numbers (1, 0, 1, 0, 1, 1) |
| `NiTextureModeProperty` | 8,967 | 3 numbers (3, 0 to 2, 0 to 3) |
| `NiVertexColorProperty` | 285 | a number (1) |
| `NiZBufferProperty` | 215 | two bytes (0 and 0; *guess*: no depth test or write) |
| `NiSpecularProperty` | 61 | a byte (1) |
| `NiShadeProperty` | 5 | nothing more |

The viewer applies a property to the object it's on and everything under
it, unless something nearer has its own of the same class. That's how
NetImmerse's scene graph is generally described, and the models look
right drawn that way.

**NiImage** (3,372): NiObjectNET's fields (its byte is 1), the file name (a
length and characters, e.g. `CorrBlk.tga`) and a number (7). No pixels
are stored in the models. All 1,260 names are in `Map.pac`, `LMap.pac` or
`Etc.pac`.

## Not read yet

`Ni3dsColorAnimator` (439), `Ni3dsAlphaAnimator` (393), `NiFlipTextures`
(282), `Ni3dsMorphShape` (122), `NiLight` (110), `Ni3dsSkin` (82) and the
game's `DDActorSharedData` (147, up to 658 KB; it doesn't start with
NiObjectNET's fields). The reader finds them and skips them. Their links
are kept, so animation can be added later without changing the rest.
