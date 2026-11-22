# MDEC frames

## What it is

The PlayStation's movie pictures: each frame stands alone, coded as its
MDEC chip decodes it, a cousin of MPEG-1's intra frames. EA used the
same frames in PC games of the time, in `mTCD` chunks between the sound
blocks of an `SCHl` stream. Reluva decodes them in `src/video/mdec.cpp`
(`reluva::mdec::Decoder`); MPEG-1's tables it shares with NFS III's MAD
decoder are in `src/video/mpeg1.h`.

## What Reluva supports

- **Version 2 frames**, of any size that is a multiple of 16 each way
  (the container gives the size; the frame doesn't). Output: the planes
  (luma and half-size Cb and Cr) and RGBA32.
- Not version 3 (the PlayStation's later DC coding), which no game
  Reluva has uses.

## The format

A frame is an 8-byte header, then its bits:

| Offset | Size | What |
|---|---|---|
| 0 | u16 | A count; not needed to decode, and what it counts isn't checked |
| 2 | u16 | `0x3800` |
| 4 | u16 | The quantiser scale, 1-63 |
| 6 | u16 | The version: 2 |

- **The bits** come as 16-bit little-endian words, each read from its
  top bit.
- **Macroblocks** of 16x16 pixels go down the picture's columns, top to
  bottom, then left to right. Each is six 8x8 blocks: Cr, Cb, then luma
  top left, top right, bottom left and bottom right.
- **A block:** its DC term, 10 bits signed, times 2; then run and level
  pairs by MPEG-1's AC table (ISO/IEC 11172-2, Table B.5c), in zigzag
  order, until `10` ends the block. Escape is `000001`, then a 6-bit run
  and a 10-bit signed level. An AC term is level × scale × MPEG-1's
  default intra matrix at its place, shifted right by 3.
- **Pixels:** the 8x8 inverse DCT; 128 added to luma. Colours are
  ITU-R BT.601's YCbCr in full range (luma 0-255, chroma about 0), each
  chroma sample covering 2x2 pixels.

## Sources and checks

- **Where the facts come from:** the header's `0x3800` and version,
  which mark the PlayStation's MDEC frames as they're publicly described;
  MPEG-1's tables from the standard; and the files: the column order, the
  DC's doubling and the AC's shift by 3 were settled by decoding NFS II
  SE's movies until their pictures came out whole *(inferred)*, then
  confirmed by the comparison below. FFmpeg's `mdec`
  decoder (LGPL) was not read; its output was compared, below.
- **How it was checked:** `common.unit.mdec` decodes frames built in code
  (the macroblocks' order, the DC's scale, an escaped AC term, bad
  frames). On NFS II SE's 15 movies (November 2026), every one of their
  9,340 frames decodes; frame 100 of each, and every 50th of `F355AV`,
  match FFmpeg's decoding to within 8 in 255 per colour, a mean
  difference of 0.25-0.56: rounding, its transform being integer and
  this one floating point.
- **Confidence:** confirmed by the pictures and the comparison.

## Used by

- **`nfs`**: Need for Speed II SE's movies.
