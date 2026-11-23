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
- `.FPC`, the same frames seen from lower down, beside the sheets of
  those walking about (not the balloons', thoughts' or heads'): for the
  first-person view, where the camera walks among the visitors at eye
  level (from 22:30 in the recording), as `.TPC`'s are for the park's
  view from above *(inferred)*. The settings' `FP` fields for a
  visitor's umbrella (below) are the first-person view's, beside its
  others;
- `.ESP`, its animations: which frames of the sheet each plays.

The visitors: kids `SPR_KB`, `SPR_GK`, `SPR_BD` and `SPR_GB`; parents
`SPR_FA` and `SPR_MO`; grandparents `SPR_GR` and `SPR_GM`.

A `.TPC` frame is a `.FPC` one squashed: as wide, but shorter (the
father standing 54 pixels high against 63), its point below the feet
rather than at them, as a person is seen from above *(confirmed: every
pair of sheets alike)*.

### How large

A frame's pixel is about a tenth of the world's unit *(inferred)*:

- the recording's visitors stand about half a path's cell (10 units)
  high, 15 to 20 pixels against the cell's 32, at 11:40 and 20:00;
- the general settings (`levels/Standard.sam`) hold an umbrella over
  each kind of visitor (`PersonSpriteUmbrellaModifiers[n].Y`): 5.7
  units up over a child, 6.5 over the "tall woman", 5.5 and 5.6 over the
  "old biddy" and "old bloke", the comments say; their `.FPC` frames
  are 50 to 63 pixels high, 0.10 to 0.11 units each.

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
*(inferred)*. The others, four frames in five directions, by what they
show; what the game plays them for isn't known:

| Animation | What its frames show |
|---|---|
| 4 | Leaning, then bending forward |
| 6 | Bending right down to the ground |
| 12 | Arms flung out, the mouth open: cheering, or shouting |
| 15 | A hand to the mouth: eating or drinking |

The guards' have more: 4 holding up a card with a red cross, 5 a sign,
8 walking with a sack over the shoulder (eight frames), 7 and 9 lifting
and putting down the sack.

## In Reluva

`SpriteSheet` and `SpriteSet` (`src/engines/tpw/formats/sprites.cpp`)
read the sheets and the animations; `drawSpriteFrame` and `spritePose`
(`src/engines/tpw/ui/sprites.cpp`) choose a frame by the person's
heading against the camera's, the nearest of the eight, and draw it,
blended, scaled by its distance. The park's visitors are drawn so
([park.md](../systems/park.md#visitors)), from the `.TPC` sheets, a
look each of their generation's, 0.1 units a pixel. Their walk steps a
frame each unit walked, its eight frames over 8 units, about one and a
half of an adult's heights *(guessed)*.

`reluva-tool tpw sprite-check <game folder>` reads every sheet and
animation set, and checks each animation's frames are in its sheet;
the game-files test runs it (`tpw.gamedata.sprites`). The unit tests
(`tpw.unit.formats`) read a sheet and a set built in the test and
choose frames by heading.

## Sources and checks

- **Where the facts come from:** the US release's `esprites.wad`
  (November 2026): the frames by reading every sheet through to its
  end; the palette's order and the directions by drawing them; the
  general settings' umbrellas and their comments; the recording, its
  visitors measured against its paths.
- **How it was checked:** `sprite-check`: 71 sheets and 45 sets read,
  7,893 frames, none of the animations past its sheet's frames; the
  visitors drawn walking up the park's path, their backs to the camera.

## Open questions

- The frames' 128s; the `.ESP`'s flags.
- When the game plays the visitors' animations 4, 6, 12 and 15, and the
  staff's others.
- How fast the game steps them: the recording's visitors are too small,
  and its camera moves too often, to count their frames.
- Which umbrella setting is which visitor's: 10 of them, against 8
  visitors' sprites.
