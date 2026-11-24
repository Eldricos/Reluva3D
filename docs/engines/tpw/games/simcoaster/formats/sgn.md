# SimCoaster: signs (`.sgn`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/sign.cpp`, `src/engines/tpw/ui/sign_painter.cpp` · **Checked on:** Land of Invention's 26 signs · **Confidence:** confirmed unless marked

## Overview

Beside an attraction's model with a sign is a `.sgn` file of its name
(`levels/science/rides/robot/robot.sgn` for `robot.MD2`): how the
attraction's name is lettered on it, in two lines, each in a TrueType
font of the game's (`fonts.wad`: `VICTP___.TTF`, "Victorian Plain"),
at a size and height, filled with a picture of colours. The model's sign
has two textures side by side, `sign1` and `sign2`, each a square
(the robot's 5.4 × 5.3 units, in front of its `signback2`); the files of
those names are placeholders, `sign1.fsh` a magenta "SIGN1", so the
game draws the name over them *(inferred)*. No description of the
format had been published; all that follows was worked out from the
files, setting all 26 side by side.

## Layout

All 17,337 bytes long; numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0x000 | 4 | 101: a version, perhaps *(guessed)* |
| 0x004 | 4 | 0 |
| 0x008 | 4 | 256: the canvas's height, perhaps *(guessed)* |
| 0x00C | 4 | 256 or 512: not worked out |
| 0x010 | 1 | 1 or 2: not worked out |
| 0x011 | 64 | The font's name, ending in 0: "Victorian Plain" |
| 0x051 | 256 | Its file, in `fonts.wad`: "VICTP___.TTF" |
| 0x155 | 0x1B4 each | The two lines (below) |
| 0x3A1 | | The lines' fills (below), to the end |

### A line

| Offset | Size | Field |
|---|---|---|
| +0 | 4 | How wide, percent: 96 to 127, 100 most often |
| +4 | 4 | Its top, on a canvas 256 high: −10 to 141 |
| +8 | 60 | A Windows font description (`LOGFONT`): its height, negative, the em in pixels (−20 to −174); its width (0, or 14 to 90); its weight (400; 160 to 1,000 on some); at +36, its font's name, which may be another of the game's ("Baylac") |
| +68 | 32 | Eight floats: 2.5, 1.25, 0.33, 0.33, 1.1, 10, 66, 270 on the robot's; not worked out |
| +100 | | The rest: not worked out |

### A fill

A line's fill is a picture: its rows (16), its width (128) and 4 (bytes
to a pixel), then its pixels, alpha, red, green, blue. The colours run
down it, and a little across: the robot's first line blue, lighter at
the top, its second gold *(inferred)*. The alpha is 255, but 0 in the
Tesla Tastic Spinner's second line's: not worked out.

## Behaviour

### Lettering it

The two lines of an attraction's name, its pair of `OBJECT_NAMES`
([building.md](../systems/building.md#their-names)), each in its font,
at its height and top, its fill stretched over its letters, centred on
a canvas 512 × 256 whose left half is `sign1` and right half `sign2`
*(guessed)*: the canvas's shape from the two square textures side by
side, its height from the tops and sizes, all of which fit it. The
font is found by its name, as Windows finds an installed font, its
file the fallback. Its letters are widened to the description's width,
as Windows makes a font's average letter that wide, and stretched by
the line's percent *(guessed)*. A recording close enough to read a sign
would settle these.

## In Reluva

`SignStyle` (`src/engines/tpw/formats/sign.cpp`) reads a sign;
`SignPainter` (`src/engines/tpw/ui/sign_painter.cpp`) letters it with
the game's fonts (`ttf::Font`) over the model's `sign1` and `sign2`,
which the park does as it reads a placed object's model. Bold weights
aren't drawn bold, the floats are unused, a line too wide for the
canvas is narrowed to fit, and where the canvas is clear the sign's
backing shows. `reluva-tool tpw sign-shot <game folder> <world folder>
<out.png>` letters all of a world's signs and lists their fonts and
lines; the game-files test runs it (`tpw.gamedata.park`).

## Sources and checks

- **Where the facts come from:** Land of Invention's 26 signs, side by
  side: the fields that change between them, the fonts' names against
  `fonts.wad`'s files, the fills drawn as pictures.
- **How it was checked:** every sign lettered with `sign-shot`, its
  lines legible and within the canvas. The unit tests
  (`tpw.unit.formats`) read a sign built in the test, and fail a cut one
  and one of another version. Not against the game: no recording shows
  a sign close.

## Open questions

- The header's 256 or 512 and its 1 or 2; the floats; the lines' left
  edge, if they aren't centred.
- How the game draws weight, and the fill's alpha.
- Whether the signs of the other worlds are the same.
