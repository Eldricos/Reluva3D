# AVI and Cinepak video

`src/common/video` plays AVI files with Cinepak video and PCM sound, as
Clue: Murder at Boddy Mansion's are (and many games of the 1990s).
`reluva-tool avi-frames` lists a file's streams, decodes all its frames
and writes those asked for as TGA files.

## Sources

- AVI: Microsoft's RIFF AVI form as publicly documented (AVI RIFF File
  Reference: `hdrl`, `avih`, `strl`, `strh`, `strf`, `movi`, `idx1`).
- Cinepak: the public description of the format on the MultimediaWiki
  ("Cinepak"). The decoder was written from that description alone, not
  from any other program's code.

Checked against the original: Clue's clip of Mrs. Peacock with the
candlestick (`MP_candle.avi`), decoded by Reluva, against the same clip
in a 640×480 recording of the game (the longplay listed in
[../engines/clue.md](../engines/clue.md), at 18:46): a mean difference of
4 in 255 per colour, the recording's compression's blur.

## AVI

A RIFF form of type `AVI `. Reluva reads:

- `LIST hdrl`: `avih` (its first value, microseconds per frame), and a
  `LIST strl` per stream: `strh` (type `vids` or `auds`, the handler,
  scale and rate, giving frames per second as rate / scale) and `strf`
  (for video a BITMAPINFOHEADER: width, height, compression `cvid`; for
  sound a WAVEFORMATEX: format 1 for PCM, channels, rate, bits).
- `LIST movi`: the data chunks in order, named by stream number and kind:
  `00dc` video (Clue's encoder also names key frames `00db`), `01wb`
  sound. `LIST rec ` groups may hold them. A video chunk of size 0 repeats
  the frame before. `idx1` isn't needed to play from the start.

Chunks are padded to even sizes. Some of Clue's files have bytes after
the RIFF form's end; they're ignored.

## Cinepak

All numbers big-endian. A frame:

| Size | |
|---|---|
| 1 | flags: bit 0 set, each strip keeps its codebooks from the last frame; clear, strip 2 on starts with strip 1's |
| 3 | the frame's length |
| 2 | width |
| 2 | height |
| 2 | number of strips |
| | the strips |

A strip: a 2-byte id (`0x1000` key, `0x1100` inter), a 2-byte size
(including these 12 bytes), then y1, x1, y2, x2. In Clue's files y1 and
x1 are 0 and y2 is the strip's height: strips stack top to bottom.
Then chunks, each a 2-byte id, a 2-byte size (with its 4 header bytes)
and data:

| Id | |
|---|---|
| `0x20xx`-`0x27xx` | a codebook: `0x02` in the high byte V1 (else V4), `0x01` partial, `0x04` grey |
| `0x30xx` | blocks, each V1 or V4 |
| `0x31xx` | blocks, each unchanged or V1 or V4 |
| `0x32xx` | blocks, all V1 |

A codebook has up to 256 entries, each four luminances (a 2×2 block) and,
unless grey, a signed U and V: 6 bytes, or 4. Colour is R = Y + 2V,
G = Y − U/2 − V, B = Y + 2U. A partial codebook gives a 32-bit mask before
each 32 entries; set bits (from the top) say which entries follow.

Blocks are 4×4, left to right, top to bottom in the strip. Bits for them
come 32 at a time (from the top) from the same data as their indices,
read when needed: in `0x30`, 1 for V4 and 0 for V1; in `0x31`, 0 for
unchanged, else a second bit, V4 or V1. A V1 block is one index: its
entry's four colours each fill a 2×2 quarter. A V4 block is four
indices, one entry for each quarter, a pixel per luminance.

## Clue's videos

44 files, all Cinepak at 15 frames a second with 16-bit mono PCM sound at
22,050 Hz: the intro, the deal and the six suspects' prison clips
(`CM_prison.avi` ...) at 320×240; the 36 suggestion clips, a suspect
with a weapon, at 200×400 (`CM_candle.avi` alone at 250×500). The
suggestion clips last 6.3 s: 1.4-2.7 s of picture (in the five
looked at), then black frames while the sound goes on, as the original
shows them.
