# SimCoaster: the park's fence (`hoardings.map`, `Hoardings.sam`)

> **Status:** partial · **Code:** `src/engines/tpw/park/land.cpp` · **Checked on:** the US release's three worlds' files and the missions' · **Confidence:** confirmed unless marked

## Overview

Each world's folder (`Data/levels/science/`) has a `hoardings.map`, the
cells inside the park's fence as a new park has it, and three settings
files of fences drawn as corners: `Hoardings.sam`, `EndHoardings.sam`
and `cem_Hoardings.sam`. The missions give their own, `BoundaryClicks`,
for the fence once they're done ([land.md](../systems/land.md)). The
Intro World has the settings files but no map.

## Layout

### `hoardings.map`

16,384 bytes, no header: a byte for each of the world's 128 × 128 cells,
laid out as the attribute map's tiles are
([attribute-map.md](attribute-map.md)): row x, column y. 0 is inside the
fence, 1 outside; no other values. Land of Invention has 1,259 cells
inside, Arabian Nights 2,323, Polar Zone 1,343. Inside are the park's
own land and the way in from the road, the entrance's path among them.

### The fences' corners

A fence in a settings file is a list of corners, `HoardingClicks[n].X`
and `.Y` (`BoundaryClicks` in a mission's), ended by one at (512, 512).
Each corner shares a coordinate with the one before: the fence runs
along the cells' edges, round the park, from (0, 0) to the last corner
before the end, the gap between them its entrance, eight cells wide
((−8, 0) to (0, 0) in every one).

The corners are counted in cells from a point by the entrance: x 0 is
four cells on from the entrance's second cell (Land of Invention's
(48, 17)), so the gap's middle is between the entrance's two cells, and y
0 is the cell past them. Drawn there, `Hoardings.sam`'s fence takes in
the same cells as the world's `hoardings.map` but for a few: 91, 92 and
83 percent of them in Land of Invention, Arabian Nights and Polar Zone.
The missions' fences, drawn so, run along the worlds' walls and the
terrain's edges in the worlds' map pictures *(inferred)*.

`EndHoardings.sam` and `cem_Hoardings.sam` are fences of other shapes;
what they're for isn't known.

## In Reluva

`Park::loadLand` reads a world's `hoardings.map`, and `growLand` draws a
mission's fence (`src/engines/tpw/park/land.cpp`); the settings files'
fences aren't read ([land.md](../systems/land.md)).

## Sources and checks

- **Where the facts come from:** the files and the missions' settings
  and comments ("Boundary set when mission is complete", "New hoardings
  are loaded").
- **How it was checked:** each world's `Hoardings.sam` drawn at every
  offset and in each orientation against its `hoardings.map`; Land of
  Invention's missions' fences drawn over its attribute map, along its
  outer walls.

## Open questions

- Why `Hoardings.sam` and the map differ by a few cells; the map's
  diagonal edges aren't a fence of straight runs.
- What `EndHoardings.sam` and `cem_Hoardings.sam` are for.
