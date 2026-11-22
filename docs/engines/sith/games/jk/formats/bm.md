# Jedi Knight: pictures (`.bm`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/bitmap.cpp` · **Checked on:** US retail, all 146 pictures and the 27 fonts' · **Confidence:** confirmed unless marked

## Overview

The interface's pictures, in `Res1hi.gob`'s `ui\bm\`: the menus'
backgrounds (640x480, with a palette of their own), buttons and icons,
the HUD's pieces. 33 come in two copies, 8-bit and 16-bit (`icf_pro8.bm`
and `icf_pro16.bm`). A file holds one or more cels of the same size, a
button's states or an animation's frames. The fonts' glyphs are `.bm`
pictures too ([sft.md](sft.md)).

| Kind | Files |
|---|---|
| 8-bit, a palette of its own | 11: the menus' backgrounds |
| 8-bit, palette mode 1 | 72: the menus' buttons and icons |
| 8-bit, palette mode 0 | 22: the Force powers' icons, the saber's and the Force meter's pieces |
| 16-bit | 41 |

## Layout

All numbers little-endian.

### Header

128 bytes, 32 numbers of 4 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `BM  ` |
| 4 | 4 | Version: 70 (`0x46`) in every file |
| 8 | 4 | 0 in every file |
| 12 | 4 | Palette mode: 2 when a palette ends the file; else 0 or 1 *(what tells them apart is guessed: see [Behaviour](#behaviour))* |
| 16 | 4 | How many cels |
| 20 | 8 | Where the picture goes, x and y: set on 41, all within the 640x480 screen for the menus' pieces (`flok.bm`: 568, 344) and small for the HUD's (`sthealth.bm`: 21, 21) *(inferred)*; 0, 0 on the rest |
| 28 | 4 | The transparent colour: 0 in the 8-bit pictures, `0xf81f` (magenta) in the 16-bit ones |
| 32 | 4 | 1 in the 16-bit pictures, 0 in the 8-bit ones |
| 36 | 4 | Bits a pixel: 8 or 16 |
| 40 | 12 | 16-bit: red, green and blue's bits: 5, 6, 5 |
| 52 | 12 | 16-bit: where each starts: 11, 5, 0 |
| 64 | 12 | 16-bit: the bits each lacks of 8: 3, 2, 2 |
| 76 | 52 | 0 in every file |

### Cels

For each cel: its width and height (4 bytes each), then its pixels, row
by row from the top, one byte each (8-bit) or two (16-bit).

### Palette

When the palette mode is 2: 256 colours of three bytes, red, green and
blue, at the end of the file.

## Behaviour

The 8-bit pictures without a palette are drawn with another's. Palette
mode 1 is on the menus' pieces, which would take the menus' palette;
mode 0 on the HUD's, which would take the level's *(guessed)*.

## In Reluva

`parseBitmap` (`src/engines/sith/formats/bitmap.h`) reads one into a
`Bitmap`; `bitmapToImage` makes a cel RGBA, through its own palette or
one it's given, its transparent colour clear or not. `reluva-tool sith formats <install>` reads them
all.

## Sources and checks

- **Where the facts come from:** the 146 pictures and the 27 fonts'
  compared: every file's size is the header, its cels and its palette to
  the byte; the editing community's published write-ups of the format,
  which agree.
- **How it was checked:** every picture read by `reluva-tool sith
  formats`; `sith.unit.formats` on 8-bit and 16-bit pictures made by the
  test.

## Open questions

- What palette modes 0 and 1 mean to the game, and what the HUD's x and
  y are measured from.
- Whether the field at 32 says the transparent colour is used.
