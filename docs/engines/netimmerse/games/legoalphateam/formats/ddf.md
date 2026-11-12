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

- **From the code** (November 2026): LoadComp reads them (the function
  that checks `FNT2`, at 0x1005e3d0), and a font class's methods (its
  vftable at 0x10081518) give each field its use. That settled the four
  fields after the page count, which had been read as a line height, a
  descent, an em height (a guess) and the space's advance: they are two
  sizes per page. See "Pages and sizes" below.

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
| 78 | 4 × pages | per page: its ascent and its descent, the rows above and below the baseline at the size its characters were drawn (Arial: 38 and 9, then 32 and 7). Both checked to be 0 to the page height |
| 78 + 4 × pages | 18 × n | a record per character, first to last (86 with two pages, as in all) |

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
| 14 | 0 in every file; the game scales it vertically, like the heights, but no font uses it |
| 16 | its page (unsigned) |

So a glyph is `right + left-field` columns wide and `above + below` rows
high, starting at (x, y) on its page. It is drawn with its top at the
baseline less `above`, from the pen less the left field. Arial's `l`, for
example, is `(170, 170, 30, 0, -3, 7, 10, 0, 0)`: four columns from x = 170,
drawn three to the right of the pen, which then moves 10.

On the options screen the game scales a whole string. There it drew
Eurostile Bold at 0.386 across and 0.29 down, with the texture rectangles
running from texel centre to texel centre, half a texel beyond the glyph.

## Pages and sizes

The pages are `<name>_<n>.TGA`, 256 × 256 32-bit TGA. They are white, with
the glyphs' coverage in alpha.

In every font, page 0 holds characters 33 to 127 and page 1 holds 128 to
255, **drawn at a smaller size** so that 128 glyphs fit. Each page's pair
says that size: in 11 of the 12 fonts page 1's pair is exactly its glyphs'
greatest extents above and below the baseline. Page 0's ascent is higher
than any glyph, being the Windows font's ascent with its internal leading.

The game draws text at a size its text window asks for
(`UITextWin::SetFontSize`). The font then works out, for each page, a
vertical scale of that size over the page's ascent plus descent, and a
horizontal one of that times a width factor (0.6 unless set). So both
pages come out the same height on screen. The font's ascent and descent are
page 0's, scaled.

**The space isn't in the font**: the characters start at 33. When the game
finds no glyph for a character (`UITextWin::PutChar`), a space moves the
pen by the text window's own space width, 18/35 of the size the window was
made with (`SetFontSize` doesn't change it). A tab moves it to the next tab
stop.

The viewer draws page 1's glyphs scaled to page 0's line, as the game does,
and uses a quarter of the line for the space.

## The fonts

ABADI MT CONDENSED, ARIAL, BOOKMAN OLD STYLE BOLD ITALIC, DOM CASUAL BOLD
ITALIC, EUROSTILE BOLD, EUROSTILE, HAETTENSCHWEILER, IMPACT, LUCIDA SANS
UNICODE BOLD, OCR A EXTENDED, OCRB and TIMES NEW ROMAN ITALIC. They are all
drawn with lines of 41 to 54 pixels (page 0's ascent and descent), covering characters 33 to 255 of
Windows-1252.
