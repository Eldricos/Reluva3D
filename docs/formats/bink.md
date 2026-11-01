# Bink video (.bik)

RAD Game Tools' video format, used by many games of its time. Murder on
the Orient Express has 46 movies in `Movies/`, plus looping videos in
`Elements/` and among the scenes' cutouts. Reluva's support is in
`src/common/video/`, shared by every engine:

| File | What |
|------|------|
| `bink_file` | the container: header, audio tracks, frame index, each frame's packets |
| `bink_video` | video, revisions d to i (not b: Heroes of Might and Magic 3's) |
| `bink_audio` | audio, the DCT form (the FFT form isn't done yet) |

## Sources

- **The container** comes from "Description of the Bink File Format"
  (Mike Melanson), on the MultimediaWiki as "Bink Container". One
  correction: a frame's audio packet starts with the size it decodes to
  **in bytes**, not in samples. Over `logos.bik` they add up to its
  length × 44,100 × 2 channels × 2 bytes.
- **Video and audio**: the MultimediaWiki's "Bink Video" and "Bink Audio"
  describe the bitstream: bundles, block types, the coding of the trees'
  leaves, audio frame sizes, bands and floats. They leave out:
  - the DCT and residue coefficients' coding;
  - the IDCT;
  - the 16 predefined Huffman trees, the run fill patterns, the scan
    order and the quantisers;
  - the audio coefficients' runs and the transform's scaling.

  For those, FFmpeg's decoder (`libavcodec/bink.c`, `binkdsp.c`,
  `binkaudio.c`, LGPL 2.1 or later, tag `n7.1`) was the reference. Its
  data tables are included unchanged in `third_party/ffmpeg_bink`. The
  decoder is Reluva's own code. This was the project's choice for Bink,
  rather than working those parts out from `binkw32.dll`.
- **Checked against FFmpeg's output** (the `ffmpeg` program, as a black
  box), on `logos.bik` (revision i, 1024×768, 539 frames, 44.1 kHz stereo):
  - video: every frame byte for byte the same as
    `ffmpeg -i logos.bik -f rawvideo -pix_fmt yuv420p` gives
    (`reluva-tool bink-info logos.bik -yuv ours.yuv`);
  - audio: every sample within ±1 (rounding) of
    `ffmpeg -i logos.bik -c:a pcm_s16le`
    (`reluva-tool bink-info logos.bik -wav ours.wav`). The one difference
    is that Reluva cuts each packet's sound to the size its frame
    declares. Only the file's last packet decodes to more (7,680 bytes
    where 2,640 are declared): its block runs past the end of the movie.
    FFmpeg keeps the whole block.

- **Checked against the game's own `binkw32.dll`** (Murder on the Orient
  Express's, run in Reluva's x86 interpreter: `reluva-tool awe-bink
  <game folder> <movie.bik>... [-frames n] [-yuv]`). The DLL opens the
  movie from memory and its frames are copied out as the game copies
  them (`BinkCopyToBufferRect`, 32-bit; with an alpha plane, 32-bit with
  alpha). With `-yuv` they're copied as YUY2 instead, before the colour
  conversion: every Y, U and V the same as Reluva's on every frame tried.
  The RGB first differed in a quarter of the pixels, by up to 7, from
  rounding: Reluva now converts as the DLL does (below), and every pixel
  of every frame tried is the same.

## Colour conversion

binkw32.dll converts to RGB with BT.601 at studio range, each term in
fixed point and truncated on its own before they're added and clamped to
0..255. Its MMX path (`0x30013d00`), which every machine since the late
1990s takes:

    Y' = ((max(Y − 16, 0) · 4) · 0x4a85) >> 16
    R  = Y' + r[V]          G = Y' + gu[U] + gv[V]          B = Y' + b[U]

with the tables built at start-up (`0x3000a990`), for c = 0..255,
divided by 32768 and truncated toward zero:

    b[c]  = (−0x811a80 + 0x10235·c)      (c − 128) × 2.01723
    gu[c] = ( 0x191280 −  0x3225·c)     −(c − 128) × 0.39177
    gv[c] = ( 0x340780 −  0x680f·c)     −(c − 128) × 0.81300
    r[c]  = (−0x662580 +  0xcc4b·c)      (c − 128) × 1.59603

Each chroma sample serves its 2×2 pixels. The DLL's path without MMX
(taken in the interpreter, which has none) gives the same, except that
it clamps Y − 16 to 219 as well: they can differ only where Y is over 235.

## Notes

- Planes are Y, then the two chroma planes (V first from revision h on),
  each 8×8 blocks in rows, ending on a 32-bit boundary. From revision i,
  the alpha and luma planes are preceded by their size.
- Pixel values aren't clamped after the IDCT or after adding a residue.
  They wrap, as FFmpeg's do, and the comparison above includes this.
- The audio's inverse DCT is done with an FFT (Makhoul's method:
  `V[k] = (X[k] − iX[N−k])·e^{iπk/2N}`, an inverse DFT, then interleaving
  from both ends). It's checked against the plain sum to 1e-14.
- Speed: `logos.bik`'s 539 1024×768 frames decode in about 2 seconds,
  about 280 frames a second.

`reluva-tool awe-check <game folder> -bik` decodes every movie and loop
of an install (several at once) and reports any failures.

## Still to do

- The FFT form of Bink audio, and revision b (if a game needs them).
- Seeking (to a keyframe) and frame skipping for slow machines.
