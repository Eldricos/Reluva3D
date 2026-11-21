# FLIC animations (`.fli`, `.flc`)

## What it is

Autodesk Animator's animation format, used by many games of the 1990s
for small animations. Reluva decodes it in `src/video/flic.cpp`
(`reluva::flic::Decoder`).

## What Reluva supports

- **FLI and FLC files:** FLI's 70ths of a second and FLC's milliseconds
  a frame; any size; 8-bit pictures with a palette.
- **Every drawing chunk:** 64- and 256-level colours, a black frame, a
  raw picture, a run-length picture, and the changes since the last frame
  by bytes (FLI) or by 16-bit words (FLC).
- **From a file or frame by frame:** `load()` reads a file's frames in
  turn; `loadHeader()` and `decodeFrame()` take a header and frames that a
  game hands over itself (LEGO Island streams its frames from its SI
  archives).
- **Not handled:** the postage stamp (a frame's thumbnail) and other
  chunks of a program's own are passed over; so is the loop frame some
  files have after their last, and the chunk of Animator's settings before
  the first.

## The format

All values little-endian.

### The file

A 128-byte header, then the frames. The header's fields used:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | the file's size |
| 4 | 2 | `0xAF11` FLI, `0xAF12` FLC |
| 6 | 2 | frames, not counting a loop frame after them |
| 8 | 2 | width |
| 10 | 2 | height |
| 12 | 2 | bits a pixel: 8 (0 in some FLI files) |
| 16 | 4 | time a frame: 70ths of a second (FLI), milliseconds (FLC) |
| 80 | 4 | FLC: where the first frame is |
| 84 | 4 | FLC: where the second frame is (where a loop goes back to) |

### A frame

A 16-byte header (its size, `0xF1FA`, how many chunks), then the chunks,
each its size (6-byte header included), type and data:

| Type | Chunk | Data |
|---|---|---|
| 4 | 256-level colours | a packet count; each packet: entries to skip, entries to set (0 for 256), 3 bytes each |
| 11 | 64-level colours | as type 4, the levels 0 to 63 |
| 7 | changes by words (FLC) | a count of lines changed; for each, words before its packet count: top bits `11` skip lines (the word, negative, is the count), `10` set the line's last pixel to the low byte; then packets: columns to skip, then a signed count of words, copied if positive, the next word repeated if negative |
| 12 | changes by bytes (FLI) | lines to skip, lines changed; for each, a packet count; packets: columns to skip, then a signed count of bytes, copied if positive, the next byte repeated if negative |
| 13 | black | none: every pixel 0 |
| 15 | run-length picture | each line: a packet count (not to be trusted, ignored), then runs till the line is full: a positive count repeats the next byte, a negative one copies that many bytes |
| 16 | raw picture | every pixel, top line first |
| 18 | postage stamp | a thumbnail, not drawn |

## Sources and checks

- **Where the facts come from:** the format's published descriptions
  (Autodesk's Animator Pro file format notes, and the write-ups based on
  them), read for facts only; the code is new.
- **How it was checked:** `common.unit.video` decodes a FLIC built in the
  test, with every drawing chunk, and checks its pixels and palette. All
  816 of LEGO Island's FLC animations (English 1.0, rebuilt from its SI
  archives) decode to their last frame, and were compared pixel for pixel
  with FFmpeg's decoding of every frame: 798 the same throughout; 17
  differ only in the last column, which FFmpeg never changes after the
  first frame though their "last pixel" words set it (Reluva sets it, as
  the format says); one differs in 2 pixels of its first frame, whose raw
  picture chunk is sized 2 bytes short of its picture (Reluva reads them
  from the rest of its frame, which holds them).
- **Confidence:** published, and confirmed on LEGO Island's files.

## Used by

- **`legoisland`**: the game's FLC animations, in its SI archives
  ([si.md](../../engines/legoisland/formats/si.md#each-formats-pieces)).
