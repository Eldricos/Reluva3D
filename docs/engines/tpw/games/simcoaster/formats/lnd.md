# SimCoaster: the park's ground (`.lnd`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/lnd.cpp` · **Checked on:** the US release's five `.lnd` files · **Confidence:** confirmed unless marked

## Overview

Each world's terrain is in `Data/levels/<world>/terrain/`:

- `base.MD2`, a model ([md2.md](md2.md)) of everything around the park:
  hills, the border wall, the entrance plaza, the road, the river's
  channel. It
  has a hole where the park's own ground goes, x from 0 to 960 and z
  from 0 to about 860, its edge at y = 0.
- `base.lnd`, that ground: masks over it, cell by cell, and the
  textures it is painted with. This page.
- `base.map`, an attribute map of the world's tiles
  ([attribute-map.md](attribute-map.md)).
- A `.tct` file naming the paths' and queues' textures by number
  (`Arabian.tct`: `PathTex`, `0 an_squ1.tga`...): not read yet.

There are five `.lnd` files: the four worlds' `base.lnd`, and in the
Polar Zone's `terrain/proc_tex/` a copy of its own beside the pictures
it seems to have been built from (below). The Land of Invention's and
the Intro World's grounds have the same cells, with textures and
settings of their own.

## Layout

All numbers little-endian. The file is pictures one after another, each
its width, height and bytes a pixel (4 bytes each), then its pixels row
by row.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 1 | 3: a version, perhaps *(guessed)* |
| 0x01 | 10 | Settings, not worked out (below) |
| 0x0B | | The cells: a picture 384 × 344, 4 bytes a pixel |

Then three times, for three sizes:

| Size | Field |
|---|---|
| 4 | 64, 32, then 16: the textures' border, two halves (below) |
| 1 | 0, 1, then 2 |
| | Six textures, 4 bytes a pixel: A (0 in all), R, G, B |
| | Seven masks, 1 byte a pixel |

Each size's pictures are half the last's, rounded down (192, 96, 48).
In all five files the cells and the 39 pictures run to the end exactly.

### The cells

A cell for every 2.5 × 2.5 units of the hole in `base.MD2`, 384 across x
and 344 along z, the first row at the far side, z ≈ 860 *(confirmed)*:
laid so, Land of Invention's holes fall on the prebuilt park's
([tpws.md](tpws.md#the-grid)) plaza outside the gates, every one of its
132 cells, and on its lake's tiles, and run down the middle of its
river's tiles, the channel's water in `base.MD2`; none on its bare
land. Turned the other way, 679 of its bare land's cells would be holes,
and 3 of the plaza's. A cell's four bytes:

- **Byte 0**: 255 where the park has ground, 0 where it has none and
  `base.MD2` shows through: the plaza and forecourt before the gates,
  the lake, and the river's water, its banks ground. In the Land of
  Invention's and the Intro World's, 215 cells are between; in the
  Polar Zone's, 43.
- **Bytes 1 to 3**: the red, green and blue of the Polar Zone's
  `proc_tex/control.fsh`, a 384 × 344 picture (correlations 0.87, 0.99
  and 0.92 against its pixels): how much of each of the ground's three
  layers covers the cell *(inferred)*, the green and blue laying the
  snow and ice over the Polar Zone's rock.

### The textures and masks

The Polar Zone's `proc_tex/` folder holds what look like their sources,
each 128 × 128, all of them recognisable in its `base.lnd`:

| In the file | Its source |
|---|---|
| Textures 0 to 5 | `base1.fsh`, `base2.fsh`, `mid1.fsh`, `mid2.fsh`, `top1.fsh`, `top2.fsh`: two for each of three layers |
| Mask 0 | `baseblend.tga` |
| Masks 1 and 2 | `basecover.tga`, and `midblend.TGA` (the same picture as `topblend.TGA`): which is which not settled, the two look alike |
| Masks 3 to 6 | `mid1cover.tga`, `mid2cover.tga`, `top1cover.tga`, `top2cover.tga` |

In the file each is resized and given a border for repeating: the
picture is a size from the folder's `settings.dat` plus the level's
first number, 64 at the first size (`base1`: 148 + 64 = 212), its edges
the far sides repeated. `settings.dat` holds 28 words; ten of them are
the `.lnd`'s settings bytes, in order (48 40, 40 90 68 40, 97 30 55 50),
the rest the sizes and others not worked out.

## Behaviour

How the game paints the ground from these isn't known. Reluva's guess,
which gives ground like the recordings': each layer's two textures
mixed by its blend mask; the middle layer laid over the base as much as
a cell's byte 2 says, the top over both as much as its byte 3. Byte 1
and the cover masks aren't used *(guessed)*.

## In Reluva

`Land` (`src/engines/tpw/formats/lnd.cpp`) reads the cells and the
three sizes' textures and masks. The viewer's Worlds page draws each
world's `base.MD2` with the ground laid flat at y = 0 in its hole,
painted by the guess above (`src/engines/tpw/ui/world_view.cpp`);
`reluva-tool tpw world-shot <game folder> <world folder> <out.png>`
draws the same to a PNG. `reluva-tool tpw terrain-check` reads every
`.lnd` and attribute map; the game-files test runs it
(`tpw.gamedata.terrain`).

## Sources and checks

- **Where the facts come from:** the US release's five `.lnd` files
  (November 2026): the pictures' sizes walked to each file's end; the
  cells drawn as pictures and laid over `base.MD2`'s hole; the Polar
  Zone's `proc_tex/` pictures set beside the file's, by eye, and its
  control picture against the cells by correlation.
- **How it was checked:** all five read with `reluva-tool tpw
  terrain-check`; the four worlds drawn with `world-shot`, the ground
  meeting the surroundings; Land of Invention's holes set against the
  prebuilt park's cells, and the fly-in's forecourt grey before the
  gates, as the recording's at 1:48. The unit tests (`tpw.unit.formats`) read a
  ground built in the test, and fail cut and disordered ones.

## Open questions

- How the game paints the ground: which bytes weigh which layer, and
  what the cover masks and byte 1 do.
- The settings bytes.
- Whether the ground can be raised and lowered, and where its heights
  would be kept.
- Where the game finds the Intro World's surroundings' textures: its
  `base.MD2` names the Land of Invention's (`ns_oh01.tga`...), which
  only that world's folders hold, so Reluva draws them grey.
