# id Tech 3: Raven's fonts (`fonts/*.fontdat`)

> **Status:** decoded · **Code:** `src/engines/idtech3/raven/fonts.cpp` · **Checked on:** Jedi Outcast 1.02a's three fonts, `anewhope`, `ergoec` and `ocr_a` (`reluva-viewer`'s Text page, November 2026) · **Confidence:** layout confirmed; how a line is laid out published

## Overview

The fonts Jedi Outcast's menus and HUD write with: each a picture of its
characters (`fonts/<name>.tga`, 512x256) and a `.fontdat` saying where
each of the 256 characters (Latin-1) is in it and how it sits on a line.
The game's code names a font (`anewhope`, the Star Wars lettering;
`ergoec`; `ocr_a`) and draws text with it at a scale.

## Layout

7,180 bytes, little-endian: 256 characters' records of 28 bytes, then
the font's sizes (and two bytes of padding):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | width in the picture, in pixels |
| 2 | 2 | height |
| 4 | 2 | how far the pen moves on after it |
| 6 | 2 | how far right of the pen it starts |
| 8 | 4 | how far above the line it stands its top |
| 12 | 16 | f32 s, t, s2, t2: its corners in the picture, 0 to 1 |

| Offset | Size | Field |
|---|---|---|
| 7168 | 2 | point size: a line's height |
| 7170 | 2 | height |
| 7172 | 2 | ascender |
| 7174 | 2 | descender |
| 7176 | 2 | 0 |

## Behaviour

Drawing text from a point (its top left), at a scale:

- The line the letters stand on is `height - descender / 2` below it,
  rounded.
- Each letter goes `offset` right of the pen, its top `baseline` above
  the line, `width` by `height`, the pen then moving on by its advance
  (each scaled and rounded); a letter with no width in the font is drawn
  as a full stop. A space only moves the pen.
- `\n` starts a new line a point size down; `^` and a digit set the
  colour (Quake III's eight, the digit's low three bits) and draw nothing.
- Drawing stops at the first letter or space that would pass a maximum
  width, if one is given.
- Sizes are kept whole only for fonts with "ergo" in their name; the
  others' are scaled as fractions.
- A text's width is the widest line's advances, colour codes counted as
  letters.

## In Reluva

`parseFontdat` reads a font; `Font::layout` places a text's letters as
above, and `textWidth` measures it. The data viewer's Text page draws the
game's string packages ([strings.md](strings.md)) with them, breaking
long lines at spaces to show them whole. The menus and HUD will use them
with the game's code.

## Sources and checks

- **Where the facts come from:** the record and the drawing from the GPL
  Jedi Outcast renderer (`qfiles.h`, `tr_font.cpp`), checked against the
  files: all three are 7,180 bytes, 256 records and the sizes.
- **How it was checked:** the game's text drawn in each font in the Text
  page reads right, accented letters included; a unit test
  (`idtech3.unit.formats`, "raven text") lays out a made-up font. Not yet
  compared with the original's screens.
