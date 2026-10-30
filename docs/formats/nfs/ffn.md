# Need for Speed III: fonts (`.ffn`, "FNTF")

Status: **implemented** (`src/engines/nfs/ffn.cpp`), and checked on all
8 retail fonts:

- `FeData/text`: `arial10b`, `cred`, `cred10i`, `credi`, `sci20`;
- `GameData/DashHud`: `cp16bi`, `cp1bi`, `small6`.

Worked out from hex dumps of the retail fonts, October 2026:

- **The atlas.** Each font's atlas size accounts for the rest of its file
  exactly, with 4 bits per pixel. The nibble order was found by drawing
  the atlas both ways: high nibble first gives clear letters, the other
  order doesn't.
- **The glyph fields.** Each glyph's box lies inside the atlas. Lines of
  text drawn with the offsets and advances (`reluva-tool ffn-render`)
  sit on a common baseline and are evenly spaced.

All values are little-endian.

## Header (32 bytes)

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `FNTF` |
| 4 | u32 | size: 32 + 11 × glyphs + the atlas, without the padding before the atlas, so 11 short in `arial10b` and `sci20` (223 glyphs in room for 224). The game doesn't read it. |
| 8 | u16 | 100 in all (a version?) |
| 10 | u16 | number of glyphs (223 or 224) |
| 12 | u16 | 0, 1, 4 or 5; the game doesn't read it |
| 14 | u16 | 0 in all. If bits 0-1 are 2 (or there are over 255 glyphs, or the first code is above 255), the game looks codes up as 16-bit |
| 16 | s8, s8 | x and y added to the pen where drawing starts |
| 18 | u8 | line height in pixels |
| 19 | s8 | extra gap between lines: a new line moves down 18 + 19 |
| 20 | u32 | offset of the glyph table: 32 in all |
| 28 | u32 | offset of the atlas: 2496 in all (32 + 224 × 11) |

The other bytes are 0 in every font. Bytes 16, 17 and 19 are 0 in all 16
retail fonts too; Reluva applies them anyway (`Font::draw`,
`Font::lineStep`), on the Strings and Fonts pages and in `ffn-render`.

The game's two loaders (`setfont` at `0x4ef520`, `FONT_create` at
`0x519f10`) were read in nfs3.exe (October 2026) for these fields.

## Glyphs (11 bytes each, from offset 32)

| Offset | Size | Meaning |
|---|---|---|
| 0 | u16 | character code, Windows-1252 |
| 2 | u8 | width |
| 3 | u8 | height |
| 4 | u16 | x in the atlas |
| 6 | u16 | y in the atlas |
| 8 | u8 | advance: how far the pen moves after the glyph |
| 9 | s8 | x offset from the pen |
| 10 | s8 | y offset from the line's top |

The table always has room for 224 glyphs:

- In the 223-glyph fonts, the last slot holds filler (code `0xADAD`).
- In some 224-glyph fonts, one record is all zeros.

Records past the count, and those of width 0, are ignored. `sci20` (the
front end's big title font) draws many punctuation marks and lower-case
letters as plain boxes, in its atlas.

## Atlas

The atlas is a picture block as in [.fsh](fsh.md) files, of type `0x7A`:
4 bits per pixel, high nibble first. Each pixel is coverage, from 0
(clear) to 15 (solid); Reluva scales it to 0-255.

## Tools

`reluva-tool ffn-render <file.ffn> <out.tga> [text]` draws every
character, or the text given (as Windows-1252 bytes).
