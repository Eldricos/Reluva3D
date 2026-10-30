# LEGO Alpha Team: `.DDF` fonts

Source: the 12 `.DDF` files in the English retail `Etc.pac` (October 2026),
each beside two TGA pages (`ARIAL.DDF`, `ARIAL_0.TGA`, `ARIAL_1.TGA`).
This is how the layout was worked out:

- **The header.** The magic and the font's name can be read as text. Every
  file is 4,100 bytes, which leaves 18 bytes for each of the 223 characters
  from 33 to 255 after an 86-byte header. The nine 16-bit numbers before the
  characters were then matched against things that could be measured: the
  pages' size (256 × 256), the character range, the number of pages, and
  heights compared with the glyphs drawn on the pages.
- **Where each glyph is.** The boxes from the first two fields, with heights
  from the next two, were drawn over the pages. Each sat on its character.
- **The horizontal fields.** These were not clear from the pages, since
  glyphs sit close together there. They were taken from what the game does:
  the retail game was run in Reluva to its options screen, with the
  screen-space quads it handed to Direct3D written out. For each word there,
  each quad's texture rectangle and screen position were compared with that
  character's record.

`src/engines/netimmerse/ddf.{h,cpp}` reads them, and the viewer's Fonts page
draws them.

## The file

Little-endian.

| Offset | Size | What |
|---|---|---|
| 0 | 4 | `FNT2` |
| 4 | 64 | the Windows font's name (`Arial`), NUL-padded |
| 68 | 2 | page width (256) |
| 70 | 2 | page height (256) |
| 72 | 2 | first character (33) |
| 74 | 2 | last character (255) |
| 76 | 2 | pages (2) |
| 78 | 2 | line height (38 for Arial) |
| 80 | 2 | descent: rows below the baseline (9) |
| 82 | 2 | probably the em height: the line height less the internal leading (32) |
| 84 | 2 | the space's advance (7); the space isn't one of the glyphs |
| 86 | 18 × n | a record per character, first to last |

Each character's record, all signed 16-bit apart from the last two:

| Offset | What |
|---|---|
| 0 | x: the glyph's left column on its page |
| 2 | y: its top row |
| 4 | rows above the baseline |
| 6 | rows below it (negative if it ends above it, like `'`) |
| 8 | its left edge from the pen, negated: −1 means one column right of it |
| 10 | its right edge from the pen |
| 12 | how far the pen moves after it |
| 14 | 0 in every file (unsigned) |
| 16 | its page (unsigned) |

So a glyph is `right + left-field` columns wide and `above + below` rows
high, starting at (x, y) on its page. It is drawn with its top at the
baseline less `above`, from the pen less the left field. Arial's `l`, for
example, is `(170, 170, 30, 0, -3, 7, 10, 0, 0)`: four columns from x = 170,
drawn three to the right of the pen, which then moves 10.

On the options screen the game scales a whole string. There it drew
Eurostile Bold at 0.386 across and 0.29 down, with the texture rectangles
running from texel centre to texel centre, half a texel beyond the glyph.

## The pages

The pages are `<name>_<n>.TGA`, 256 × 256 32-bit TGA. They are white, with
the glyphs' coverage in alpha.

## The fonts

ABADI MT CONDENSED, ARIAL, BOOKMAN OLD STYLE BOLD ITALIC, DOM CASUAL BOLD
ITALIC, EUROSTILE BOLD, EUROSTILE, HAETTENSCHWEILER, IMPACT, LUCIDA SANS
UNICODE BOLD, OCR A EXTENDED, OCRB and TIMES NEW ROMAN ITALIC. They are all
drawn at a line height of 33 to 47 pixels, covering characters 33 to 255 of
Windows-1252.
