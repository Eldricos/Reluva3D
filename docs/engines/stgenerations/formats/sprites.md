# Sprite sets (.GS2, .GSF)

> **Status:** decoded · **Code:** `src/engines/stgenerations/formats/sprite.h` · **Checked on:** the 2 CD Windows release, two GS2 samples · **Confidence:** confirmed unless marked

## Overview

These are billboard sprites for the 3D away missions: `verid1/001.gs2` is a crew member
(standing, walking, beaming in), and `008.gs2` is a flame.

## Layout

### GS2 (`.GS2`)

```
uint32  2                           version (GSF files have 1, see below)
uint32  frame count                 (25 in verid1/001.gs2, 11 in 008.gs2)
uint32  0
uint32  sequence count              (16 in both)
uint32  0
uint16  ?, uint16 ?                 (128, 256 / 64, 128: a size hint)
sequences[sequence count]:
  uint8 n, uint8 frame index[n]     (001: [0..4], [], [5..19], [20..24], 12 empty)
frames[frame count]:
  uint32 half width at scale 1.0, 16.16 (00 9c 00 00 = 0.609 for 78 columns)
  uint32 column count
  uint32 column height              (256 in 001, 128 in 008)
  uint8  pixels[columns][height]    column-major, BOTTOM row first, 0xFF transparent
  uint8  tables[3][columns]         per-column hints; the first is twice the
                                    topmost opaque row. Not needed to draw.
```

Both samples parse to exactly their end with the advertised frame count.
The half width is always columns / 128 for 256-high frames and columns / 64
for 128-high ones. Actors scale it (see Actors below). As with overlays,
there's no palette.

### GSF (`.GSF`): not used

These start like GS2 but with version 1:
`01 00 00 00 33 00 00 00 f8 06 00 00 10 00 00 00 18 00 00 00 80 00 00 01 ...`
(51 frames, with something in the third dword). They're refused by the GS2
decoder rather than misread. Only ANTILIOS.DAT has two, older than the
`011.gs2` and `012.gs2` beside them, and the game only loads `NNN.gs2`
(`0x4C01B0`), so they're leftovers.

## Sources and checks

From the notes read off `sgens.exe` and the game's files, the 2 CD Windows release, while the engine was STG-Engine: [actors-draw](../research/actors-draw.md), in [research/](../research/README.md). [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) says how they were made. All values are little-endian.
