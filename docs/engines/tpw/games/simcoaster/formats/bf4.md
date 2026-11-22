# SimCoaster: fonts (`.bf4`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/bf4.cpp` · **Checked on:** the US release's 35 fonts · **Confidence:** confirmed unless marked

## Overview

The game's fonts are in its language's folder, `Data/Language/American/`:
35 of them, for the menus (`MENUBIG.bf4`), the game's panels (`GAME5AA`
to `GAME12AA`, some in two forms, plain and `AA`), the money and date
counters (`CASH*`, `DATE*`), the staff's files (`PERSONNELFILE*`), the
postcards (`POSTCARD.bf4`) and others. Each has a glyph for every code
of the language's character set ([str.md](str.md)), so text from the
string tables is drawn code by code.

## Layout

All numbers little-endian.

### The header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `F4FB` |
| 4 | 1 | 1, or 2 for `CONSOLE6.bf4` and `COURIER8.bf4`: not worked out |
| 5 | 1 | Not worked out: near the font's height for most (13 for `GAME10.bf4`, whose glyphs reach 12 rows down), but 4 to 6 for `PERSONNELFILE*`, whose glyphs are 9 to 18 rows high |
| 6 | 2 | How many glyphs: 250, one for each code of the character set |
| 8 | 4 each | Where each glyph is, from the start of the file |

### A glyph

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | The Unicode character it draws |
| 4 | 4 | Its pixels' size, as stored |
| 8 | 4 | Its pixels' size at 4 bits a pixel: half its width times its height, rounded up |
| 12 | 4 | How its pixels are stored: 0, 4 bits a pixel; 2, 1 bit a pixel; 1, not worked out (below) |
| 16 | 2 | Its width, in pixels |
| 18 | 2 | Its height |
| 20 | 1 | How far right of the pen it starts |
| 21 | 1 | How far down from the line's top it starts |
| 22 | 2 | How far the pen moves on after it |
| 24 | | Its pixels |

The glyphs follow one another in the codes' order, each straight after
the one before. A code the font has no glyph of its own for has a `*`
(or, for the blanks, a space of no size): 57 to 72 codes in each font.

### The pixels

Row by row, with nothing between the rows, then zeros to a multiple of 8
bytes:

- **4 bits a pixel** (storage 0): the coverage, 0 for none to 15 for
  whole, the high half of each byte first. The `CASH*`, `SESH*` and
  `PERSONNELFILE*` fonts are all of this kind, the others in part.
- **1 bit a pixel** (storage 2): set or not, the top bit of each byte
  first. Most glyphs of the other fonts, even those named `AA`.
- **Storage 1**, in `POSTCARD.bf4` only (160 of its glyphs): fewer bytes
  than its 4-bit size, so packed in some way, not worked out.

## In Reluva

`Font` (`src/engines/tpw/formats/bf4.cpp`) reads a font into its glyphs,
each with its box and its pixels' coverage from 0 to 15; a glyph of
storage 1 keeps its box but not its pixels, and says so. `reluva-tool
tpw bf4-check <game folder>` reads every font; `reluva-tool tpw bf4-show
<game folder> <font> <text>` draws text in one, a character a pixel. The
game-files test runs the first (`tpw.gamedata.fonts`).

## Sources and checks

- **Where the facts come from:** the US release's fonts (November 2026):
  the glyphs' order by their characters matching the character set's;
  the size fields by their values against each glyph's width and height;
  the bit orders by drawing glyphs both ways and seeing which made
  letters; the offsets by the baseline the letters share (`e` 3 rows
  down and 6 high, `E` at the top and 9 high, `g` 3 down and 8 high).
- **How it was checked:** all 35 fonts read with `reluva-tool tpw
  bf4-check`; text drawn in several with `bf4-show` sits on its baseline,
  descenders below it. The unit tests (`tpw.unit.formats`) read a font
  built in the test, of both storages and the one not read.

## Open questions

- Storage 1, `POSTCARD.bf4`'s.
- The header's bytes 4 and 5.
- How far apart the game sets its lines: perhaps byte 5 *(guessed)*.
