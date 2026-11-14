# id Tech 3: Raven's compiled maps (`maps/*.bsp`, RBSP)

> **Status:** decoded · **Code:** `src/engines/idtech3/formats/bsp.cpp` (with the IBSP reader), `render/world_renderer.cpp`, `render/light_grid.cpp` · **Checked on:** all 41 Jedi Outcast 1.02a maps (`reluva-tool idtech3 bsp-check`, November 2026) · **Confidence:** layout confirmed; behaviour published unless marked

## Overview

Raven's map format, Jedi Outcast's: Quake III's ([bsp.md](bsp.md)) with
**light styles**. A surface can be lit by up to four styles, each with
its own lightmap (or vertex colours), and each style's brightness can be
changed while the level runs (flickering, switched lights). Jedi
Outcast's maps are in its `base/assets0.pk3` (26 single player's, 15
multiplayer's). Jedi Academy and Soldier of Fortune II are said to use
the same format, unchecked.

## Layout

What isn't said here is as [bsp.md](bsp.md) has it.

### Header

`"RBSP"`, version 1, then **18** lumps (offset and size each): Quake
III's 17 in the same order, and an 18th, the light grid's index.

### Lumps that differ

| # | Lump | Record size | Fields |
|---|---|---|---|
| 9 | brush sides | 12 | i32 plane, i32 shader, i32 the drawn surface the side made (-1 if none) |
| 10 | vertices | 80 | f32[3] position, f32[2] texture coordinates, f32[2] lightmap coordinates for each of 4 styles, f32[3] normal, u8[4] colour for each of 4 styles |
| 12 | fogs | 72 | as Quake III's, but a brush of -1 is the whole level's fog |
| 13 | surfaces | 148 | below |
| 15 | light grid | 30 | the grid's distinct points; below |
| 17 | light grid index | 2 | u16: for each grid point in order, which of lump 15's points it is |

A style number is a byte: 0 is the level's steady light, 255 means the
slot is unused, others are styles the game sets (Jedi Outcast's maps use
1, 2, 6, 9 and 11, and 32 to 38).

#### Surfaces

| Offset | Size | Field |
|---|---|---|
| 0 | 4 × 7 | shader, fog, type, first vertex, vertex count, first index, index count (as Quake III's) |
| 28 | 4 | u8 style of each of the 4 lightmaps (255: none) |
| 32 | 4 | u8 style of each of the 4 vertex colours (255: none) |
| 36 | 16 | i32 lightmap of each style (-1: none) |
| 52 | 16 | i32 x of each style's place in its lightmap |
| 68 | 16 | i32 y of each style's place in its lightmap |
| 84 | 8 | i32 width, height of the place (the same for every style) |
| 92 | 48 | lightmap origin and vectors, as Quake III's |
| 140 | 8 | patch width, height |

Each style's lightmap coordinates fall inside its place: all 12,796 of
`artus_mine`'s multi-style vertices do, and no two surfaces of a map share
a place.

### The light grid

The grid is laid out as Quake III's (cells of the worldspawn's
`gridsize`, 64 64 128 by default, over the world's bounds), but each cell
is a u16 in lump 17 naming one of lump 15's points, so cells that are the
same are stored once (kejim_post: 496,800 cells, 32,993 points). A point:

| Offset | Size | Field |
|---|---|---|
| 0 | 12 | u8[3] ambient colour for each of 4 styles |
| 12 | 12 | u8[3] directed colour for each of 4 styles |
| 24 | 4 | u8 style of each (255: none) |
| 28 | 2 | u8 longitude, latitude of the direction, as Quake III's |

## Behaviour

- **Light styles add up.** A surface's light is the sum of its styles'
  lightmaps (or vertex colours, or grid colours), each scaled by its
  style's current colour, taken in order up to the first unused style
  (255; the original stops at 254 too, which no Jedi Outcast map uses). There are 64 styles; until the game's code sets them,
  every one is full white (the renderer's start-up).
- **Each extra lightmap is a pass of its own**: the shader's lightmap
  stage copied right after it, with that style's lightmap and colour,
  added (`GL_ONE GL_ONE`) over what's drawn.
- **A grid point with no first style is inside a wall** and is skipped
  when models are lit, as Quake III's skips a black one; a point's styles
  are added up to the first unused one. Jedi Outcast's maps have one such
  point each (41 in all, with no other style); 4,688 points with a style
  have black ambient light and, unlike Quake III's, are used.
- **The level's fog** (a fog whose brush is -1, named again by the
  worldspawn's `fog` key) has no surface: everything is in it, fogged by
  distance from the eye. `yavin_canyon` and `yavin_swamp` have one.

## In Reluva

`parseBsp` reads both formats into the same `Bsp`: Quake III's maps get
one style (0) on every surface, vertex and grid point, and their grid an
index of one point each. The light styles aren't animated yet: they wait
for the game's code (Jedi Outcast's plan, phase 6), so for now the
renderer adds each surface's styles into its first style's lightmap and
vertex colours as the level loads, all at full; the light grid's points
are added up the same way. That draws as the original's passes do where
the lightmap's stage is the shader's first, as it is in all but 7 of the
250 shaders on multi-style surfaces (`textures/cairn/basewall`,
`textures/imp_mine/grating` and others put a texture first): on those the
extra styles' light is multiplied by the texture, where the original adds
it on top. `reluva-viewer`'s World and Maps pages show the maps; `reluva-tool idtech3 bsp-check` reads them all.

## Sources and checks

- **Where the facts come from:** the record sizes, field order and the
  index lump were read from Jedi Outcast's 41 maps (every lump a whole
  number of records, brush sides' counts adding up, the index's points
  in range, its length the grid's cell count); the GPL Jedi Outcast
  source (`qcommon/qfiles.h`, `renderer/tr_bsp.cpp`, `tr_light.cpp`) as a
  guide to names and to what the renderer does with them; the grid's
  sampling as its `R_SetupEntityLightingGrid` (`tr_light.cpp`) reads it,
  the extra passes as its `FinishShader` (`tr_shader.cpp`) makes them,
  the styles' start as `R_Init` (`tr_init.cpp`) sets them.
- **How it was checked:** `bsp-check` reads every map to the end and
  checks every index; a unit test (`idtech3.unit.formats`) reads a
  made-up RBSP map with two styles, a level fog and a shared grid. The
  maps drawn in `reluva-viewer` look lit as expected; not yet compared
  with the original running.
