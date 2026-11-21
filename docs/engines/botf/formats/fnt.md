# Star Trek: Birth of the Federation: fonts (`.fnt`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/font.cpp` · **Checked on:** all 21 fonts of the English retail `stbof.res`, each loaded with every row's expected number of cells (October 2026) · **Confidence:** confirmed unless marked

## Overview

21 fonts in `stbof.res`: `human`, `klingn`, `romuln`, `ferngi` and `cardsn`
(one per race, see [stbof-res.md](stbof-res.md#races)) at 9, 15, 20 and 28
point, and `generic9.fnt`. Each is a header, the glyphs' advances and a
glyph sheet. Every glyph's place and advance is known, and how to draw it;
three header fields and how the game colours text are open.

## Layout

All values little-endian.

| Offset | Type | Field | Values |
|---|---|---|---|
| 0 | `u8` | line height | the sheet's row pitch: glyph height + 1 (10, 16, 21, 29) |
| 1 | `u8` | the gap after a glyph's cell | 0 in the 9-point fonts, 1 in the others, as the galaxy map's names show *(inferred)* |
| 2 | `u8` | space advance | 4 (9 and 15 point) or 5 (20 and 28); no glyph is a space *(guessed)* |
| 3 | `u8` | ? | 1 (9 and 15 point), 2 (20), 3 (28) |
| 4 | `u16` | ? | 10 in every font |
| 6 | `u16` | number of glyphs | 120 |
| 8 | `u16` | sheet width | |
| 10 | `u16` | sheet height | |
| 12 | `u32` | sheet size | the TGA's size after its 18-byte header |
| 16 | `u8` | ? | 0 in the 9-point fonts, 1 in the others |
| 17 | `u8` | ? | 6; 7 in `generic9.fnt` |
| 18 | `u16` | ? | 86 in every font |
| 20 | `u16[120]` | advances | per glyph, in sheet order: its width in pixels + 1 |
| 260 | | the glyph sheet | a 16-bit TGA |

The sheet is an uncompressed 16-bit TGA (type 2, rows from the bottom up),
some with a 26-byte TGA 2.0 footer (the sheet size counts it). Its height
is 5 x the line height + 1 (the 9-point sheets have a row or two more).

### The glyph sheet

Five rows of cells, each cell framed by 1-pixel lines of one colour (white
in `generic9.fnt`, green `0x03F1` in the race fonts) that also run along
the top of the sheet and between the rows. Row r's cells start at
y = r x line height + 1 and are line height - 1 tall. Inside a cell the
background is black; the glyph is drawn in its own colours: two colours in
the 9-point fonts, grey levels (anti-aliasing) in the larger ones.

Each cell is as wide as the header's advance - 1, except the first cell of
rows 2 to 5 (`A`, `a`, `` ` ``, `ä`), which is a pixel wider (in both
whole fonts read). Reluva finds the cells by their frame lines (a column
of frame colour all the way down a row ends a cell): in both whole fonts
120 cells, in rows of 10, 26, 26, 32 and 26.

The game doesn't draw a cell's last row, which only descenders reach: in
screenshots the bottom row of `g` and `y` is blank where the sheet has it.

The game advances by the cell's width + 1, so for those four glyphs a
pixel further than the header says. Drawn with the header's advances,
the text in screenshots of the Hall of Fame and Load Game screens drifts a
pixel to the left after every `a` and `A`; with the cells', each of its
glyphs lands on the game's own pixels. In the 9-point fonts the game
advances by the cell's width alone, as their byte 1 is 0 where the
others' is 1: the galaxy map's names, in `human9.fnt`, land on the
game's pixels so, ten of them, and a pixel apart further each glyph with
the cells' width + 1
([systems/galaxy-screen.md](../systems/galaxy-screen.md#the-map)).

Wrapped to a width, a line takes a word only while it and the space
after it (none after a paragraph's last word) stay narrower than the
width: the galaxy screen's pulsars' descriptions break where the line
and its space would fill the box exactly. A line broken at two spaces
starts with the second.

The glyphs, in order (the header's advances are in the same order):

| Row | Glyphs |
|---|---|
| 1 | `1 2 3 4 5 6 7 8 9 0` |
| 2 | `A` to `Z` |
| 3 | `a` to `z` |
| 4 | `` ` - = \ [ ] ; ' , . / ~ ! @ # $ % ^ & * ( ) _ + \| { } : " < > ? `` (a US keyboard's keys, unshifted then shifted) |
| 5 | `ä â à ê è é ï î ö ô ü ñ Ç ç Ä Ö Ü ß ® © ™ … — ¿ ¡` and a short dash, taken as `–` *(inferred)* |

`generic9.fnt` leaves row 4 empty but for the apostrophe: its other cells
are framed and blank.

## In Reluva

`Font` (`src/engines/botf/formats/font.cpp`) reads the 120 glyphs of every
`.fnt`, found by their frames, with their advances, and draws UTF-8 text.
reluva-viewer's Birth of the Federation Fonts page shows each glyph sheet
and sample text, drawn as stored. The unit tests (`botf.unit.fonts`) cover
fonts and line wrapping.

## Sources and checks

- **Where the facts come from:** the first 2 KB of all 21 fonts in the
  English `stbof.res`, and two whole fonts, `generic9.fnt` and
  `human15.fnt` (`tools/botf-re/botf_fonts.py`), October 2026. Glyph cells
  were found by their frame lines in both whole fonts and compared with the
  header; the characters were read from the glyphs drawn large.
- **How it was checked:** all 21 fonts loaded in Reluva's viewer, every row
  with its expected number of cells. The advances and the shading, against
  screenshots of the game's Hall of Fame, Load Game and options screens
  (November 2026), drawn by Reluva from the English `stbof.res`: the text
  lands on the game's pixels, each grey of the glyph sheet always giving
  the same colour on the game's 16-bit screen.

## Open questions

- How the game colours text exactly. It tints the grey glyphs: in the
  screenshots each grey gives one colour, the brightest grey the text's
  colour and the rest about in proportion, as Reluva draws them (within the
  16-bit screen's rounding). In `human15.fnt`'s `8` and `B` a few pixels
  differ from the sheet's; why isn't known.
- Bytes 3, 16 and 17 and the values 10 and 86. Byte 2 is taken as the
  space's advance (there is no space glyph); the running game will show
  whether that's right.
- Which code page the game's text uses for row 5's characters
  (Windows-1252 has all of them).
