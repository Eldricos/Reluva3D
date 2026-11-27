# SMUSH videos (`.san`)

## What it is

LucasArts' own video format of the 1990s, made by its SMUSH tools and
played by the games themselves. Reluva decodes it as Mysteries of the
Sith's cut scenes have it, in `src/video/smush.cpp`
(`reluva::smush::Decoder`), from the game's own player in `JKM.EXE` and
public write-ups of the container *(from the code)*: 23 files on the
game's CD, 640 by 300 or 350, 12 frames a second, with 22,050 Hz stereo
sound.

## What Reluva supports

- **Pictures:** codec 48 (all four kinds of frame the files use: raw,
  runs, blocks; its table of averages), 8-bit with a palette set by
  `AHDR` and `NPAL` and faded by `XPAL`; a frame object kept with `STOR`
  and drawn again with `FTCH`.
- **Sound:** `IACT` chunks of code 8, 16-bit stereo, at the rate
  `AHDR` gives. `takeAudio()` hands over what each frame carried.
- **Subtitles:** `subtitle()`, the frame's `TRES` number; what it names
  is the game's business.
- **Not handled:** codec 47 and other codecs, `IACT` of other codes, and
  the `PSAD`/`SAUD` sound of earlier games: no file of the games Reluva
  plays has them.

## The format

Chunks: a tag (4 bytes), a size (4 bytes, big-endian, the header left
out) and that many bytes, padded to an even length. Inside, numbers are
little-endian. The file is one `ANIM` chunk holding an `AHDR` and then a
`FRME` for each frame, each holding the frame's chunks in the order
they're acted on (`JKM.EXE` `0x569600` onwards).

### AHDR

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Version: 2 |
| 2 | 2 | Frames |
| 4 | 2 | 0 |
| 6 | 768 | The palette: 256 red, green and blue bytes, 0 to 255 |
| 774 | 4 | Frames a second: 12 in every file |
| 778 | 4 | Differs between files; not used by Reluva |
| 782 | 4 | The sound's rate: 22050 |

### A frame's chunks

| Tag | What it does |
|---|---|
| `FOBJ` | A frame object: its codec (a byte), a byte, its place and size (four words: x, y, width, height), 4 bytes, then the codec's data from byte 14 (`0x56a100`). The games register their codecs in a table (`0x923108`); `JKM.EXE` has only 47 and 48, and an object of another codec is passed over |
| `NPAL` | A palette of 768 bytes |
| `XPAL` | The palette's fade, by its word at 2 (`0x569ce9`): `0x200`, a palette (at `0x604`) and each channel's step (at 4, 768 signed words); 0, the steps alone; anything else, one step: each channel, kept times 128 in 16 bits, moves by its step, and the colour is that over 128 |
| `STOR` | Keeps the next frame object, for `FTCH` |
| `FTCH` | Draws the frame object `STOR` kept again, moved by the words at 2 and 4 (in 6-byte chunks) |
| `IACT` | Sound ([below](#sound)) |
| `TRES` | A subtitle, its string's number in the word at 16; the words before hold a place and a box: 160, 20, then 0, 0, 0, 641, 481, 0 in every file |

### Codec 48

The picture in 8x8 blocks, on two buffers the picture's size rounded up
to whole blocks: each frame of type 3 is decoded into one from the
other, which then holds the frame before (`0x567b00`).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Type: 0 raw pixels, 2 runs, 3 blocks; 1, 4 and 5 not in the files |
| 1 | 1 | Which motion table: 1 in every file but three keyframes' 0 |
| 2 | 2 | The frame's number in sequence; 0 clears both buffers |
| 4 | 4 | The pixels' count, for types 0 and 2 |
| 12 | 1 | Flags: 8, a table of averages follows (32,896 bytes: for each colour a, each b from a on, their average, the table symmetric); 2, the frame isn't shown; 0x10, shown as the average of both buffers; 1, with the number's bit 0, which buffer to decode into. 4 is set in every frame |

The data follows at 16, and the table of averages first where flag 8 is
set.

- **Runs (type 2):** a byte, half of it plus one the length; bit 0 set,
  that many of the next byte; clear, that many bytes as they are
  (`0x56aef0`).
- **Blocks (type 3):** the buffers swap first (when the number is odd,
  or flag 1 is clear, or flag 0x10 set), then a byte for each block,
  row by row (`0x56afc3`). A frame whose number isn't the last one's
  plus one isn't decoded (but 0).

| Byte | Block |
|---|---|
| 0 to `0xf6` | Copied from the other buffer, moved by the table's vector of that number |
| `0xf7` | Its 64 pixels |
| `0xf8` | Each 2x2 square copied, moved by its own 16-bit offset (32 bytes) |
| `0xf9` | Each 2x2 square moved by a vector of the table (16 bytes) |
| `0xfa` | Sixteen colours, each a 2x2 square |
| `0xfb` | Each 4x4 quarter moved by its own 16-bit offset (8 bytes) |
| `0xfc` | Each 4x4 quarter moved by a vector of the table (4 bytes) |
| `0xfd` | Four colours, for the 2x2 squares at (2, 2), (6, 2), (2, 6) and (6, 6) of the block; the other twelve squares averaged from them and from the pixels above the block and left of it (`0x56b7ef`) |
| `0xfe` | Copied, moved by a 16-bit offset |
| `0xff` | One colour, for the lower right square; the others averaged as for `0xfd`, starting from it (`0x56b71d`) |

An offset is in the buffer's bytes, from the block's own place.
Motion table 1 holds 255 vectors, vector *i* being (s[*i* mod 16],
s[*i* / 16]) with s = 0, 1, 2, 3, 5, 8, 13, 21, -1, -2, -3, -5, -8,
-13, -17, -21; the offset is y times the buffer's width plus x (its
data at `0x5a9a7c`, the offsets made at `0x567bba`).

The averages, for the squares of a 4x4 grid g (row by row) of `0xff`,
with a its colour, u the pixel above the block's column 7, and Lr the
pixel left of its row r: g15 = a; g7 = avg(a, u); g3 = avg(g7, u); g11
= avg(g7, a); g1 = avg(g3, L0); g0 = avg(g1, L0); g2 = avg(g1, g3); g5 =
avg(g7, L2); g4 = avg(g5, L2); g6 = avg(g5, g7); g9 = avg(g11, L4); g8
= avg(g9, L4); g10 = avg(g9, g11); g13 = avg(a, L6); g12 = avg(g13,
L6); g14 = avg(g13, a). For `0xfd`, with its four colours g5, g7, g13,
g15 and u3 and u7 the pixels above columns 3 and 7: g1 = avg(g5, u3),
g3 = avg(g7, u7), g11 = avg(g7, g15), g9 = avg(g5, g13), and the rest
as for `0xff`. avg is the table of averages, as the frames give it.

### Sound

`IACT` chunks of code 8 (their first word), after their 18 bytes,
carry a stream of packets that runs on from chunk to chunk
(`0x565db0`): a packet is its length (2 bytes, big-endian), a byte of
two shifts (the left channel's in its top four bits, the right's in the
bottom), then 1,024 stereo samples, left then right. A sample is a
signed byte shifted up by its channel's shift, or `0x80` and a whole
16-bit sample, big-endian. The first chunk carries 2.7 seconds of sound
ahead of the picture.

### What's shown

The decoded buffer is what the game shows, at the frame object's place:
in `JKM.EXE`, the picture at row 25 of its 640x480 screen *(confirmed)*.

## Sources and checks

- **Where the facts come from:** `JKM.EXE`'s player, read from its
  machine code (the addresses above); the container's chunks as the
  MultimediaWiki's SAN page and Kostya's "Looking at SMUSH/INSANE
  formats" describe them, which outline codec 48 but give no detail.
- **How it was checked:** the game's own frames, from `JKM.EXE` run in
  Reluva's emulator, against frames decoded by a prototype of the same
  rules: the opening's first 140 frames *(confirmed)*, each the same
  pixels, but for frames 104 to 117, where the game's palette fade
  leaves 0.7% of the pixels a step behind, as its screen redraws only
  what the picture changed; the decoder's frames against the
  prototype's, the same. Every file decoded to its end with as much
  sound as picture (`reluva-tool sith movies -all`), and a unit test of
  a file built in the test (`common.unit.video`).
- **Confidence:** from the code unless marked.

## Used by

- **`sith`**: Mysteries of the Sith's cut scenes
  ([mots/README.md](../../engines/sith/games/mots/README.md#its-files)),
  in the data viewer and `reluva-tool sith movies`.

## Open questions

- The `TRES` words before the number: where the game draws a subtitle.
- `AHDR`'s word at 778.
