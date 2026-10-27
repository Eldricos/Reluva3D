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
  Bink-compressed. Neither is documented. Oodle1 was worked out from the
  disassembly of the game's `granny2.dll` (see "Oodle1" below) and is
  decoded natively. The textures are decoded by the DLL itself, run in
  Reluva's x86 interpreter (`src/engines/awe/granny_dll.cpp`). Two of its
  exports are called, with the arguments Granny's public SDK header gives:
  - `GrannyDecompressData(Format, FileIsByteReversed, CompressedSize,
    Compressed, Stop0, Stop1, DecompressedSize, Decompressed)`, now only
    to check the native decoder (`reluva-tool awe-check -oodle`);
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

In the game's files, section 5 is stored uncompressed and all the others
are Oodle1: 4,034 sections that aren't empty, in the 1,803 files. None is
Oodle0.

The fixups come straight after the header, uncompressed. Each is three
words: where a pointer is in its section, then the section and offset it
points at. Reluva lays the sections out one after another in one block,
from address 16 so that 0 stays null. It writes each fixed-up pointer as
an address in that block.

## Oodle1

Reluva's decoder is `src/engines/awe/oodle1.cpp`.

### How it was worked out

From the disassembly of the game's `granny2.dll` (image base
0x50000000), following `GrannyDecompressData` down:

| Address | What it does |
|---------|--------------|
| 0x50051d50 | `GrannyDecompressData`: calls 0x500248a0 |
| 0x500248a0 | by format: 0 copies, 1 calls 0x50035470 (Oodle0, not read), 2 calls 0x50035b70 |
| 0x50035b70 | Oodle1: the three parts, each set up then decoded to its stop |
| 0x50048090 | starts the range decoder |
| 0x500480f0 | the range decoder's renormalisation |
| 0x50048190 | a value in [0, n), equally likely |
| 0x50048220 | the same with the range shifted instead of divided (a bit) |
| 0x500481f0, 0x50048150 | where the code falls in 0x4000; taking a symbol off |
| 0x50048340 | the offset models' sizes from the window |
| 0x50049430 | sets up a part's models from its header |
| 0x50049600 | decodes one literal or match |
| 0x500497a0 | copies a match |
| 0x5004e680 | sets up a model |
| 0x5004eb00 | decodes a symbol from a model |
| 0x5004ea50 | rebuilds a model's cumulative table |
| 0x5004ed60 | halves a model's counts |
| 0x5004ee70 | the steps of the binary search in the cumulative table |

`reluva-tool awe-check <game> -oodle` decompresses every compressed
section of every file both natively and with the DLL (in the
interpreter) and compares them byte for byte; see "Verification" below.

### The stream

A section's compressed data starts with three part headers of three
little-endian words each (36 bytes). The output is in three parts: up to
`stop0`, up to `stop1`, and up to the section's size. Each part sets up
its models afresh from its header, then decodes literals and matches
until the output reaches its stop (a match may run past it; the next
part goes on from where it ended). All three parts read one range-coded
stream, which starts after the headers. Reads past the end of the data
give 0.

(The DLL can also byte-swap the header words of a byte-reversed file.
The game's files aren't.)

### The range decoder

State: `code`, `range`, and one held-back bit. It starts with the first
byte `b`: `code = b >> 1`, the held bit `b & 1`, `range = 0x80`.

Before each read it renormalises: while `range <= 0x800000`, the next
byte `b` gives `code = code << 8 | held << 7 | b >> 1`, the held bit
becomes `b & 1`, and `range <<= 8`. (So the code runs one bit behind the
bytes.)

Three kinds of read, all with a step `s`, a count `n` and a value `q`:

- a value in [0, n): `s = range / n`;
- a bit: `s = range >> 1`, `n = 2`;

  for both, `q = min(code / s, n - 1)`, `code -= q * s`, and `range` is
  `s` unless `q` is the last value, which takes the rest:
  `range -= q * s`;
- a model's symbol: `s = range >> 14`, the code falls at
  `min(code / s, 0x3fff)`; the symbol found there, with cumulative
  frequency `low` and frequency `f` out of 0x4000, is taken off:
  `code -= s * low`, and `range = s * f`, or `range -= s * low` if the
  symbol is the last (`low + f` reaches 0x4000).

### The models

Each model has a capacity (how many distinct symbols it holds) and a
size hint, both from the part header. It keeps, as 16-bit numbers:

- slots 0 to `seen`: slot 0 is the escape; the others hold a symbol
  value and its count;
- `total` of the counts (starts at 4, the escape's count);
- a cumulative table, rebuilt now and then, covering slots 0 to `built`;
- when to rebuild: when `total` reaches `threshold` (starts at 8);
- `step` (starts at 4), up to `maxStep`, and `maxTotal`:
  `maxTotal = clamp(hint * 32, 0x100, 0x3b38)`;
  `maxStep = 0x80` if `hint * 2 < 0x80`, else
  `min(hint * 2, maxTotal / 2 - 0x20)`.

To decode a symbol:

1. If `total >= threshold`: if `threshold >= maxTotal`, halve the counts
   (below); then rebuild the table (below).
2. Read a model's symbol from the table. The table starts at 0x8000:
   entry 0 is 0x8000, entry `i + 1` adds slot `i`'s count times
   `0x20000 / total` (integer division), shifted right by 3, and entry
   `built + 1` is 0xc000. The symbol is the slot `i` whose entries
   `i`, `i + 1` hold the point found (plus 0x8000). Before the first
   rebuild the table holds only the escape, over the whole range.
3. Add 1 to the slot's count and to `total`.
4. If the slot isn't 0, its symbol is the result.
5. The escape: if there are slots met since the last rebuild (`seen >
   built`), read a bit. If 1, read a value `k` in [0, seen - built); slot
   `built + 1 + k` gets 2 more counts (and `total` 2 more), and its
   symbol is the result.
6. Otherwise a new symbol: `seen` goes up by 1, that slot's count and
   `total` by 2. If `seen` reaches the capacity, the escape's count is
   taken out of `total` and set to 0. The caller reads the value (a value
   in [0, n), n depending on the model) and stores it in the slot.

Rebuilding the table, after writing it as in step 2: if `step * 2 >
maxStep`, `threshold = total + maxStep`; else `step` doubles and
`threshold = total + step`. Then `built = seen`.

Halving the counts: the escape's count is halved and becomes `total`.
Then for each slot `i` from 1 to `seen`: while its count is 1 or less, it
is replaced by the last slot (count and symbol), whose count becomes 0,
and `seen` goes down by 1; if `i` is the last slot itself, its count is
set to 0, `seen` goes down by 1 and the loop ends. Otherwise its count is
halved and added to `total`, and the slot with the largest halved count
is noted (the first one, on a tie). After the loop, if there was one, and
`seen` isn't 0 and it isn't slot `seen`, it swaps places with slot
`seen`. Last, if `seen` is below the capacity and the escape's count is
0, the escape's count becomes 1 and `total` goes up by 1.

### A part's header and models

Header words `h0`, `h1`, `h2`:

- `h0 & 0x1ff`: how many literal values (L);
- `h0 >> 9`: the window `W`, the farthest a match may reach back;
- `h1 & 0x1ff`: each literal model's capacity;
- `(h1 >> 19) + 1`: the capacity of the offset's top model;
- `h2`: the length models' capacities, a byte for each 16 models, the
  top byte first; the 65th model takes the low byte.

From the window: `lowCount = min(W + 1, 4)`,
`highCount = min((W >> 2) + 1, 256)`, `midCount = (W >> 10) + 1`.

The models (capacity, hint):

- 4 literal models: (`h1 & 0x1ff`, L - 1);
- 65 length models, chosen by the previous length (0 at the start of the
  part): (byte of `h2`, 64);
- one model for the low 2 bits of the offset: (`lowCount`,
  `lowCount - 1`);
- one for the offset's top bits (mid): (`(h1 >> 19) + 1`,
  `midCount - 1`);
- `midCount` models for its middle 8 bits (high), chosen by mid:
  (`highCount`, `highCount - 1`).

### Literals and matches

Each step first decodes a length from the length model of the previous
length; a new one is read as a value in [0, 65).

- **0: a literal.** Its value comes from literal model `position & 3`
  (the output position in the section; the DLL uses the address, which
  is aligned); a new one is read in [0, L). The low byte is written.
- **1 to 64: a match** of `length + 1` bytes for 1 to 60, and 128, 192,
  256 or 512 bytes for 61 to 64. Let `w` be the smaller of `W` and the
  bytes the part has made so far. Then `low` (new: in [0, lowCount)),
  `mid` (new: in [0, (w >> 10) + 1)) and `high` from high model `mid`
  (new: in [0, (w >> 2) + 1) if `w < 0x400`, else [0, 256)). The match
  copies from `(mid * 256 + high) * 4 + low + 1` bytes back, a byte at a
  time (it may overlap what it writes).

### Verification

`reluva-tool awe-check <game> -oodle` on the full install: 1,803 files,
4,034 compressed sections (181 MB decompressed), all identical to the
DLL's. The DLL in the interpreter took 757 s (summed over the threads),
the native decoder 10 s. `awe-check -gr2` went from 260 s to 5 s.

The unit test encodes a short stream by hand (`tests/test_awe.cpp`): a
model's first two symbols need no table, so a few plain values and bits
make literals and matches.

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

## Still to do

- Decode the Bink textures natively, checked byte for byte against the
  DLL on every file. The interpreter takes about 2.5 seconds per 512×512
  texture.
- Check the curve evaluation against Granny's (`GrannyEvaluateCurveAtT`)
  on more animations.
- The face: how the game drives the face bones (the `_f*` animations,
  `blink`, `eyeleft`...).
