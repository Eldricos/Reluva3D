# SimCoaster: attribute maps (`base.map`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/attribute_map.cpp` · **Checked on:** the US release's four `base.map` files · **Confidence:** confirmed unless marked

## Overview

Each world's `terrain/` folder has a `base.map` beside its ground
([lnd.md](lnd.md)): a byte for each of the world's 128 × 128 tiles,
which says something about each, not yet worked out. The game's other
`.map` files, beside the sound banks, are another format
([sdt.md](sdt.md)).

## Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `TP2M` |
| 0x04 | 32 | "Theme Park 2 Attribute Map File", ending in a zero byte |
| 0x24 | | Chunks, to an `END ` chunk |

A chunk is its 4-letter name, its size (4 bytes, after these 8), then
that many bytes. All four maps have a `MAP ` chunk, then `END ` (of
size 0):

| Offset in the chunk | Size | Field |
|---|---|---|
| 0 | 4 | The tiles across: 128 |
| 4 | 4 | The tiles down: 128 |
| 8 | 20 | Five words, 8 in all four maps: not worked out |
| 28 | | A byte for each tile, row by row |

### The tiles

The Arabian Nights' map has 1,491 tiles not 0, all within 85 × 96 of
its first corner: the park's cells of 10 units
([park.md](../systems/park.md#the-parks-cells)), a tile for each. Row x,
column y is the settings' cell (x, y), and the world's square at x 10x,
z 10y *(confirmed: the fixed items' cells fall on their tiles, and the
river's tiles on the world's river emitters)*. What each tile holds,
from Land of Invention's set beside its settings and the world's model:

| Tile | Tiles | Where |
|---|---|---|
| 0 | | The park's own land, open |
| 1 | 929 | The park's edges and walls, and the rivers' banks *(inferred)* |
| 3 | 285 | Its river (Land of Invention's and the Intro World's) |
| 8 | 10 | The entrance's path: the cells (47, 17) and (48, 17) and the four rows in from them |
| 0x10, 0x11 | 175, 242 | Outside the park, about the road |
| 0x90 | 8 | The ticket booths' cells, (45, 8) and (45, 11), and the road's edge |
| 0x91, 0x94, 0x95 | 18, 124, 3 | The road the bus comes along, its bus stops' cells among them |

(The counts are the Arabian Nights' map's; the Polar Zone's has the
most 1s, 1,990, along its wider river.) What each bit means is not
worked out.

## In Reluva

`AttributeMap` (`src/engines/tpw/formats/attribute_map.cpp`) reads the
`MAP ` chunk's tiles. `reluva-tool tpw terrain-check` reads every map;
the game-files test runs it (`tpw.gamedata.terrain`).

## Sources and checks

- **Where the facts come from:** the US release's four maps (November
  2026): the header's text, the chunks' sizes walked to the end, the
  tiles drawn as a picture beside the ground's cells.
- **How it was checked:** all four read with `reluva-tool tpw
  terrain-check`. The unit tests (`tpw.unit.formats`) read a map built
  in the test, and fail a cut one.

## Open questions

- What the tiles' bits mean, and the five words.
