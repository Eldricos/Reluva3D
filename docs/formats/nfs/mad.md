# Need for Speed III: movies (`.mad`)

Status: **implemented** (`src/engines/nfs/mad.cpp`):

- the four movies on the CD (`fedata/movies`: `titleav`, `demoav1`-`3`)
  read;
- all 2,882 of their frames decode, and their audio
  (`reluva-tool nfs-survey`, `mad-info`).

`reluva-tool mad-frames` writes frames as TGA pictures. `mad-wav` writes
the audio.

## How it was worked out

October 2026.

- **The file.**
  - The chunk layout was read off hex dumps: the same chunks as the music
    streams ([mus.md](mus.md)), with video frames between the audio
    blocks.
  - The fields of the frame header were found by comparing the four
    movies.
- **The video codec.** EA's "Madcow" codec was learned from two
  references, read as references only ([nfs.md](../../engines/nfs.md),
  Sources):
  - the MultimediaWiki page on EA's MAD;
  - FFmpeg's `eamad` decoder.

  Reluva's decoder was written from that understanding. The MPEG-1 code
  table and intra quantiser matrix were taken from ISO/IEC 11172-2. The
  AAN scale factors are computed, not tabulated.
- **Checks.**
  - **Pictures.** The frames are the pictures one expects: the EA and
    Dolby logos, the title sequence's driving shots, the demos' races
    with their "DEMO" caption.
  - **Against FFmpeg.** Every frame of all four movies was compared with
    FFmpeg 6.1's raw YUV output. Most frames are identical byte for byte.
    FFmpeg keeps Q in 16 bits, so above *q* ≈ 8 its highest-frequency
    values wrap; Reluva keeps them whole, as the game does (The game's
    decoder, below). That is the only difference: with Q cut the same
    way, all four movies match FFmpeg. 21 frames differ, all at high *q*:
    4-8 in each demo movie and 5 in the title movie.
  - **Against the game.** The game's decoder was read from nfs3.exe's
    disassembly, and its arithmetic was rebuilt in Reluva and run on all
    four movies. Frames of the demo movie were captured from the game in
    nfs-host (`-native -deterministic 50000 -frames dir -framesfrom
    2950`; frames 2968 and 2969 are movie frames 3 and 6).
  - **Audio.** The audio matches FFmpeg's decode sample for sample.
    FFmpeg rejects `demoav1`'s short last block; Reluva decodes it.

## The file

Chunks, each a 4-character tag and a u32 size (little-endian, the 8 bytes
included):

| Chunk | Contents |
|---|---|
| `MADk` | a key frame: every block coded on its own |
| `MADm` | an inter frame: blocks may be copied from the frame before |
| `MADe` | an inter frame that later frames don't refer to (none on this CD) |
| `SCHl`, `SCCl`, `SCDl`, `SCEl` | one audio stream, as a music segment ([mus.md](mus.md)): stereo EA-XA, 22,050 Hz |

Each audio block follows the frame it belongs with. The four movies are
320×240 at 15 frames a second. A key frame comes every fifth frame.

## Frames

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | tag |
| 4 | u32 | chunk size |
| 8 | u32 | 0 in all frames; the game doesn't read it |
| 12 | u32 | frame duration, in 1/65536 ms (0x42AAAB: 66.67 ms) |
| 16 | u16 | width |
| 18 | u16 | height |
| 20 | u8 | 0; not read |
| 21 | u8 | quantiser scale (1-29) |
| 22 | u16 | 0; not read |
| 24 | | the bitstream |

nfs3.exe's player (0x495c54-0x495d4f) takes the duration, width and
height from the first frame, and from each frame its tag (`MADk` or
not) and quantiser byte. The bitstream starts at 24. No other header
byte is read there. The stream code before it uses the chunk size.

The bitstream is a sequence of 16-bit little-endian words, each read from
its top bit.

The picture is YUV 4:2:0, full range: black is Y = 0, and chroma is
centred on 128. It is coded in 16×16 macroblocks, left to right and top
to bottom. Each macroblock has six 8×8 blocks: four luma (top left, top
right, bottom left, bottom right), then U, then V.

### Macroblocks

In a key frame every block is intra coded (below). In an inter frame a
macroblock starts with a prefix:

| Prefix | Meaning |
|---|---|
| `1` | all six blocks are copies |
| `01` + 6 bits | the blocks whose bit is set are copies (bit 0: the first luma block) |
| `00` | no copies |

When there are copies, a motion vector follows: x, then y, each a
**motion value**:

- `0` means 0;
- `1`, a sign bit and 4 bits *m*: *m* + 1 when the sign is 0, *m* − 16
  when it is 1.

Then the blocks follow in order. A copy is a motion value *d*: the block
at the same place in the previous frame, moved by the vector, with 2*d*
added to each sample and clamped to 0-255. For chroma the vector is
halved, rounding toward zero.

A copy whose source would start outside the picture is skipped: the
block keeps what the previous frame had there. The test is the source's
offset in the plane, which must be at least 0 and below
(height − 7) × stride − 7. Any other block is intra coded.

A `MADe` frame isn't kept as the reference for the next frame. An inter
frame with nothing before it works from black (Y 0, U and V 128).

### Intra blocks

1. **DC.** An 8-bit signed value *s*. The coefficient is (128 + *s*) × Q[0].
2. **AC.** Coefficients in zigzag order, until the end-of-block code
   `10`. Each code is one of:
   - **A run and level** from MPEG-1's DCT coefficient table (table B.14
     of ISO/IEC 11172-2), followed by a sign bit. Run 0, level 1 is `11`
     throughout, never the first-coefficient form `1`.
   - **An escape**, `000001`, followed by a 10-bit signed level and a
     6-bit run. This differs from MPEG-1's escape.

   The run skips that many positions. The coefficient at position *j* is
   ((|level| × Q[*j*]) >> 4) − 1, made odd by setting its lowest bit, and
   negated when the level is negative.
3. **Inverse transform.** EA's own fixed-point transform, which FFmpeg
   shares with EA's TGQ and TQI video:
   - add 4 to the DC coefficient;
   - transform the columns, keeping 16-bit results;
   - transform the rows, shifting the results right by 4 and clamping
     them to 0-255.

   The 1-D transform is the AAN butterfly network. Its constants have 8
   or 9 fraction bits:
   - 181: 1/√2;
   - 669: cos(π/8)·√2;
   - 277: sin(π/8)·√2;
   - 196: sin(π/8).

   It leaves out the scale factors a(k) = √2·cos(kπ/16), with a(0) = 1,
   so the quantiser divides by them instead (below).

### The quantiser

Q[*i*] is computed for each frame from the scale byte *q*, the MPEG-1
default intra matrix W, and S[*i*] = round(4096 / (a(*u*)·a(*v*))) for
row *u* and column *v*:

- Q[0] = (S[0] × W[0]) >> 11, which is 16 (DC is not scaled by *q*);
- Q[*i*] = (S[*i*] × W[*i*] × *q* + 32) >> 10.

## The game's decoder

nfs3.exe's decoder was read from the disassembly:

| Address | What |
|---|---|
| 0x4f3560 | the quantiser, per frame |
| 0x510e48 | an intra block's coefficients |
| 0x510cbd | the inverse transform: 0x510a50 for the columns, 0x510bb3 for the rows |
| 0x4f30d0 | the output table at 0x9f0d38 |
| 0x4f3420 | a copied block |
| 0x510ff8 | packing samples for display (V, U, Y1, Y0 per pixel pair) |

It decodes the same bitstream with different arithmetic:

- **The quantiser** is 16.16 fixed point, kept to 32 bits. Q[0] is W[0]
  × 2^15 × S'[0] / 2^16 and Q[*i*] is W[*i*] × *q* × 2^12 × S'[*i*] /
  2^16, rounded on bit 15. S'[*i*] = round(8192 / (a(*u*)·a(*v*))), a
  table at 0x56d02c. Nothing is cut to 16 bits, so the game decodes high
  *q* the way Reluva does, not the way FFmpeg does.
- **Coefficients**: DC is *s* × Q[0], with no 128 offset (the picture is
  centred on 0); AC is level × Q, not shifted and not made odd.
- **The transform** is AAN in 32 bits. 1/√2 is a 32-bit multiply
  (1518500250 / 2^31, then doubled). The odd part's rotations are x87
  floats (0.541196108, 1.306563020, 0.382683426), rounded to the nearest.
  A row whose AC terms are all 0 copies its DC term.
- **Samples are 7 bits.** Bits 16-23 of each result, as a signed byte,
  clamped to −64..63 and offset by 64.
- **Copies add *d*** to these 7-bit samples (2*d* at 8 bits) and don't
  clamp: a byte wraps.

Reluva has this as `MadDecoder::Arithmetic::Game` (`mad-frames ...
-game`). It is not the default. On the demo movies it differs from the
reference decode by about 1 in 8 bits, and is no closer to frames
captured from the game running in nfs-host.

## The game's display step

Read from the disassembly (October 2026). After 0x510ff8 the frame is a
4:2:2 buffer of 2 bytes a pixel (allocated at 0x4957f6): per pixel pair
the bytes V, U, Y1, Y0, each a 7-bit sample (0-127).

| Address | What |
|---|---|
| 0x495bc0 | opens a movie; called once, from 0x4961b5 |
| 0x4df1f0 | the display setup: keeps the position, size and mode |
| 0x4df2b0 | shows a frame |
| 0x4fdc90 | the software converter: clipping, then a routine per line |
| 0x4fda20 | builds the conversion tables for 15- or 16-bit screens |
| 0x4fd61c, 0x4fd6ef, 0x4fd7b6, 0x4fd892, 0x4fd8ee | the line routines |

**Hardware or software.** 0x4df1f0 can ask DirectDraw for overlay or
blit support for YUV surfaces (its caps, bits 0x2000/0x4000 and
0x100/0x200), but the movie player always passes the flag that skips
this (`push 0x1` at 0x4961ab), so movies always go through the software
converter. It works only on 15- and 16-bit screens (0x565010); any other
returns −2.

**The scaling mode** depends on the CPU's clock, measured at start-up
(0x4f2076, MHz × 10⁶ in 0x5652a4; chosen at 0x496175):

| CPU | Mode | Each output frame |
|---|---|---|
| under 133 MHz | 1 | each pixel twice across, on even lines; odd lines not written |
| 133-199 MHz | 2 | as 1, and each odd line the average of the source lines above and below it, each pixel twice across |
| 200 MHz and up | 3 | interpolated across and down (below) |

The picture is centred on the screen at twice its size (0x495c92).

**The conversion** (tables from 0x4fda20). With Y, U, V the 7-bit
samples:

- R = 2Y + 1 + 2 × round(1.402 × (V − 63.5))
- G = 2Y + 1 + 2 × (round(−0.3441 × (U − 63.5)) + round(−0.7141 × (V − 63.5)))
- B = 2Y + 1 + 2 × round(1.772 × (U − 63.5))

Each is clamped to 0-255. These are the full-range BT.601 (JPEG)
coefficients, applied to the 7-bit samples' centres; each chroma term is
rounded to the nearest (x87 `fistp`, in the FPU's rounding mode) on its
own, and G's two separately. Then each component is cut to 5 bits (6 for
16-bit G) in one of two ways:

- the left pixel of each output pair (low half of each 32-bit write):
  (c + 4) >> 3, at most 31 (6-bit G: (c + 2) >> 2, at most 63);
- the right pixel: c >> 3 (c >> 2).

So even columns are rounded and odd columns truncated, a two-pixel
dither. 0x4fdd09 makes the starting x odd when the screen address there
isn't 4-byte aligned, keeping the writes aligned.

**The averages** are per byte, of the 7-bit samples, rounded down:
(a + b) >> 1 on the packed 32-bit words, masked with 0x7F7F7F7F.

- **Pixel repeat** (modes 1 and 2, 0x4fd6ef): each source pixel is
  written twice, once rounded and once truncated (the dither above).
- **Odd lines, mode 2** (0x4fd7b6): Y, U and V averaged between source
  lines j and j + 1, then repeated as above, on output line 2j + 1;
  the last odd line isn't written.
- **Across, mode 3** (0x4fd892): from source pixel pairs (Y0, Y1, U, V)
  and the next pair (Y0′, U′, V′), four output pixels: Y0, (Y0 + Y1)/2,
  Y1, (Y1 + Y0′)/2. The first two keep U, V; the last two take (U + U′)/2,
  (V + V′)/2. The last source pair of a line isn't interpolated, so each
  line stops 4 pixels short.
- **Down, mode 3** (0x4fd8ee): the odd lines average source lines j and
  j + 1 first, then interpolate across the same way; again the last odd
  line isn't written.

When the area is above 153,600 pixels the odd lines' pass first calls
0x4f90e0, which can unlock and relock the screen.

Reluva does all three (`MadDecoder::toGameSurface`; `reluva-tool
mad-frames ... -display 1-3`), and the Movies page shows movies so by
default, in mode 3 with the game's decoder arithmetic: the game in
nfs-host reads its processor as 300 MHz, so mode 3 is what it shows. Key
G steps through modes 2 and 1 and Reluva's plain conversion (the
full-range BT.601 matrix in floating point, each pixel shown twice).

**Checked against the game** (October 2026). The demo's first frames
were captured from the game in nfs-host (`-frames dir -every 1
-framesfrom 2950`: captures 2968-2970 are `demoav1` frames 3, 6 and 10)
and compared in 5:6:5 with every frame of the three demos in each mode
and arithmetic:

| Capture | Mode 3, game arithmetic | Mode 3, reference | Mode 2 | Mode 1 |
|---|---|---|---|---|
| 2968 (frame 3) | 67.8% of pixels exact, mean 0.22 | 66% | 51% | lower |
| 2969 (frame 6) | 36%, mean 0.58 | 35% | 10%, mean 3.07 | lower |
| 2970 (frame 10) | 82.7%, mean 0.09 | 69% | 65% | lower |

Mode 3 matches best in every frame, with the layout exact: each line's
last 4 pixels and the last line are black in both. What differs is one
step of 5:6:5 here and there, mostly in green alone or in all three
together: samples one apart, from the decoder, not the display. The
chroma lines are used as they are for both luma lines they cover;
averaging them between lines matches worse (70% against 82.7% on frame
10). The softer look of the fade-in frames is this interpolation.

## What isn't known
- **The remaining differences** from the game's frames: one step of
  5:6:5 in a third to two-thirds of the pixels of the high-*q* fade-in
  frames, from decoded samples one apart. The game's decoder arithmetic as
  rebuilt (The game's decoder) isn't yet exact there.
