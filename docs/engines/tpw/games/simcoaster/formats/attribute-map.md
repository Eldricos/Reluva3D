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
its first corner: the park's 96 × 86 tiles of 10 units
([lnd.md](lnd.md#the-cells)), across and down swapped *(inferred)*.
1 (929 tiles) traces the park's edge and its rivers; the others are
0x11 (242), 0x10 (175), 0x94 (124), 8 (10), 0x90 (8) and 0x95 (3). The
Land of Invention's and the Intro World's add 3 (285) and 0x91 (18);
the Polar Zone's has the most 1s (1,990), along its wider river. What
the bits mean is not worked out.

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
- Which way the map lies over the park.
