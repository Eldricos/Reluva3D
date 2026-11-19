# Midtown Madness 2: surfaces and collisions

> **Status:** partial · **Code:** `src/engines/age/game/collision.cpp`, `city_collision.cpp`, `src/engines/age/formats/materials.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

What the city's surfaces are made of, and how cars collide with the city, its bounds and its placed models. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## What the city is made of

`city/materials.mtl` is blocks of `mtl <name> { ... }`: **elasticity**,
**friction**, **effect**, **sound**, **drag**, **width**, **height**,
**depth**, **ptxindex** and **ptxthreshold** (particles). There are
eight: `deepwater` (depth 100), `_default` (friction 0.9), `grass`
(sound 2), `water` (sound 1, drag 0.119), `dirt`, `sand`, `cobblestone`
and `wood`. `city/materials.csv` lists textures and their material
("none": `_default`). Reluva gives the city's triangles their texture's
material: friction relative to `_default`'s (so roads keep their grip),
its sound (which surface of the tyres' tables), its drag and depth.
Deep water (depth 10 or more: the bay, the Thames) isn't collided with,
so a car sinks and is put back. A model's bound polygons keep their own
friction and elasticity and take the sound, drag and depth of the city
material of their material's name. **effect** is `none` for every
material, so it isn't used.

**ptxindex** and **ptxthreshold** are two pairs: what a wheel on the
material throws up, and from how much slip. The thresholds are 0.25 and
0.5 for most, 0.25 and 0.3 for cobblestone, 0 and 0 for water. Reluva
reads them against the wheel's slip (0 gripping, 1 sliding at 10 m/s or
spinning): the first effect once the wheel slips past the first
threshold, the second past the second, from none at the threshold to 8
particles a second slipping fully. A threshold of 0 (water) means
whenever the wheel rolls (over 1.5 m/s), more the faster up to 10 m/s.
The indices aren't named anywhere: they aren't the `tune/effects` rules
in alphabetical or archive order. Reluva pairs them by what the
materials are, and draws each with the particle texture that looks like
it:

| ptxindex | used by | rule (`tune/effects`) | texture |
|---|---|---|---|
| 1 | grass, sand (first) | `dust` | `fxpt8` |
| 2 | grass (second) | `grass` | `fxpt4` |
| 4 | `_default`, cobblestone (first): every road | `smoke` | `fxpt8` |
| 5 | sand (second) | `dirt` | `fxpt11` |
| 6 | water (second) | `splash` | `fxpt9` |
| 7 | cobblestone (second) | `rock` | `fxpt14` |

The rules' frame numbers (grass 6-9, splash 16-21) are of a sheet
bigger than any of the textures, so a particle takes any frame of its
texture; their radii (1-2 m, for clouds) are halved. **width** (water
2.42, grass and sand 0.45) spreads where the particles start across the
tyre's foot, and **height** (grass 0.04) lifts it: Reluva's reading of
them.

## The city's collisions

`buildCityCollision`:

- every triangle generated from the PSDL (roads, ground, walls, roofs),
  with its material (above);
- the facade bounds (PSDL attribute 7) as walls from their vertices'
  height down a metre to their height: they stand where placed building
  fronts are (`sw_`, `nw_`, `gw_`... models have no bounds of their own);
- the bounds of the placed models that have one (`bound/<model>_BOUND.bnd`
  or `.bbnd`: 207 of San Francisco's 3,763, 1,263 of London's 1,997).

In a grid of 8 m cells. The tunnels' walls and ceilings and the
bridges' railings are among the city's surfaces ([psdl.md](../formats/psdl.md)),
so they're collided with; the water under the bridges and the roads'
far-off low-detail copies aren't.

## Sources and checks

- **Where the facts come from:** `city/materials.mtl` and `materials.csv`, the bounds ([bnd.md](../formats/bnd.md)), the PSDL ([psdl.md](../formats/psdl.md)).
- **How it was checked:** `age.unit.driving` and `age.unit.city`; driving into walls, kerbs and the water by hand.
