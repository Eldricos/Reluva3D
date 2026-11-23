# SimCoaster: height maps (`.hmp`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/hmp.cpp` · **Checked on:** the US release's 411 `.hmp` files · **Confidence:** confirmed unless marked

## Overview

Beside most rides', shops', features' and queues' models
([md2.md](md2.md)) is a height map of the same name
(`levels/arabian/rides/wateride/wateride.hmp` for `wateride.MD2`): how
high the model stands over each part of the park's ground it covers, on
a grid of tiles 10 units square, 5 samples to a tile's side. The game
has 411: 129 a single tile, 112 two by two, the largest the Land of
Invention's lake (`levels/science/obstacles/lake/lake.hmp`), 36 × 37.
What the game uses them for isn't known yet: where guests and the
camera can go, or what the model hides, perhaps *(guessed)*. No description of the format had been published; all that
follows was worked out from the files.

## Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | 3: a version, perhaps *(guessed)* |
| 0x02 | 2 | 0xAB1E |
| 0x04 | 2 | Samples along a tile's side: 5 |
| 0x06 | 2 | 100: not worked out |
| 0x08 | 2 | The tiles across (along x) |
| 0x0A | 2 | The tiles deep (along z) |
| 0x0C | 4 | Where the samples start: 0x30 |
| 0x10 | 4 | Where the tiles' heights start |
| 0x14 | 4 | Where the tiles' flags start |
| 0x18 | 24 | The model's box as its header gives it ([md2.md](md2.md#the-header)): least x, y, z, then most |

Then, in all 411:

- **The samples**: a byte each, row by row over the whole map, (tiles
  across × 5) to a row and (tiles deep × 5) rows: not tile by tile. Each
  is a height in steps of 0.4 units *(inferred)*.
- **The tiles' heights**: a byte each, row by row: the highest of the
  tile's 25 samples for 2,965 of the 4,865 tiles, and more than that
  for 1,758: `Gates.hmp`'s samples are all 0, its 24 tiles' heights all
  98. Below it for 142.
- **The tiles' flags**: a byte each, row by row: 1 (3,988 tiles) or 0
  (877). Not worked out: `wateride.hmp`'s 12 tiles are all 1, the ATM
  desk's and the gates' first tile alone.

The three follow one another to the end of the file, with nothing
between them.

### Placing it on its model

The model stands with y up and its ground at y = 0. For some models the
map's sample (x, z) is the square of the ground from (2x, 2z) to
(2x + 2, 2z + 2) from the model's origin, and its height, in steps of
0.4, the model's highest point over that square *(inferred)*: not for
all of them (below).

`wateride.hmp` (4 × 3 tiles; its model 40 × 30 units from its origin,
43.5 high) shows it: its 20 × 15 samples, times 0.4, match the model's
top surface sampled at each square's middle where the model is flat
(the queue's floor at 29 steps, 11.6 units; the walls at 68; the edges
at 8 and 13), and its highest, 110 steps, 44 units, is the model's
height. Over the statue's head the map is up to 20 steps higher than
the model at the squares' middles, as a square's highest point would
be.

Over all the maps with a model of their name (382 of them; the three
largest left out), sampling each model's top surface at nine points in
each square and keeping the highest: 113 maps match their model within
6 steps (2.4 units), on average over their samples. Of those, 87 match
best from the model's origin and 26 from its box's least corner; 64 of
the 87 with the heights from y = 0, 23 from the box's bottom. The other
269 match neither way: the coasters' pylons (`looppylon.hmp`,
`StdPylon.hmp`) are off by 185 to 229 steps, their maps much lower than
the pylons. What those maps measure, and where the game places a map
on its model, are not worked out.

## In Reluva

`HeightMap` (`src/engines/tpw/formats/hmp.cpp`) reads a map's samples,
its tiles' heights and flags. `reluva-tool tpw hmp-check <game folder>`
reads every map and counts the tiles whose heights are their highest
sample's; the game-files test runs it (`tpw.gamedata.heights`). The
viewer's Models page shows a model's height map over it with H, a dot
for each sample at its square's middle, red where it is its tile's
height; `reluva-tool tpw md2-shot ... -heights` draws the same.

## Sources and checks

- **Where the facts come from:** the US release's height maps (November
  2026): the header's offsets and counts across all 411; what the
  samples measure by setting them beside their models' surfaces, by eye
  (`wateride.hmp`) and with a fitting script over all of them.
- **How it was checked:** all 411 read with `reluva-tool tpw
  hmp-check`; drawn over their models in the viewer. The unit tests
  (`tpw.unit.formats`) read a map built in the test, and fail cut ones
  and ones of another kind.

## Open questions

- What the game uses the maps for, and where it places each on its
  model: the rule above fits 113 of the 382.
- The tiles' heights that aren't their highest sample's, and the flags.
- The header's 100 at 0x06.
- Whether a height step is 0.4 units exactly.
