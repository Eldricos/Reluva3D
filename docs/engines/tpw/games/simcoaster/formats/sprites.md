# SimCoaster: sprites (`.TPC`, `.FPC`, `.ESP`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/sprites.cpp` · **Checked on:** the US release's 71 sheets and 45 animation sets · **Confidence:** inferred unless marked

## Overview

The park's people aren't models but sprites: flat pictures, drawn
facing the camera. They are in `Data/esprites.wad` ([wad.md](wad.md)),
a folder a world (`Arabian`, `Science`, `Water`, `wIntro`: the
costumed entertainers and their heads) and `Generic` for the rest:
visitors (`Kids`, `Parents`, `Grandparents`, and their heads apart),
staff (`Guards`, `Handymen`, `Mechanics`, `Gardeners`, `Researchers`),
`Balloons`, `Umbrellas`, `Litter`, `Thoughts` and some effects.

Each sprite is two or three files of one name:

- `.TPC`, a sheet: a palette and frames, each a picture of run-length
  coded rows;
- `.FPC`, the same frames a little larger, beside some sheets: another
  detail setting's, it seems *(guessed)*;
- `.ESP`, its animations: which frames of the sheet each plays.

The visitors: kids `SPR_KB`, `SPR_GK`, `SPR_BD` and `SPR_GB`; parents
`SPR_FA` and `SPR_MO`; grandparents `SPR_GR` and `SPR_GM`.

## Layout

All numbers little-endian.

### A sheet (`.TPC`, `.FPC`)

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | 3 |
| 0x02 | 2 | 3 |
| 0x04 | 4 | How many frames |
| 0x08 | 1,024 | The palette: 256 colours, each blue, green, red, alpha: the visitors' skin and clothes come out right only so *(confirmed)* |
| 0x408 | | The frames, one after another |

A frame:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | How many bytes its rows take |
| 0x04 | 2 | Width |
| 0x06 | 2 | Height |
| 0x08 | 2 | 128 in every frame: its scale, perhaps *(guessed)* |
| 0x0A | 2 | 128, likewise |
| 0x0C | 4 | Its left edge from the sprite's point, signed: about minus half its width |
| 0x10 | 4 | Its top from the point: about minus its height, so the point is at its feet |
| 0x14 | | Its rows, one a line |

A row is a byte, how many bytes follow for it, then runs: a byte below
0x80 is followed by that many colours; a byte from 0x80 by one colour,
repeated 256 less the byte times. A row's runs make up its width or
less, the rest left empty. Colour 0 is empty; the palette's alpha
blends the rest, the shadows' colours having alphas between
*(inferred)*.
Every sheet reads to its last byte, each frame's rows to its size
*(confirmed)*.

### Animations (`.ESP`)

350 bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 12 | `ESP_FILE2.00` |
| 0x0C | | The sheet's name, ended by a 0: `SPR_GM.TPS` (its `.TPC` and `.FPC`) |
| 0x10C | 2 | Flags, not worked out |
| 0x10E | 4 each | 20 animations |

An animation:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | Its first frame in the sheet |
| 0x02 | 1 | How many frames it has a direction |
| 0x03 | 1 | How many directions: 0 for none, 1, or 5 |

Frame *i* of direction *d* is the first plus *d* times the frames, plus
*i*. Five directions are the person seen walking away, away and to the
right, to the right, towards and to the right, and towards; the other
three are those turned over, left for right *(inferred: the frames show
so)*. No animation's frames run past its sheet's *(confirmed)*.

For the visitors, animation 0 is one frame in five directions, standing;
1 is eight in five, walking; the kids' 2 eight in five, running, by their frames
*(inferred)*. What the rest are isn't worked out.

## In Reluva

`SpriteSheet` and `SpriteSet` (`src/engines/tpw/formats/sprites.cpp`)
read the sheets and the animations; `drawSpriteFrame` and `spritePose`
(`src/engines/tpw/ui/sprites.cpp`) choose a frame by the person's
heading against the camera's, the nearest of the eight, and draw it,
blended, scaled by its distance. The park's visitors are drawn so
([park.md](../systems/park.md#visitors)), from the `.TPC` sheets, a
look each, walking at 12 frames a second *(guessed)*.

`reluva-tool tpw sprite-check <game folder>` reads every sheet and
animation set, and checks each animation's frames are in its sheet;
the game-files test runs it (`tpw.gamedata.sprites`). The unit tests
(`tpw.unit.formats`) read a sheet and a set built in the test and
choose frames by heading.

## Sources and checks

- **Where the facts come from:** the US release's `esprites.wad`
  (November 2026): the frames by reading every sheet through to its
  end; the palette's order and the directions by drawing them.
- **How it was checked:** `sprite-check`: 71 sheets and 45 sets read,
  7,893 frames, none of the animations past its sheet's frames; the
  visitors drawn walking up the park's path, their backs to the camera.

## Open questions

- Which detail setting picks `.FPC` over `.TPC`; the frames' 128s.
- The animations past the third: what each plays; the `.ESP`'s flags.
- How fast the game plays them, and how large it draws a sprite against
  the world: Reluva's 0.05 units a pixel is matched by eye to a
  recording.
