# EA's TQI video and its movies (`.tgq`)

## What it is

Electronic Arts' TQI video, the pictures of some of its games' movies
around 2000: every frame a picture of its own (there are no frames built
from the one before), coded as MPEG-1 codes its intra frames. A movie
(`.tgq`) is an EA sound stream with the pictures' chunks (`pIQT`)
between the sound's. The code is `src/video/tqi.cpp` (`tqi::Decoder`)
and `src/video/ea_movie.cpp` (`EaMovie`).

## What Reluva supports

- Frames of any size in whole 16 × 16 macroblocks, at any quantiser.
- The movie's sound, in the stream's own codings: stereo EA-XA or 16-bit
  PCM ([ea-streams.md](../audio/ea-streams.md)).
- The inverse DCT is floating point, so pictures can differ from EA's own
  integer one by a level or two; the colours are converted to RGB with
  ITU-R BT.601's formula, which may not be what each game does (Used by).

## The format

### The movie

Chunks, each a 4-character tag and a u32 size, little-endian, the 8 bytes
included: an EA sound stream's (`SCHl` with its PT header, `SCCl`,
`SCDl`, `SCEl`), with a `pIQT` chunk for each frame among them. Frames
are shown at 15 a second.

### A frame (`pIQT`)

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | The width in pixels, a multiple of 16 |
| 2 | 2 | The height |
| 4 | 1 | The quantiser |
| 5 | 3 | Not worked out: `14 1E 03` in every frame of SimCoaster's |
| 8 | | The picture's codes |

The codes are read from 32-bit little-endian words, each from its top
bit. The frame is macroblocks of 16 × 16 pixels, row by row from the top
left, each four 8 × 8 luma blocks (top left, top right, bottom left,
bottom right) then a Cb and a Cr block for its 8 × 8 of half-size
chroma (4:2:0). There are no macroblock or slice headers.

Each block is coded as an MPEG-1 intra block (ISO/IEC 11172-2):

- **DC:** a size code (table B.12 for luma, B.13 for chroma), then that
  many bits of difference, MPEG-1's way; added to the last DC of the same
  plane. Each plane's DC starts the frame at 0, where MPEG-1's starts at
  1024. The block's DC coefficient is the DC × 8.
- **AC:** MPEG-1's run and level codes (table B.14), each with a sign bit;
  `10` ends the block. The escape, `000001`, is followed by a 6-bit run
  and an 8-bit signed level; a level byte of 0 or −128 is followed by
  another 8 bits, the level being that byte, or it − 256, as in MPEG-1;
  no frame here has one, its escaped levels reaching 72 *(inferred)*.
  The coefficients go in MPEG-1's zigzag order.
- **Dequantising:** an AC coefficient is level × matrix × scale / 8,
  with MPEG-1's default intra matrix, and scale = (107.5 − quantiser) ×
  0.625 (5.3125 at SimCoaster's 99).
- **Then** the 8 × 8 inverse DCT; the result, clamped to 0-255, is the
  pixel.

## Sources and checks

- **Where the facts come from:** MultimediaWiki's pages on EA's formats
  ("Electronic Arts TQI", "Electronic Arts TGQ"): the frame's header, the
  macroblocks, the frame rate, the scale's formula and the matrix (as a
  table: MPEG-1's), and that the coefficients are MPEG-1's codes. The
  rest was worked out on SimCoaster's EA GAMES logo (`Data/movies/bf.tgq`,
  81 frames, 320 × 480): reading MPEG-1's codes from byte 8, the first
  macroblock decodes as a flat white block, and every frame's codes end
  within the last word; the DCs' start at 0 by decoding to the pictures
  (November 2026).
- **How it was checked:** the 81 frames decoded and compared with
  ffmpeg's own decoder's (run as a program, its source not read): the
  planes match to 58-61 dB, never more than 4 levels apart, the inverse
  DCTs' differences. The unit tests (`common.unit.tqi`,
  `tests/common/tqi.cpp`) decode frames built bit by bit and a movie
  built in the test.
- **Confidence:** confirmed unless marked.

## Used by

None yet.
