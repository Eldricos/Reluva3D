# Jedi Knight: fonts (`.sft`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/font.cpp` · **Checked on:** US retail, all 27 fonts · **Confidence:** confirmed unless marked

## Overview

The interface's fonts, in `Res1hi.gob`'s `ui\sft\`: 17 for text (`small`,
`med`, `large`, the credits', the messages'), and 10 of digits for the
HUD's health, shields and ammunition. Each is a strip of glyphs side by side in
a `.bm` picture ([bm.md](bm.md)), and where each character's glyph is.

## Layout

All numbers little-endian.

### Header

40 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `SFNT` |
| 4 | 4 | Version: 10 in every file |
| 8 | 4 | 0 to 3: not yet understood |
| 12 | 4 | 0 to 8: not yet understood, perhaps the space between glyphs *(guessed)* |
| 16 | 4 | 0, 32 or 63 (a space or `?`): perhaps the character drawn for one the font lacks *(guessed)* |
| 20 | 4 | How many ranges of characters |
| 24 | 16 | 0 in every file |

### Ranges

For each range: its first and last character (2 bytes each), then for
each character in it its glyph's x in the strip and its width (4 bytes
each). The text fonts have 14 ranges: 32 (or 33) to 126 or 122, then
accented letters from 161 to 252, in Windows' Western European code page
*(inferred)*. The digit fonts have one, `0` to `:`.

### The strip

A `.bm` picture to the end of the file, with one cel: the glyphs side by
side, as high as the font. The last glyph ends at the strip's right edge
in every font.

## In Reluva

`parseFont` (`src/engines/sith/formats/font.h`) reads one into a `Font`.
`reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 27 fonts compared: their ranges and
  glyphs read to where the picture starts.
- **How it was checked:** every font read by `reluva-tool sith formats`,
  each glyph inside its strip; `sith.unit.formats` on a font made by the
  test.

## Open questions

- The header's fields at 8, 12 and 16.
