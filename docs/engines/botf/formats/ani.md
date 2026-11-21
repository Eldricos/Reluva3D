# Star Trek: Birth of the Federation: animations (`.ani`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/animation.cpp` · **Checked on:** all 65 `.ani` files of the English retail `stbof.res`, and screenshots of the galaxy screen's strip (November 2026) · **Confidence:** confirmed unless marked

## Overview

65 animations in `stbof.res`: the planets (`gmel.ani`, `bnom.ani`; the
last letter `l`, `m` or `s` for their size), Sol's own planets
(`merc.ani` to `plut.ani`), the stars (`s-y-.ani` and the other
colours), and the stellar objects (`ns.ani` the neutron star, `blkh.ani`,
`worm.ani`, `rpls.ani`, `xp-.ani`). The galaxy screen shows them turning
in the strip under the map ([systems/galaxy-screen.md](../systems/galaxy-screen.md#the-strip)).
Each is a 16-byte header and a 16-bit TGA holding every frame.

## Layout

All values little-endian.

| Offset | Type | Field | Values |
|---|---|---|---|
| 0 | `u16` | frame height | 12 to 64 for planets, 100 for stars and stellar objects |
| 2 | `u16` | frame width | the same as the height for planets and stars; 300 for stellar objects |
| 4 | `u16` | number of frames | 24 for planets, 36 for stars, 8 to 48 for stellar objects |
| 6 | `u16` | ? | the number of frames again, but 5 in `blkh.ani`, `worm.ani`, `rpls.ani` and `xp-.ani` |
| 8 | `u16` | sheet width | the TGA's |
| 10 | `u16` | sheet height | the TGA's |
| 12 | `u32` | data size | the TGA's pixels and its 26-byte footer, not its 18-byte header |
| 16 | | the frames' sheet | a TGA: an 18-byte header, the pixels, a TGA 2.0 footer |

The height comes first: in the five stellar objects' files the first word
is 100 and the second 300, and only 300 across by 100 down fits their
sheets (`ns.ani`'s 1200x200 holds its 8 frames as 4 by 2). Every file's
frames fit its sheet read so.

The sheet is an uncompressed 16-bit TGA (type 2, 5 bits a colour, rows
from the bottom up), each with a TGA 2.0 footer. The frames are laid left
to right, then down, in the order they're shown.

### What isn't drawn

- When the TGA's descriptor declares an alpha bit (its low bits, 1), a
  pixel's top bit says it's drawn (`s-w-.ani`, `blkh.ani`).
- Without, black isn't drawn (the planets').
- `ns.ani` declares an alpha bit but sets it on no pixel; the game draws
  it as it does a file without, black not drawn (its screenshot matches
  pixel for pixel so).

## In Reluva

`readAnimation` (`src/engines/botf/formats/animation.cpp`) reads an
`.ani` into its frames, each an RGBA picture, transparent where nothing is
drawn. The galaxy screen draws them. The unit tests
(`botf.unit.archives`: "animations") cover the header's order, the frames'
places on the sheet and the three ways of transparency.

## Sources and checks

- **Where the facts come from:** the headers of all 65 `.ani` files of
  the English `stbof.res`, and their sheets decoded and compared with
  screenshots of the game, November 2026.
- **How it was checked:** drawn by Reluva, the frames of the planets of
  Sol, Babel and Manark, their stars and the neutron star land on the
  screenshots' pixels exactly, each at one of its frames.

## Open questions

- The word at 6: the frames' count in 61 files, 5 in four of the
  stellar objects' (`ns.ani` has 8, its count).
- How fast each kind turns is the screen's, not the file's: no field
  holds it ([systems/galaxy-screen.md](../systems/galaxy-screen.md#the-strip)).
