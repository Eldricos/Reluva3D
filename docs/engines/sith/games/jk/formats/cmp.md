# Jedi Knight: colormaps (`.cmp`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/colormap.cpp` · **Checked on:** US retail, all 21 colormaps · **Confidence:** confirmed unless marked

## Overview

Each level names a colormap: its palette of 256 colours, what each
colour becomes in less light, and a table for blending two colours. They
are in `Res2.gob`'s `misc\cmp\`. Sixteen are 82,752 bytes, with the blend
table; five (the `redmap`, `yellow` and `16red` ones) are 17,216 bytes,
without it.

## Layout

All numbers little-endian.

### Header

64 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `CMP ` |
| 4 | 4 | Version: 30 (`0x1e`) in every file |
| 8 | 4 | 1 when the blend table follows, 0 when not |
| 12 | 12 | Three floats: 1, 1, 1 in the sixteen with a blend table; 1, 0.2, 0.2 in the red maps and 1, 0.75, 0.2 in `01yellow.cmp`, their palettes' tint *(inferred)* |
| 24 | 40 | 0 in every file |

### Palette

256 colours of three bytes, red, green and blue, each 0 to 255.

### Light levels

64 tables of 256 bytes: what each colour becomes at that light level.
The colours grow brighter from level 0 to level 63, by the palette; in
`dflt.cmp` level 63 leaves every colour as it is.

### Blend table

When the header says so, 256 tables of 256 bytes, the colour two colours
make together *(inferred)*. Which of the two picks the table isn't
known.

## In Reluva

`parseColormap` (`src/engines/sith/formats/colormap.h`) reads one into a
`Colormap`. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 21 colormaps compared: their sizes,
  the header's fields, the palettes and the tables' brightness by level;
  the editing community's published write-ups of the format, which
  agree.
- **How it was checked:** every colormap read by `reluva-tool sith
  formats`; `sith.unit.formats` on a colormap made by the test.

## Open questions

- How the game uses the three floats, and the blend table's order.
