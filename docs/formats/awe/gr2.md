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
  scale/shear). A few animations have compressed curves instead (below).
  Reluva evaluates them as B-splines with one knot per control, each knot
  ending its control's interval (`Curve::evaluate`). This is checked by
  eye only so far.

## Compressed curves

Most animations have the plain curves above. 36 of the 1,701 have
Granny's compressed curve formats instead: each curve holds a
`CurveData` variant whose type names the format. Every format starts
with a header of two bytes, `Format` and `Degree`. The DLL picks the
format's code by the `Format` byte, so Reluva does too
(`src/engines/awe/curves.cpp`). The other members are read by name from
the variant's own type.

The full install has 10,314 compressed curves:

| Format | # | Curves | Members after the header |
|--------|---|--------|--------------------------|
| `DaIdentity` | 2 | 2,902 | `Dimension` (int16) |
| `DaConstant32f` | 3 | 702 | `Padding` (int16), `Controls` (array of real32) |
| `D3Constant32f` | 4 | 3,188 | `Padding` (int16), `Controls` (real32[3]) |
| `DaK32fC32f` | 1 | 33 | `Padding` (int16), `Knots`, `Controls` (arrays of real32) |
| `D3K16uC16u` | 10 | 25 | `OneOverKnotScaleTrunc` (uint16), `ControlScales`, `ControlOffsets` (real32[3]), `KnotsControls` (array of uint16) |
| `D3K8uC8u` | 11 | 36 | the same, `KnotsControls` an array of uint8 |
| `D4nK16uC15u` | 8 | 8 | `ScaleOffsetTableEntries` (uint16), `OneOverKnotScale` (real32), `KnotsControls` (array of uint16) |
| `D4nK8uC7u` | 9 | 3,420 | the same, `KnotsControls` an array of uint8 |

The other formats the DLL knows (`DaKeyframes32f`, `D4Constant32f`,
`DaK16uC16u`, `DaK8uC8u`, `D9I1K16uC16u`, `D9I3K16uC16u`, `D9I1K8uC8u`,
`D9I3K8uC8u`) aren't used by the game. Reluva reports them as unread.

How each expands to knots and controls (dimension values a knot):

- **`DaIdentity`:** one knot at time 0. Its controls are the track's
  identity (position 0, the unit quaternion, the identity scale/shear),
  `Dimension` values of it.
- **`DaConstant32f`, `D3Constant32f`:** one knot at time 0, the
  controls as stored (as many as `Controls` has, or 3).
- **`DaK32fC32f`:** the knots and controls as stored. The dimension is
  the controls' count divided by the knots'.
- **`D3K16uC16u`, `D3K8uC8u`:** `KnotsControls` holds 4n values: n
  knots, then 3 values a knot. `OneOverKnotScaleTrunc` is the top 16 bits
  of a float (the low 16 are 0). The knot scale is 1 divided by that
  float, rounded to a float. A knot is its value times the knot scale. A
  control is its value times `ControlScales[c]`, plus `ControlOffsets[c]`.
- **`D4nK16uC15u`, `D4nK8uC7u`:** unit quaternions (x, y, z, w).
  `KnotsControls` holds n knots, then 3 values a knot (16- or 8-bit). The
  knots are as above, with the knot scale 1 / `OneOverKnotScale`. Each
  value is a 15- or 7-bit component, plus a top bit. The top bits of the
  second and third values say which component was left out (second × 2
  + third). The first value's top bit is that component's sign. The three
  stored components follow the one left out, wrapping round (left out y:
  z, w, x). Each is its value times a scale, plus an offset, and the one
  left out is the square root of 1 minus the sum of their squares,
  negated if the sign bit is set.

  The scales and offsets come from a table of 16 entries in the DLL
  (0x500551d8, scale then offset, floats). `ScaleOffsetTableEntries`
  picks one for each component, 4 bits each, x in the low bits. With h =
  √½ as a float (0x3f3504f3), entries 0-7 are (2h, -h), (h, -h/2),
  (h/2, -0.75h), (h/2, -h/4), (h/2, h/4), (h/4, -h/4), (h/4, -h/8) and
  (h/4, 0). Entries 8-15 are the same negated. A component's scale is the
  entry's times 1/127 (7 bits) or 1/32767 (15 bits), as floats
  (0x50055270, 0x50055268), rounded to a float.

The DLL computes in x87 at 53-bit precision, so Reluva computes in
doubles. It rounds to float where the DLL stores a float: the knot
scale, the components' scales, each knot and control. The sum of squares
uses the unrounded components. Fused multiply-adds would round once where
the DLL rounds twice, so `curves.cpp` is built without them.

### How this was worked out

- The member names and types come from the files' own type definitions
  (`reluva-tool gr2-info -t`).
- The DLL's code was read in its disassembly. `GrannyCurveGetKnotCount`,
  `GrannyCurveGetDimension` and `GrannyCurveExtractKnotValues` (0x5004fbc0
  → 0x50003e20) jump through a table at 0x50060620. It has an entry of 11
  words for each format, in `Format` order: the format's type, its name,
  then its functions (dimension at +8, knot count at +0xc, extract at
  +0x18). The extract functions read were 0x500040d0 (`DaIdentity`),
  0x50004180 (`DaConstant32f`), 0x500042b0 (`D3Constant32f`), 0x500044f0
  (`DaK32fC32f`), 0x500061d0 (`D3K16uC16u`), 0x50006690 (`D3K8uC8u`),
  0x50005250 (`D4nK16uC15u`) and 0x50005a20 (`D4nK8uC7u`). The knot
  counts are 0x500065a0 and 0x50005950 (4n values: n knots), 0x50004160
  (`DaK32fC32f`), 0x50004170 (constants: 1) and 0x5001d960 (identity:
  0).
- The check: `reluva-tool awe-check <game> -gr2` loads every animation,
  then has the DLL load the file too (`GrannyReadEntireFileFromMemory`)
  and expand each compressed curve with `GrannyCurveExtractKnotValues`,
  in Reluva's x86 interpreter. It compares degree, knots and controls bit
  for bit. On the full install (1,803 files) all 10,314 compressed curves
  in the 36 animations are the same. The DLL is used only for this check;
  the engine expands the curves itself.
- The expanded curves go to the same B-spline evaluation as the plain
  ones. `vendor01_walk` and `poirot_suprised` render posed as they should.

## Vector tracks

About 80 animations also have vector tracks: "Displacement Max
Triangles", "value", "TrackMasktrackmask". These are settings that 3D
Studio MAX animated, which the exporter kept. Nothing suggests the game
uses them, so they aren't read (`awe-check` counts them).

## Still to do

- Decode Oodle1 and the Bink textures natively, checked byte for byte
  against the DLL on every file. The interpreter takes about 2.5 seconds
  per 512×512 texture.
- Check the curve evaluation against Granny's (`GrannyEvaluateCurveAtT`)
  on more animations.
- The face: how the game drives the face bones (the `_f*` animations,
  `blink`, `eyeleft`...).
