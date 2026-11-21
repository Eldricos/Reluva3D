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
  every one is full white (the renderer's start-up). The client game sets
  all 64 each frame (`R_SetLightStyle`, its colour's four bytes in a word,
  red lowest), from the level's style strings: 1.02a's flicker, pulse and
  dim the classic styles 1 to 12, and set 13 from the level.
- **Style 0's lightmap isn't coloured**: the first lightmap pass is drawn
  white whatever style 0's colour; each extra style's pass takes its
  colour. Vertex colours and the grid take every style's colour, style 0's
  too: a vertex's light is each style's colour times its light, added up
  (and divided by 256, which the renderer does), a grid point's each
  style's light times its colour / 255.
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
  distance from the eye. `yavin_canyon` and `yavin_swamp` have one. With
  it, each frame starts cleared to its colour (in bytes), so where
  nothing is drawn (both levels have no sky) the fog's colour shows, not
  black; the last such fog is the one (jk2sp.exe 0x45d09e, 0x45f160;
  jk2mp.exe 0x46e399) *(confirmed)*. Single player's light amplification
  goggles clear to their own fog's colour instead (0x45d076), which
  Reluva doesn't have yet.
- **Models in fog** (jk2sp.exe 0x4749a0 for MD3 models, 0x467bc0 for
  Ghoul2's, 0x472990 for sprites): a sphere about the model (an MD3's,
  its frame's centre moved by the entity, not turned, and its radius; a
  Ghoul2 entity's, its origin and radius) picks the fog. The fogs are
  tried in order: one whose box holds the whole sphere is taken; one
  whose box holds a corner of the sphere's box is taken if it's the fog
  the eye is in or has the same red and green (0x4731e0), else kept if
  it's the first such; when none is taken, the one kept (or none). So with
  the eye out of the water, an object half in it gets the whole level's
  fog, which comes after the water in the list. A fog's box is its brush's first six sides
  (0x45f1b5); the whole level's is ±65536, so it takes whatever no
  volume does. The eye's fog (0x473230) is the first whose box holds the
  eye, if the level's contents there are fog *(confirmed)*.

## In Reluva

`parseBsp` reads both formats into the same `Bsp`: Quake III's maps get
one style (0) on every surface, vertex and grid point, and their grid an
index of one point each.

The renderer draws each surface's extra styles as the original does: the
lightmap stage again for each, added, coloured by the style
(`ShaderCache::draw`'s style passes). Each extra style slot (second,
third, fourth) has a lightmap of its own per lightmap, holding the
style's light at the surface's first style's place, so the surface's one
set of lightmap coordinates finds it; surfaces are batched by their
styles. When the game's code changes a style, only the passes' colours
change, and the vertices of surfaces lit by styles other than 0 alone are
lit again, each style's light times its colour over 256
(`WorldRenderer::setLightStyles`); the grid scales each point's styles as
it's sampled. In multiplayer only `ffa_ns_streets` has such surfaces
(45, style 6, a flicker); single player's levels many more.

One difference remains: vertices lit by style 0 alone aren't lit again
if a game changes style 0 (none of Jedi Outcast's does).

**Brightness.** Jedi Outcast's engine never brightens the level's light
as it loads it, unlike Quake III's and Elite Force's: its shift is
1 - `r_overBrightBits` when overbright is on (0x470230, 0x470b90 for the
grid), and overbright is only ever 0 or 1, forced to 0 in a window or
without hardware gamma (0x47ccd0). In fullscreen the screen's gamma ramp
doubles everything instead, and the 2D pictures are drawn at half to make
up for it. So in a window the level is as stored, half Reluva's usual
brightness, as the retail game's screenshots show it (November 2026);
Reluva doubles the level's light (lightmaps, vertex colours and the light
grid) only in fullscreen with `r_overBrightBits` on
(`GameInfo::overbrightFullscreenOnly`). Retail screenshots never show the
ramp's doubling, as it happens after the frame is read; Reluva's show the
screen as it looks.

`reluva-viewer`'s World and Maps pages show the maps (all styles full);
`reluva-tool idtech3 bsp-check` reads them all.

## Sources and checks

- **Where the facts come from:** the record sizes, field order and the
  index lump were read from Jedi Outcast's 41 maps (every lump a whole
  number of records, brush sides' counts adding up, the index's points
  in range, its length the grid's cell count); the GPL Jedi Outcast
  source (`qcommon/qfiles.h`, `renderer/tr_bsp.cpp`, `tr_light.cpp`) as a
  guide to names and to what the renderer does with them; the grid's
  sampling as its `R_SetupEntityLightingGrid` (`tr_light.cpp`) reads it,
  the extra passes as its `FinishShader` (`tr_shader.cpp`) makes them and
  the extra passes' colours as its stages' colour generators do (`tr_shade.cpp`), a vertex's
  styles as `tr_surface.cpp` adds them up, the styles' start and setting
  as `R_Init` and `RE_SetLightStyle` (`tr_init.cpp`) do; what 1.02a's
  client game sets, from it running in Reluva.
- **How it was checked:** `bsp-check` reads every map to the end and
  checks every index; a unit test (`idtech3.unit.formats`) reads a
  made-up RBSP map with two styles, a level fog and a shared grid; and
  ("light styles") draws a two-style surface, its lightmap and vertices
  made again as its style goes dark, half and back, the grid's points
  scaled. `game-run -cgame` reports the styles 1.02a's client game sets
  (recorded by the game-files step `jk2-game`). The maps drawn in
  `reluva-viewer` look lit as expected; the styles changing haven't been
  seen on screen yet, nor compared with the original running.

### Open questions

- The GPL renderer gives a surface with a lightmap white vertex colours
  (`ComputeFinalVertexColor`), so a stage of its shader that uses vertex
  colour sees white; Reluva gives such stages the surface's vertex light.
  Not found in jk2mp.exe yet, so not followed.
