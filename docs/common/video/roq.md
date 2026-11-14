# RoQ video

## What it is

RoQ (`.roq`) is the video format of Quake III Arena and the games built on
it: Jedi Outcast's cinematics and in-game screens, Jedi Academy's. A file
is a run of chunks; frames are built from small cells kept in codebooks
and from the frame before, with DPCM sound between them. Reluva decodes
it in `src/video/roq.cpp` (`reluva::roq::Movie`).

## What Reluva supports

Every chunk is a u16 id, a u32 size and a u16 argument, then its data
(little-endian):

| Id | Chunk | What Reluva does |
|---|---|---|
| `0x1084` | the file's first, its size `0xffffffff`; the argument is the frames per second | read |
| `0x1001` | the size: u16 width, height (both multiples of 16), then 8 and 4 | read |
| `0x1002` | a codebook: as many 2x2 cells as the argument's high byte (0: 256), 6 bytes each (Y top left, top right, bottom left, bottom right, then U, V), then as many 4x4 cells as its low byte (0: 256 if there's room left), 4 bytes each (four 2x2 cells, the same order) | read |
| `0x1011` | a frame (below); the argument is the frame's mean motion, x in its high byte, y in its low, signed | decoded |
| `0x1020`, `0x1021` | sound, mono or stereo (below) | decoded |
| `0x1012` | a frame as a JPEG | refused (none of Jedi Outcast's has one) |

- **Frames.** Each 16x16 block, left to right then top to bottom, is
  four 8x8 blocks in the same order, each chosen by a 2-bit code: 0 kept,
  1 moved, 2 a 4x4 cell doubled, 3 cut into four 4x4 blocks, each with a
  code of its own: 0 kept, 1 moved, 2 a 4x4 cell, 3 four 2x2 cells. The
  codes come eight to a 16-bit word, highest first, the words read from
  the frame's bytes as they're needed, among the bytes the codes use (a
  cell's index, a move's offsets). A move takes the block from the last
  frame, `8 - high nibble - mean x` across and `8 - low nibble - mean y`
  down, of the byte after its code.
- **Two pictures in turn.** A frame is drawn over the picture from two
  frames before (the second frame over a copy of the first; the first
  over black): a kept block keeps that one's pixels, while moves take
  from the last frame. A frame read as if drawn over the last one goes
  wrong wherever the video relies on this, in blocks left behind from
  earlier.
- **Colours.** Y, U and V full-range (BT.601, no studio range): R = Y +
  1.402 V', G = Y − 0.344 U' − 0.714 V', B = Y + 1.772 U', with U' and V'
  less 128. A 2x2 cell's one U and V cover its four pixels; Reluva keeps
  the planes whole-size. Output: RGBA32.
- **Sound.** 22,050 samples a second, 16-bit. Each byte changes its
  channel's last sample by its square, below 128, and by minus the square
  of itself less 128 above; stereo bytes alternate left and right, and
  each chunk starts from its argument (mono: the whole of it; stereo: the
  high byte as the left's high byte, the low byte as the right's).

## Sources and checks

- **Where the facts come from:** the format's public descriptions (the
  MultimediaWiki's RoQ page, and the GPL Quake III Arena source's
  `cl_cin.c` read as documentation), the double-buffering as decoders
  describe it; the code is new.
- **How it was checked:** all 15 of Jedi Outcast 1.02a's videos decode to
  their end (`reluva-tool idtech3 roq-decode`), their sound as long as
  their frames (the opening logos: 25.8 s of sound for 381 frames at 15
  a second); frames looked at (the Activision logo, the opening shot of
  the first cinematic) are clean, and were blocky where kept blocks had
  been taken from the last frame instead. A unit test
  (`common.unit.video`, "roq") decodes a made-up movie: cells, a move, a
  frame of kept blocks, stereo sound. Not compared with another decoder's
  output yet.
- **Confidence:** published unless marked.

## Used by

- The id Tech 3 engine's Jedi Outcast
  ([docs/engines/idtech3/games/jk2/](../../engines/idtech3/games/jk2/README.md)).
