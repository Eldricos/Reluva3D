# Star Trek: Starfleet Command II: anti-aliased fonts (`.bft`)

> **Status:** decoded · **Code:** `src/engines/sfc/formats/bft_font.cpp` · **Checked on:** English retail 1.0, all 126 files · **Confidence:** confirmed unless marked

## Overview

`Assets/aafonts/` holds 126 fonts: Arial, and the lettering of the
Federation, the Klingons, the Romulans, the Gorn, the Hydrans, the
Lyrans, the Mirak and the ISC, each in 14 sizes from 5 to 24. Each glyph is a bitmap of coverage, 0 to 255, so text is blended
onto what's behind it.

## Layout

All numbers are little-endian, 32 bits.

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | line height (8 for size 5, up to 46 for size 24) |
| 4 | 4 | half the line height in every file; what it's for is unknown |
| 8 | 4 | the number of glyphs, 252 to 256 |

### A glyph

For each character from 0 up, in order:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | x: from the pen to the bitmap's left (0 in 10,129 glyphs with a bitmap, 1 in 872) |
| 4 | 4 | y: from the line's top to the bitmap's top |
| 8 | 4 | advance: how far the pen moves |
| 12 | 4 | width |
| 16 | 4 | height |
| 20 | width x height | coverage, a byte a pixel, rows top down |

Characters 0 to 31 have empty glyphs. The glyph for a code is that
code's character in Windows-1252: 128 is €, 140 Œ, 233 é. The fonts'
codes past what they draw (from 253, mostly) aren't there. An empty
glyph's x and y are meaningless (-2 and -3 for the space, odd values
for a few others).

## In Reluva

- **Reading it:** `BftFont` (`src/engines/sfc/formats/bft_font.h`), which
  also measures and draws text into an RGBA32 surface, blending by
  coverage.
- **The data viewer:** the Fonts page shows each font's characters from
  32 up, with their boxes if asked, and lines of sample text.
- **Commands:** `reluva-tool sfc fonts <game folder>` reads every font.

## Sources and checks

- **Where the facts come from:** the files themselves: the records'
  sizes, and glyphs' bitmaps seen as letters ('A' at 65 in Arial 10, 9
  pixels wide and 12 high).
- **How it was checked:** all 126 files read to their last byte, each
  holding the header's count of glyphs; the characters of Arial's, the
  Federation's, the Gorn's and the Hydrans' fonts drawn by the data
  viewer and seen to be in Windows-1252's order (the other races' aren't
  looked at yet).
- **Unit test:** `sfc.unit.formats` (font), on a font built in the test.

## Open questions

- The header's second number.
- How the game picks a size and a race's lettering.
