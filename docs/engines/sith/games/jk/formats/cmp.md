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
| 8 | 4 | Flags: bit 0, the blend table follows (`0x44288a`). 1 or 0 in the files. Bit 2, set in none, makes the game build a table of its own from the palette in place of reading the light levels (`0x44272d`) *(from the code)* |
| 12 | 12 | Three floats, red, green and blue: what the game multiplies a polygon's light by when it's drawn through this colormap (`0x43b9ee`; [Behaviour](#behaviour)) *(from the code)*. 1, 1, 1 in the sixteen with a blend table; 1, 0.2, 0.2 in the red maps and 1, 0.75, 0.2 in `01yellow.cmp` |
| 24 | 40 | 0 in every file |

### Palette

256 colours of three bytes, red, green and blue, each 0 to 255.

### Light levels

64 tables of 256 bytes: what each colour becomes at that light level.
The colours grow brighter from level 0 to level 63, by the palette; in
`dflt.cmp` level 63 leaves every colour as it is.

### Blend table

When the header says so, 256 tables of 256 bytes, for drawing see-through
things: the colour being drawn picks the table, and the colour already
on the screen the byte in it, the colour to draw there (`0x44ea48`,
`0x450b70`) *(from the code)*. Each byte is about a third of the drawn
colour and two thirds of the one beneath, to the nearest colour of the
palette: fitted over every table of the sixteen files, the drawn
colour's weight is 0.33 to 0.35 *(inferred)*.

## Behaviour

`JK.EXE` reads a colormap at `0x442660`. A file that isn't a colormap,
or one it hasn't the memory for, becomes a grey one named `GREYRAMP.CMP`
with the floats 1, 1, 1 (`0x443190`) *(from the code)*.

Where the game gives the corners of a polygon a red, green and blue
light, a polygon drawn through a colormap other than the one it has made
current (`0x442560`) has each of the three multiplied by that colormap's
floats, so the red maps make what's drawn through them red
(`0x43b9d6` to `0x43ba2e`, `0x43bf88` to `0x43bfcb`) *(from the code)*.
Their palettes and light levels are red already *(inferred)*.

## In Reluva

`parseColormap` (`src/engines/sith/formats/colormap.h`) reads one into a
`Colormap`. The data viewer's Colormaps page shows the palette, the light
levels and the blend table; its Materials page draws the materials
through a chosen colormap, at a chosen light level. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 21 colormaps compared: their sizes,
  the header's fields, the palettes and the tables' brightness by level;
  the blend tables fitted against their palettes; the editing
  community's published write-ups of the format, which agree;
  `JK.EXE`'s reader (`0x442660`) and writer (`0x442a50`) of the format,
  and its drawing code.
- **How it was checked:** every colormap read by `reluva-tool sith
  formats`; `sith.unit.formats` on a colormap made by the test.

## Open questions

- Which renderer the three floats' tint is used in: the code found
  works out light as red, green and blue, which the 8-bit drawing
  doesn't need; seeing the red maps drawn would settle it.
