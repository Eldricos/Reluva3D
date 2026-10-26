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
| 4 | u32 | size. Equal to the file's in 6 fonts; in `arial10b` and `sci20` it's 11 bytes short (meaning unknown). |
| 8 | u16 | 100 in all (a version?) |
| 10 | u16 | number of glyphs (223 or 224) |
| 12 | u16 | flags: 0, 1, 4 or 5; meaning unknown |
| 18 | u8 | line height in pixels |
| 20 | u16 | 32 in all (the first character?) |
| 28 | u32 | offset of the atlas: 2496 in all (32 + 224 × 11) |

The other bytes are 0 in every font.

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
