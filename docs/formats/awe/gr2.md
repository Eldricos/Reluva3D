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
  Reluva provides. The textures are now decoded natively (see "Bink
  textures" below); the DLL's decoder is only called by `awe-check
  -textures`, to compare.
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
- **Textures:** inside the model files (Poirot's: three, 512×512),
  `Encoding` 3 (Bink). See "Bink textures" below.
- **Animations:** one per file: name, duration (walk: 0.8 s), time step
  (1/60), and a track group of transform tracks, one per bone. The 2.5
  exporter writes plain curves: `Degree` (2 here), `Knots` (times) and
  `Controls` (3 values per knot for position, 4 for orientation, 9 for
  scale/shear), not the compressed curve formats of later Granny versions.
  Reluva evaluates them as B-splines with one knot per control, each knot
  ending its control's interval (`Curve::evaluate`). This is checked by
  eye only so far.

## Compressed curves

Most animations have the plain curves above. About 30 of the 1,701 have
some of Granny's compressed curve formats: a `CurveData` variant whose
type names the format. The formats seen are `DaIdentity`,
`DaConstant32f`, `D3Constant32f`, `DaK32fC32f`, `D3K16uC16u` and
`D4nK8uC7u` (quaternions as 7-bit components, scaled by tables inside the
DLL).

The DLL expands them: `GrannyReadEntireFileFromMemory` loads the file,
then each such curve goes through `GrannyCurveExtractKnotValues`, which
gives its knots and controls. An identity or constant curve gives one
knot of its value. The curves are found in the DLL's memory with the
same members' offsets Reluva reads from the file's own type definitions,
since the loaded structures are those types. The expanded curves go to
the same B-spline evaluation as the plain ones. `vendor01_walk` and
`poirot_suprised` render posed as they should.

## Vector tracks

About 80 animations also have vector tracks: "Displacement Max
Triangles", "value", "TrackMasktrackmask". These are settings that 3D
Studio MAX animated, which the exporter kept. Nothing suggests the game
uses them, so they aren't read (`awe-check` counts them).

## Bink textures

The codec is Granny's own, not Bink video's. It is a wavelet codec
with a range coder. Reluva decodes it in
`src/engines/awe/bink_texture.cpp`.

### How this was worked out

From the disassembly of the game's `granny2.dll` (image base
0x50000000), starting at the export `GrannyBinkDecompressTexture`
(0x500502a0, which calls 0x5000fe70). The functions read:

| Address | What |
|---------|------|
| 0x5000fe70 | the decoder: flags, padding, planes, colours, output |
| 0x5000fb70, 0x50047ab0 | the size rounded up to 16 (only above 256 pixels) |
| 0x50047c40 | each plane: its coefficients, then four inverse levels |
| 0x5004d820 | one plane's coefficients |
| 0x50048090 – 0x50048220 | the range decoder |
| 0x5004db20 | the lowest band |
| 0x5004dec0, 0x5004e580 | a band of details, and its models |
| 0x5004e680, 0x5004eb00, 0x5004ea50, 0x5004ed60, 0x5004ee70 | the adaptive model: set-up, decoding, table, halving, search |
| 0x5004dab0 | the row flags |
| 0x5004ba20 | one inverse level, in strips |
| 0x5004a060, 0x5004ac20 | the 9/7 filter, rows and columns |
| 0x5004b7d0, 0x5004b910 | the two-tap filter, rows and columns |
| 0x5000f7c0 | planes to colours |

Tables: bit lengths at 0x5008a098, long runs at 0x50068fb4, long zero
runs at 0x50068fc4. Each stage was then checked on its own: a scratch
harness called these functions in the interpreter on the game's
textures and compared their output with Reluva's. The verification is
below.

### Flags

The decoder's flags are the texture's `SubFormat`, plus 1 if its layout
has alpha. Bit 0: an alpha plane. Bit 1: the planes are the colours
(each shifted right by 2) instead of luma and differences (0x5000f910).
Bit 2: this codec; without it, an older one (0x50012770). The game's
files only use 4 and 5 (`SubFormat` 4 without alpha, 5 with), so Reluva
decodes only those. Textures of 256 pixels or fewer are stored raw; none
are in the game.

### Layout

The width and height are rounded up to multiples of 16 (W, H); the
picture is the top-left part. The data is a 32-bit word (the size of
the work space the DLL's decoder needs; not used), then each plane: luma,
two colour differences, then alpha if there is one. A plane is two
words, the sizes of its range-coded bytes and of its raw bits, then
those bytes, then the bits. The raw bits are 32-bit little-endian words
read from the lowest bit up.

### Range decoder

`code` starts as the first byte >> 1, `range` as 0x80, and the first
byte's low bit is held back. The DLL holds that bit in the low bit of a
pointer, which starts as the stream's address plus the bit, so the
address's parity counts. It is taken relative to the texture data, which
the DLL is given aligned. Before each symbol, while `range` <= 0x800000:
`code = ((code * 2 + bit) << 7) | (byte >> 1)`, the new byte's low bit
is held back, `range <<= 8`.

- A symbol out of `total`: `r = range / total`,
  `v = min(code / r, total - 1)`. Then for the interval (start, size):
  `code -= r * start`; `range = r * size`, or `range - r * start` for
  the last interval.
- Out of `1 << n`: the same with `r = range >> n`.
- A uniform value out of `n`: the same with the interval (v, 1).

### Adaptive model

A model is made for a largest value: `n` = largest + 1 symbols. Index 0
is an escape; each value gets an index when it first appears. All counts
are 16-bit. It starts with the escape's count 4, total 4, threshold 8,
step 4, a limit `clamp(largest * 32, 0x100, 0x3b38)` and a step limit:
0x80 if `2 * largest` is below that, else `min(2 * largest, limit / 2 -
0x20)`.

To decode a symbol:

1. If the total has reached the threshold, rebuild the table. If the
   threshold has also reached the limit, first halve: the escape's count
   is halved; values with a count of 1 or less are dropped, the last
   value moving into their place; the others' counts are halved; the
   value with the highest count swaps with the last one; the escape's
   count becomes 1 if it is 0 and values can still be added. The table:
   `s = 0x20000 / total`; interval i starts at `0x8000 + sum of
   (count[j] * s) >> 3 for j < i`; the last one ends at 0xc000. Then if
   `2 * step` is above the step limit the threshold is `total + step
   limit`, else `step` doubles and the threshold is `total + step`.
2. Decode `v` out of 1 << 14 and find its interval (`v + 0x8000`). Its
   count and the total go up by 1.
3. Index above 0: its value. The escape: if values were added since the
   table was built, a uniform choice out of 2; 1 means one of them, a
   uniform choice out of how many; its count and the total go up by 2.
   Else a new value: its count and the total go up by 2, and the caller
   reads the value. When all `n` values are in, the escape's count is
   taken off the total and set to 0.

### Bands

Each plane is transformed in place, four levels deep. At each level the
low band sits in the left half of the columns and the even rows, the
details to its right (HL), below it on the odd rows (LH) and both (HH).
So the lowest band is W/16 × H/16 at rows 16 apart; the bands of the
next level are at rows 16 apart too, LH starting 8 rows down; and so on
to the last, W/2 × H/2 at rows 2 apart. They come in that order: the
lowest, then HL, LH, HH for each level from the smallest.

The lowest band: a bit; 1 means it is all one 16-bit value. Else its
largest difference (16 bits) and the first value (16 bits); the others
are a prediction plus a difference from a model of that size, with a
sign bit if it isn't 0 (a new value is sent as a uniform choice out of
`n`). The prediction is the value to the left on the first row, above at
the start of a row, else `(above + left) / 2` (rounded toward 0, with the
left value not cut to 16 bits).

A band of details: its quantizer (16 bits), then a bit; 1 means it is
all one value (16 bits) times the quantizer. Else its largest value (16
bits). There is a model of that size for each bit length 0 to `b`, where
`b` is the bit length of largest × quantizer (at most 15), one of 64
symbols for runs and one of 256 for zero runs. The first value is a
uniform choice out of `n` and a sign bit. Then pairs: a run (a new run
value is sent in 6 raw bits; 60 to 63 mean 0x80, 0x100, 0x200, 0x400),
then a zero run (new values in 8 raw bits; 0 is 0, 252 to 255 mean
0x200, 0x400, 0x800, 0xc00, plus 2, others plus 2). The run's values are
coded and the zero run's are 0, both across row ends. A coded value
comes from the model for the bit length of `(|left| + |above-left| +
|above| + |above-right|) >> 2`, at the end of a row `(2|left| +
|above-left| + |above|) >> 2`; with its sign bit, times the quantizer.
At the start of a row, left, above-left and above are all the value
above. After
a zero run inside a row, left is 0. On the first row the DLL's pointer
"above" trails the current one, so "above-right" is the value to the
left, "above" the one before it and "above-left" the one before that.

Last, H row flags from the range coder: how many are 0, a uniform
choice out of H + 1; then each flag, 0 if its symbol out of H is below
that count. A row whose flag is 0 has no horizontal details at the last
level.

### Inverse transform

Each level is rows first (from the band halves into a work copy), then
columns (from even and odd rows back into place). Rows of 12 or more
and columns of 10 or more use a 9/7 filter in 16.16 fixed point; the
smallest levels use two taps. With lows `L` and highs `H` of length m:

- Two taps: `x[2i] = half(2L[i] + H[i])`, `x[2i+1] = half(2L[i] - H[i])`,
  where `half(v)` is `(v + 1) >> 1`, or `(v - 1) >> 1` for v below 0. A
  row without details gives `L[i]` twice.
- 9/7: `x[2i] = R(0xc9da L[i] - 0xa6b (L[i-1] + L[i+1]) + 0x609d (H[i-1]
  + H[i]) - 0x61b (H[i-2] + H[i+1]))` and `x[2i+1] = R(0x6b08 (L[i] +
  L[i+1]) - 0x1086 (L[i-1] + L[i+2]) - 0xda4a H[i] + 0x1c52 (H[i-1] +
  H[i+1]) - 0x9af (H[i-2] + H[i+2]))`, with 32-bit wrapping sums and
  `R(v) = (v + 0x7fff) / 0x10000`, or `(v - 0x8000) / 0x10000` for v
  below 0 (rounded toward 0). A row without details leaves out the H
  terms. Past the ends the interleaved signal (L at even places, H at
  odd) is mirrored about its first and last samples: `L[-i] = L[i]`,
  `H[-1-i] = H[i]`, `L[m-1+i] = L[m-i]`, `H[m-1+i] = H[m-1-i]`.

Results are cut to 16 bits. The last level's rows use the row flags; the
others have none.

### Colours

With luma Y and differences U, V: `G = Y - (U + V) / 4` (rounded toward
0), `R = U + G`, `B = V + G`, alpha from the fourth plane, each clamped
to 0–255. Without an alpha plane the DLL's alpha is 0 (its planes start
zeroed in the interpreter), and Reluva gives the same.

### Verification

`reluva-tool awe-check <game> -gr2 -textures` decodes every Bink
texture of the install both ways and compares the RGBA bytes. Over the
full install (1,803 files) there are 278 Bink textures, from 64×64 to
1024×1024 (and 512×256, 1024×256): 251 with flags 4 and 27 with flags 5.
None differs. Run alone, a 512×512 texture takes about 11 ms natively
against 2.2 s in the interpreter, a 1024×1024 one 85 ms against 8.6 s.

## Still to do

- Decode Oodle1 natively, checked byte for byte against the DLL on every
  file.
- Check the curve evaluation against Granny's (`GrannyEvaluateCurveAtT`)
  on more animations.
- The face: how the game drives the face bones (the `_f*` animations,
  `blink`, `eyeleft`...).
