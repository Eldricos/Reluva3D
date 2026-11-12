# Bink movies (`.bik`)

## What it is

RAD Game Tools' video format, from after Smacker. Reluva decodes it in
`src/common/video/bink.cpp` (`bink::Movie`).

## What Reluva supports

- Revisions `f` to `i`.
- Video with its alpha plane, if it has one.
- Every audio track, DCT or RDFT, mono or stereo.
- Two colour conversions: rounded, and `binkw32.dll`'s own (below).
- Each frame's size and whether it's a key frame, and `rewind()` for
  movies that loop.

## The format

### The container

All numbers are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `BIK` and the revision letter |
| 4 | 4 | file size − 8 |
| 8 | 4 | frame count |
| 12 | 4 | largest frame |
| 16 | 4 | (frame count again) |
| 20 | 4 | width |
| 24 | 4 | height |
| 28 | 4 | frame rate numerator |
| 32 | 4 | frame rate denominator |
| 36 | 4 | flags (0x00100000: an alpha plane) |
| 40 | 4 | audio track count *n* |

The header continues with these, one entry per audio track:

1. *n* × 4 bytes: the largest decoded size of each track (unused).
2. *n* × (2 + 2) bytes: each track's sample rate and flags.
   - 0x2000: stereo.
   - 0x1000: DCT; otherwise RDFT.
3. *n* × 4 bytes: track ids.

Then comes the frame index: *count* × 4-byte file offsets, where bit 0
marks a key frame. Each frame runs to the next frame's offset; the last runs
to the end of the file.

A frame holds, for each audio track:

- a 4-byte packet length;
- if the length is 4 or more, the size of the decoded samples in bytes
  (4 bytes; "samples" in the published description, but bytes: over a
  movie they add up to its length × rate × channels × 2), then the
  packet's bit stream. Reluva cuts each packet's sound at that size. Only
  a movie's last packet decodes to more (its block runs past the end:
  7,680 bytes where 2,640 are declared, in `logos.bik`); in the AWE
  install's 39,331 packets, every other one is whole blocks. FFmpeg keeps
  the whole block.

The rest of the frame is the video.

Bit streams are read least significant bit first. Each plane and each audio
block starts on a 32-bit boundary.

### Video

- **Planes.** In order: alpha (if the flag is set), Y, then the two
  chroma planes at half size. From revision `h` on, the chroma planes come
  V then U. In revision `i`, 32 bits are skipped before alpha and before Y.
- **Blocks.** Each plane is decoded in 8×8 blocks, a row at a time. Before
  each row, these bundles read more values if their previous values are
  used up:
  - block types and 16×16 sub-block types;
  - colours (a high nibble from a tree chosen by the last high nibble, then
    a low nibble; before `i`, signed around 128);
  - patterns;
  - motion x and y;
  - intra and inter DC values (11 bits, then deltas in groups of 8);
  - run lengths.
- **The bundles' counts and code trees.** A bundle's count is read in
  log2(n + 511) + 1 bits, where n depends on the plane width, and 0 ends
  the plane. Each bundle's code tree is one of the 16 fixed trees, with
  its symbols reordered either by a list or by merging halves.
- **Block types:**

  | Type | Name | What it does |
  |---|---|---|
  | 0 | skip | copy the previous frame's block |
  | 1 | scaled | a 16×16 block drawn at half resolution: run, intra, fill, pattern or raw |
  | 2 | motion | copy the previous frame's block at an offset |
  | 3 | run | runs of colours in one of 16 orders |
  | 4 | residue | motion, plus a bit-plane-coded difference |
  | 5 | intra | DCT |
  | 6 | fill | one colour |
  | 7 | inter | motion, plus a DCT difference |
  | 8 | pattern | two colours chosen by an 8×8 bit pattern |
  | 9 | raw | 64 colours |

- **DCT coefficients** are read by bit planes, from the top one down. They
  are dequantised by one of 16 quantisers (<< 11) and transformed by the
  format's fixed-point 8×8 inverse DCT.
- **Colour conversion.** Reluva converts the planes to RGB with BT.601 in
  studio range, two ways (`Movie::toRgba`):
  - **Rounded** (the default, Elite Force's): each pixel in fixed point,
    rounded once.
  - **Binkw32**, as `binkw32.dll` converts (the AWE engine's), each term
    in fixed point and truncated on its own before they're added and
    clamped to 0..255. Its MMX path (`0x30013d00` in Orient Express's
    DLL), which every machine since the late 1990s takes:

        Y' = ((max(Y − 16, 0) · 4) · 0x4a85) >> 16
        R  = Y' + r[V]          G = Y' + gu[U] + gv[V]          B = Y' + b[U]

    with the tables built at start-up (`0x3000a990`), for c = 0..255,
    divided by 32768 and truncated toward zero:

        b[c]  = (−0x811a80 + 0x10235·c)      (c − 128) × 2.01723
        gu[c] = ( 0x191280 −  0x3225·c)     −(c − 128) × 0.39177
        gv[c] = ( 0x340780 −  0x680f·c)     −(c − 128) × 0.81300
        r[c]  = (−0x662580 +  0xcc4b·c)      (c − 128) × 1.59603

    Each chroma sample serves its 2×2 pixels. The DLL's path without MMX
    gives the same, except that it clamps Y − 16 to 219 as well: they can
    differ only where Y is over 235, by up to 24 in the install's movies.
    Rounded instead differs from the DLL in a quarter of the pixels, by up
    to 7.
  - The alpha plane, if any, is the pixels' alpha; else 255.

### Audio

- **Blocks.** Each block holds 2^9 samples below 22050 Hz, 2^10 below
  44100 Hz, otherwise 2^11. A stereo RDFT track interleaves both channels
  in a block twice that size.
- **Overlap.** Blocks overlap by a sixteenth of their length; the overlap
  is a linear crossfade into the previous block's tail.
- **Each block, per channel:**
  1. Two floats: a 5-bit exponent, a 23-bit mantissa and a sign.
  2. Each band's quantiser: 8 bits, used as exp(min(v, 95) × 0.1529) ×
     2 / √N.
  3. The coefficients, in runs of 8 (or a coded multiple), each run with
     a 4-bit width.
- **The transform.**
  - DCT tracks skip 2 bits per block, double the first coefficient and take
    a DCT-III: x[m] = X0/2 + Σ Xk cos(πk(m + ½)/N).
  - RDFT tracks take a real inverse DFT of a packed spectrum. The packing
    is X0, X(N/2), then the real and imaginary parts of each Xk; the
    imaginary parts are stored negated.

## Sources and checks

- **Where the facts come from:** the published descriptions of the format
  and the GPL decoders that document it (FFmpeg's, as carried by ScummVM).
  These were read for facts only; the code is new.
- **Fixed tables:** the format's 16 code trees, the coefficient scan order,
  the 16 run patterns, the quantisers and the audio band edges are
  constants every decoder must use. They are in
  `src/common/video/bink_tables.cpp`.
- **How it was checked:** `reluva-tool idtech3 bink-decode <file> -yuv out.yuv -pcm
  out.pcm` writes what Reluva decodes, for comparing with
  `ffmpeg -i <file> -f rawvideo -pix_fmt yuv420p` and `-f s16le`.
  - On five public BIKi samples, every frame's Y, U and V planes match
    exactly.
  - Audio, both DCT and RDFT, matches to within 1 except a handful of
    clipped peaks: FFmpeg clips after blending overlapping blocks, Reluva
    before.
- **Checked against Murder on the Orient Express's own `binkw32.dll`**,
  run in Reluva's x86 interpreter: `reluva-tool awe bink-check <game
  folder> <movie.bik>... [-frames n] [-yuv]`. The DLL opens the movie from
  memory and its frames are copied out as the game copies them
  (`BinkCopyToBufferRect`, 32-bit; with an alpha plane, 32-bit with
  alpha), or with `-yuv` as YUY2, before the colour conversion. Over all
  87 of the install's files (all revision `i`; the 47 with sound, stereo
  DCT), every Y, U, V and alpha value and every RGB pixel of every frame
  tried is the same as the DLL's, converted as below; pixels over Y 235
  are counted apart (the DLL's path without MMX, which the interpreter
  takes, clamps there and players' machines' MMX path doesn't).
- **One decoder for both.** The AWE engine had a decoder of its own
  (written apart, with FFmpeg's tables) until the merge with main. On the
  87 files the two gave the same planes on every frame, and sound within
  1 of each other; this one was kept, with what the AWE engine needed
  added: the frames' sizes and keyframes, every audio track's form,
  `rewind()` for loops, binkw32's colour conversion and the audio cut at
  each packet's declared size.

## Used by

- **`idtech3`**: Elite Force's cinematics
  ([cinematics.md](../../engines/idtech3/games/stvef/systems/cinematics.md)).
- **`awe`**: Murder on the Orient Express's movies and video loops
  ([movies.md](../../engines/awe/games/motoe/systems/movies.md)).
