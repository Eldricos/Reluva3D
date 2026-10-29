# Intel Indeo Video Interactive 5 (IV50)

`src/common/video/indeo5.{h,cpp}` decodes it, for LEGO Alpha Team's intro
movie.

## Where this comes from

Intel never published the format. Its one full public description is
FFmpeg's decoder (`libavcodec/indeo5.c`, `ivi.c`, `ivi.h`, `ivi_dsp.c`,
`indeo5data.h`; LGPL 2.1 or later, release 6.1 used), worked out by its
authors from the bitstream. MultimediaWiki's Indeo 5 page outlines it.
Reluva's decoder was written from that code as a reference, in Reluva's
own structure and words; the project chose this for Indeo 5 (the LGPL is
compatible with Reluva's GPL 3). Two things were taken as they are:

- **The format's fixed tables**: the macroblock and block Huffman code
  descriptors, the nine run/value maps, the quantisation base and scale
  tables, and the standard picture sizes. A decoder must use these exact
  numbers. `src/common/video/indeo5_tables.cpp` was generated from FFmpeg's
  source by a script and says so.
- **The arithmetic**: the slant transforms' integer steps, motion
  compensation's rounding and the 5/3 wavelet recomposition. Anything else
  would decode different pixels.

**Checked:** all 1,631 pictures FFmpeg outputs for LEGO Alpha Team's
`lmiintro.avi` (its last frame isn't output) decode byte for byte the same
(YUV 4:1:0 planes compared). That's 4 ms a frame on this container's
processor. The intro uses one luma band, one tile, half-pel motion,
16 × 16 luma macroblocks of 8 × 8 blocks and 4 × 4 chroma blocks. The
four-band (scalable) wavelet mode and tiling are written but haven't met
a file. A test builds a small frame by this description.

## The bitstream

Bits are read from each byte's lowest bit up; a field's first bit read is
its lowest. Huffman codes go the other way: their first bit is their
highest.

**Huffman codes** are described by rows. Row *i*'s codes are *i* one bits,
a zero bit (except in the last row), then `xbits[i]` bits of an index. The
symbols number the rows' codes in order, up to 255. A code is one of 8
fixed descriptors, or (selector 7) sent: a 4-bit row count, then a 4-bit
`xbits` each. Macroblock signals (quantiser deltas, motion vectors) and
each band's block coefficients have their own code.

**Picture header**:

- a start code (5 bits, `11111`);
- the frame type (3 bits): 0 key, 1 inter, 2 scalable inter, 3 inter that
  nothing refers to, 4 null (no data; the picture stays);
- the frame number (8 bits).

A key frame's **GOP header** follows:

- flags (8 bits): 1 means a 16-bit size follows; 0x20 password-protected;
  0x40 tiles of 64 << (2 bits); 2 YV12, unsupported; 8 transparency;
- the luma bands (2 bits × 3 + 1) and chroma bands (1 bit × 3 + 1);
- the size: an index (4 bits) into the standard sizes, or 15 and then
  height and width (13 bits each);
- for each luma band, then each chroma band:
  - half-pel motion (1 bit);
  - whether macroblocks are blocks (1 bit; else twice their size);
  - whether blocks are 4 × 4 (1 bit; else 8 × 8);
  - an extended-transform bit (0);
  - two zero bits;
- with transparency, 3 zero bits and an optional 24-bit colour;
- alignment, 23 bits, and an optional extension of 16-bit words while
  their top bit is set; alignment.

The transform, scan and dequantisation matrix go by plane and band:

| Band | Transform | Scan |
|---|---|---|
| luma 0 | 2D slant 8 × 8 | zigzag |
| luma 1 | slant along rows | down columns |
| luma 2 | slant down columns | along rows |
| luma 3 | none | along rows |
| chroma | 2D slant 4 × 4 | 4 × 4 diagonal |

Then, unless the frame is null:

- frame flags (8 bits): 1 a 24-bit size; 0x10 a 16-bit checksum; 0x20 an
  extension (8-bit lengths and that many bytes, to a 0); 0x40 the
  macroblock code is given; 8 quantiser deltas even without coded blocks;
  0x80 each band gives its size;
- 3 bits; alignment.

**Each band**, luma then the two chroma planes (Cr, then Cb):

- flags (8 bits): 2 inherit motion vectors from the first luma band; 4
  quantiser deltas present; 8 inherit them; 0x10 corrections to the
  run/value map follow (a count and pairs of symbols to swap); 0x40 the map
  is chosen (3 bits; else map 8); 0x80 the block code is given;
- a checksum flag and checksum, and the band's quantiser (5 bits);
- then each tile:
  - a bit: if set, the tile is empty, and copied from the reference (moved
    by inherited vectors if any);
  - else its size in bytes, counted from the tile's start (a flag, 8 bits,
    or 255 and 24 bits); then its macroblocks' info, then their blocks.

**Macroblock info**, each in turn:

- an empty bit: if set, the macroblock is inter, with nothing coded (an
  optional quantiser delta);
- else:
  - its type (1 bit, inter frames only);
  - which blocks are coded (1 or 4 bits);
  - a quantiser delta;
  - for inter macroblocks, motion vector deltas (y, then x) on a running
    vector.

The deltas are Huffman symbols with the sign in the lowest bit.

**Blocks:** a coded block's symbols are each a run of zeros and a value
from the run/value map, until its end-of-block symbol. An escape symbol is
followed by three symbols: run − 1, the value's low six bits and its high
bits. Each value is dequantised by the base matrix and the quantiser
level's scale:

```
q = base[pos] × scale ÷ 512
v = v × q ± ((q ^ 1) − 1) ÷ 2   (when q > 1)
```

Then:

- intra blocks of the 2D transforms carry their DC on from the block
  before;
- the inverse transform runs: columns then rows, the second pass halved;
- an inter block adds the reference block at its motion vector, averaged
  over two or four samples for half-pels.

Uncoded intra blocks repeat the running DC; uncoded inter blocks copy the
reference.

**Buffers:** each band keeps its picture as 16-bit samples, around 0,
with three buffers switched as frames come: key and inter frames write
one and refer to the other, and frames nothing refers to don't switch.

**Output:** each plane + 128, clipped; the four luma bands are first put
back together with the 5/3 wavelet filters. Chroma is a quarter the size
each way (YUV 4:1:0). The movie player converts to RGB by BT.601 (video
range).
