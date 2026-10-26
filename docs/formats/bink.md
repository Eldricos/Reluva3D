# Bink movies (.bik)

RAD Game Tools' video format, which Elite Force uses for its cinematics
(`video/<name>.bik`). Reluva decodes revisions `f` to `i` in
`src/common/video/bink.cpp`.

## Sources and checks

- **Where the facts come from:** the published descriptions of the format
  and the GPL decoders that document it (FFmpeg's, as carried by ScummVM).
  These were read for facts only; the code is new.
- **Fixed tables:** the format's 16 code trees, the coefficient scan order,
  the 16 run patterns, the quantisers and the audio band edges are
  constants every decoder must use. They are in
  `src/common/video/bink_tables.cpp`.
- **How it was checked:** `reluva-tool bink-decode <file> -yuv out.yuv -pcm
  out.pcm` writes what Reluva decodes, for comparing with
  `ffmpeg -i <file> -f rawvideo -pix_fmt yuv420p` and `-f s16le`.
  - On five public BIKi samples, every frame's Y, U and V planes match
    exactly.
  - Audio, both DCT and RDFT, matches to within 1 except a handful of
    clipped peaks: FFmpeg clips after blending overlapping blocks, Reluva
    before.

## The container

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
  (4 bytes), then the packet's bit stream.

The rest of the frame is the video.

Bit streams are read least significant bit first. Each plane and each audio
block starts on a 32-bit boundary.

## Video

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
  studio range.

## Audio

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

## In Elite Force

- **`cinematic <name>`** (`stvoy.exe` 0x404840) ends the game and plays
  `video/<name>.bik` full screen. When it ends, the engine runs the
  `nextmap` cvar's command and clears it.
- **`ingamecinematic <name>`** plays one over a running game. The game's
  scripts send it as `inGameCinematic <name>`.
- **At start-up** the engine plays `cinematic eflogo`.
- **Skipping.** A typed key (ASCII, Esc included) or the first mouse
  button skips a cinematic.
- **Variants the engine also looks for:** `_lo` versions on slow machines,
  and a German check. Reluva plays the plain name.
