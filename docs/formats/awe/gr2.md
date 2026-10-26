# Granny 2 files (.gr2)

RAD Game Tools' Granny 2 format: 1,803 files under `Models/<who>/`. Each
character folder has a model (`poirot_model.GR2`; some also have a
detailed `_model_1024`) and one file per animation (`poirot_walk.GR2`),
plus sounds tied to animations (`poirot_walk.wav`). They were exported
from 3D Studio MAX 8 by the "Granny Standard Exporter, SDK version
2.5.0.10" (the files say so).

## How this was worked out

- The container's layout is described publicly (the opengr2 wiki and the
  RagnarokFileFormats notes). It was checked against `poirot_model.GR2`
  and `poirot_walk.GR2`: each section's offsets and sizes, and each fixup
  table ending where the next block starts.
- Sections are compressed (Oodle1), and the textures inside are
  Bink-compressed. Neither is documented. Both are decoded by the game's
  own `granny2.dll`, run in Reluva's x86 interpreter
  (`src/engines/awe/granny_dll.cpp`). Two of its exports are called, with
  the arguments Granny's public SDK header gives:
  - `GrannyDecompressData(Format, FileIsByteReversed, CompressedSize,
    Compressed, Stop0, Stop1, DecompressedSize, Decompressed)`;
  - `GrannyBinkDecompressTexture(Width, Height, Flags, CompressedSize,
    Compressed, DestLayout, DestStride, Dest)`.

  The flags were taken from the DLL's own call to it in
  `GrannyCopyTextureImage`: the texture's `SubFormat`, plus 1 if its
  layout has alpha. Its start-up asks for the Windows directory, which
  Reluva provides.
- The results were checked by eye. Poirot was rendered with his three
  textures, posed by the walk animation at 0.3 s
  (`reluva-tool gr2-model ... -anim ... -obj`).

## Container

All little-endian, with 32-bit pointers:

| Offset | What |
|--------|------|
| 0x00 | magic, 16 bytes: `b8 67 b0 ca f8 6d b1 0f 84 72 8c 7e 5e 19 00 1e` |
| 0x10 | header size (0x1b8), then 3 reserved words |
| 0x20 | format version: 6 |
| 0x24 | file size |
| 0x28 | CRC |
| 0x2c | where the section table is, from 0x20 (0x38) |
| 0x30 | section count (8) |
| 0x34 | root type: section, offset |
| 0x3c | root object: section, offset |
| 0x44 | type tag (0x80000013) |
| 0x48 | 4 words, 0 |

Each section entry is 11 words: compression (0 none, 1 Oodle0, 2
Oodle1), data offset, compressed size, size, alignment, two "stops"
(where the compressor's three parts end), fixups offset and count, and
mixed-marshalling offset and count (for byte swapping, unused here).

The fixups come straight after the header, uncompressed. Each is three
words: where a pointer is in its section, then the section and offset it
points at. Reluva lays the sections out one after another in one block,
from address 16 so that 0 stays null. It writes each fixed-up pointer as
an address in that block.

## The data describes itself

The root object comes with a type: a list of members, each 32 bytes
(type, name pointer, referenced type pointer, array width, three extra
words, one ignored), ending with type 0. The member types are Granny's:
inline, reference, reference to array, array of references, variant
reference, reference to variant array, string, transform, real32, the
8- and 16-bit integers (signed, unsigned, normalised), int32, uint32,
real16 and empty reference. Their sizes (with 32-bit pointers) are in
`gr2ValueSize()`. Members follow one another without padding.

The root is Granny's file info: `ArtToolInfo`, `ExporterInfo`,
`Textures`, `Materials`, `Skeletons`, `VertexDatas`, `TriTopologies`,
`Meshes`, `Models`, `TrackGroups`, `Animations`. `reluva-tool gr2-info -t`
prints the whole tree.

## What the game's files hold

- **Units:** `UnitsPerMeter` 100; Z up, as in MAX. Poirot is about 400
  units tall.
- **Skeleton:** 89 bones for Poirot (a Biped rig: `Bip01...`), each with
  its parent, its transform at rest (position, quaternion, 3×3
  scale/shear) and the inverse of its world transform.
- **Meshes:** vertices of `Position` (3 × real32), `BoneWeights` (4 ×
  normal uint8), `BoneIndices` (4 × uint8, indices into the mesh's
  `BoneBindings`, which name the bones), `Normal` and
  `TextureCoordinates0`. 32-bit indices, in groups by material. Poirot
  has 14 meshes: his body (4,346 vertices), his head, and twelve
  untextured boxes for the face's bones (eyes, brows, lips, corners of the
  mouth). The game presumably doesn't draw the boxes; the viewer hides
  them.
- **Textures:** inside the model file, 512×512, `Encoding` 3 (Bink), 3
  bytes per pixel, no alpha.
- **Animations:** one per file: name, duration (walk: 0.8 s), time step
  (1/60), and a track group of transform tracks, one per bone. The 2.5
  exporter writes plain curves: `Degree` (2 here), `Knots` (times) and
  `Controls` (3 values per knot for position, 4 for orientation, 9 for
  scale/shear), not the compressed curve formats of later Granny versions.
  Reluva evaluates them as B-splines with one knot per control, each knot
  ending its control's interval (`Curve::evaluate`). This is checked by
  eye only so far.

## Still to do

- Decode Oodle1 and the Bink textures natively, checked byte for byte
  against the DLL on every file. The interpreter takes about 2.5 seconds
  per 512×512 texture.
- Check the curve evaluation against Granny's on more animations.
- The face: how the game drives the face bones (the `_f*` animations,
  `blink`, `eyeleft`...).
