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
- Sections are compressed (Oodle1), the textures inside are
  Bink-compressed, and some animations have compressed curves. None of
  them is documented. All three were worked out from the disassembly of
  the game's `granny2.dll` and are decoded natively: see "Oodle1",
  "Bink textures" and "Compressed curves" below. The DLL itself, run in
  Reluva's x86 interpreter (`src/engines/awe/granny_dll.cpp`), is only
  kept to check them against. These are the exports called, with the
  arguments Granny's public SDK header gives:
  - `GrannyDecompressData(Format, FileIsByteReversed, CompressedSize,
    Compressed, Stop0, Stop1, DecompressedSize, Decompressed)`, by
    `reluva-tool awe-check -oodle`;
  - `GrannyBinkDecompressTexture(Width, Height, Flags, CompressedSize,
    Compressed, DestLayout, DestStride, Dest)`, by `awe-check -gr2
    -textures`. The flags were taken from the DLL's own call to it in
    `GrannyCopyTextureImage`: the texture's `SubFormat`, plus 1 if its
    layout has alpha. Its start-up asks for the Windows directory, which
    Reluva provides;
  - `GrannyCurveExtractKnotValues`, on files the DLL loads itself, by
    `awe-check -gr2`;
  - `GrannyEvaluateCurveAtT(Dimension, Normalize, BackwardsLoop, Curve,
    ForwardsLoop, CurveDuration, t, Result, IdentityVector)`, by
    `awe-check -gr2 -curves`. The order of its arguments was checked in
    its code (0x50050740 passes them on to 0x50017cd0); see "Evaluating
    curves" below.

  A file the DLL loads is converted to its own structures, which differ
  from older files' (the plain curves' tracks are laid out differently).
  So the checks find the curves in its memory by the DLL's exported types
  (`GrannyFileInfoType`, `GrannyAnimationType`, `GrannyTrackGroupType`,
  `GrannyTransformTrackType`: pointers to member definitions laid out as
  in the files).
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
- **Textures:** inside the model files (Poirot's: three, 512×512),
  `Encoding` 3 (Bink). See "Bink textures" below.
- **Animations:** one per file: name, duration (walk: 0.8 s), time step
  (1/60), and a track group of transform tracks, one per bone. The 2.5
  exporter writes plain curves: `Degree` (2 here), `Knots` (times) and
  `Controls` (3 values per knot for position, 4 for orientation, 9 for
  scale/shear). A few animations have compressed curves instead (below).
  They are B-splines with one knot per control: see "Evaluating curves".

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

## Evaluating curves

Each curve, plain or expanded, is a B-spline with one knot per control.
Reluva evaluates it as granny2.dll does (`Curve::evaluate`,
`src/engines/awe/model.cpp`), and checks that against the DLL.

### How this was worked out

From the disassembly of `granny2.dll`, starting at the export
`GrannyEvaluateCurveAtT` (0x50050740), and at the sampling of a control's
tracks:

| Address | What it does |
|---------|--------------|
| 0x50017cd0 | finds the knot for t (0x50003ef0), then evaluates there |
| 0x50017d20 | an identity or constant curve gives its value; else builds the window, then samples |
| 0x50017810 | the window: the knots and controls around the knot, filled in past the curve's ends |
| 0x50030780 | puts quaternions on one side |
| 0x500174d0 | samples by degree and dimension, through a table of functions (0x50061a88; those that normalise at 0x50061b28) |
| 0x50044410 | a control's orientation track: the window, quaternions on one side, the sample, normalised |
| 0x5001bbe0 | a control's loop state (`GrannyGetControlLoopState`) |

### The B-spline

With knots t_0 … t_(n-1), controls P_0 … P_(n-1) and degree p (the
files use 0, 1 and 2):

1. k is the first knot after t, but at most n - 1.
2. Degree 0: the value is P_k.
3. Otherwise the window is knots t_(k-p) … t_(k+p-1) and controls
   P_(k-p) … P_k. De Boor's algorithm: for r from 1 to p, for i from k
   down to k - p + r, with a = (t - t_(i-1)) / (t_(i+p-r) - t_(i-1)) (0 if
   that is empty), P_i becomes (1 - a) P_(i-1) + a P_i. The value is P_k.

Each knot ends its control's interval. An animation's clock stays within
[0, its duration], and the knots run from 0 to it.

### Past the ends

Near either end, the window reaches past the curve. Granny fills in the
missing knots and controls, in one of two ways:

- **Not looping** that way: the end knot and control, repeated.
- **Looping**: the curve as one loop of a periodic one, of the
  animation's duration d. The last knot (at d) stands for the first,
  one loop on. Going forward, it is itself replaced: after t_(n-2) come
  t_0 + d, t_1 + d … t_(n-2) + d, then t_0 + 2d…, with their controls.
  Going back, before t_0 come t_(n-2) - d, t_(n-3) - d … t_0 - d, then
  t_(n-2) - 2d…. The DLL adds the d's up as floats.

Where it filled the window in, it puts quaternions on one side: each
control from the second on is negated if its dot product with the one
before is negative. `GrannyEvaluateCurveAtT` does that only when asked
to normalise; a control's orientation track always does.

Which way a curve loops is the control's loop state (0x5001bbe0):

- an animation looping for ever (loop count 0) loops both ways;
- loop i of a count of n loops back if i > 0, and forward if
  i < n - 1;
- an animation played once does neither.

(A flag set by `GrannySetControlForceClampedLooping` turns both off; the
game doesn't use it.) So `idle1`, `walk` and `talk` wrap round at both
ends, a gesture played once doesn't, and a blink played twice wraps
forward in its first loop and back in its second.

Orientations are then normalised. A control's track does it exactly
(0x500444ff: one over the square root of the squared length).
`GrannyEvaluateCurveAtT`'s own normalising is only close (a result
seen was 3·10⁻⁵ short of unit length); its direction is the same.

### In Reluva

`Curve::evaluate` takes the loop state (`CurveLoop`), and the Animator
gives each slot's (`Animator::Slot::loop`). It computes the B-spline in
doubles, as the DLL's x87 code keeps more than floats. It rounds the
filled-in knots to floats, as the DLL does.

There is one deliberate difference. A base animation's root bone (the
skeleton's first, with no parent) is evaluated without wrapping round.
It carries the character along: a walk's root moves the whole loop's
distance. The game takes that motion out of the pose: its base
animations' track groups accumulate it, and `GrannyUpdateModelMatrix`
moves the model. Reluva instead takes out the distance the root moves in
one loop (`Actor::motion`). Wrapping round, the root would swing back
toward its start over the loop's last piece.

Before this, Reluva never wrapped round, and kept only a curve's first
four values. On `poirot_walk`, looping, it was off by up to 239 units
(the root, near the end) and 0.003 in an orientation. Scale/shear curves
with more than one knot would have lost five of their nine values.

### Verification

`reluva-tool awe-check <game> -gr2 -curves` evaluates every curve of
every animation both ways. The DLL loads the file itself, and
`GrannyEvaluateCurveAtT` evaluates the curve in Reluva's x86 interpreter.
Each curve is evaluated at 0, at the duration, at up to 32 of its knots
and at 96 times between, in the four loop states. Orientations are
normalised on both sides. The differences are measured in floats'
epsilons of the curve's largest value (or of 1, if that is smaller).

On the full install (1,803 files, 1,701 animations): 293,531 curves at
122 million times, in 632 s on 4 threads. The largest differences:

| Loop state | Position | Orientation | Scale/shear |
|------------|----------|-------------|-------------|
| once | 0.00098 (3.2 ε) | 3.0·10⁻⁷ (2.5 ε) | 0 |
| looping, first loop, last loop | 0.00098 (1.0 ε) | 1.8·10⁻⁷ (1.5 ε) | 0 |

That is float rounding: the largest position difference is a step of a
float at a few hundred units. The check fails above 8 ε.

**Whole poses.** That check skipped curves with no knots, and so missed
how they're read. `reluva-tool awe-pose <game> <model> <animation>
[seconds...]` poses the model both ways: Reluva's `computePose`, and
granny2.dll's `GrannyInstantiateModel`, `GrannyPlayControlledAnimation`,
`GrannySetModelClock`, `GrannySampleModelAnimations` and
`GrannyBuildWorldPose` in the interpreter; then compares every bone's
world matrix. It found that an old-format curve with no knots (most
animation files have some, 1,666 of them) is the identity to Granny: no
move, no turn, unit scale. Reluva had kept the bone's rest transform
there. Where the rest isn't the identity that changes the pose: Count
Andrenyi's `sitidle1`, for one, has empty orientation curves for his neck
and collarbones, and Reluva sat him slumped forward with his head down
where the game has him upright (found against the walkthrough, Part 7,
6:16 to 6:44). Reluva now reads such a curve as the identity.

Over every character model and its animations that don't carry it
along (1,689 pairs, at 0, a third and two thirds of each), every bone
then agrees within 0.01; before, 337 of them differed, across 20
characters. The animations that carry the character along
(walks, runs, turns, and Antoinette's `blizzard2`, whose root
moves) differ by the root's motion alone: Granny's sampling takes
it out of the pose; Reluva takes it out where it places the character
(above).

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
