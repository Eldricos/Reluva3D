# SimCoaster: the park's fence (`hoardings.map`, `Hoardings.sam`)

> **Status:** partial · **Code:** `src/engines/tpw/park/land.cpp`, `src/engines/tpw/ui/park_screen.cpp` · **Checked on:** the US release's three worlds' files and the missions' · **Confidence:** confirmed unless marked

## Overview

Each world's folder (`Data/levels/science/`) has a `hoardings.map`, the
cells inside the park's fence as a new park has it, and three settings
files of fences drawn as corners: `Hoardings.sam`, `EndHoardings.sam`
and `cem_Hoardings.sam`. The missions give their own, `BoundaryClicks`,
for the fence once they're done ([land.md](../systems/land.md)). The
Intro World has the settings files but no map. The fence a new park has
is drawn by the world's own model; the missions' are put together from
the pieces in the world's `hoarding.wad`. Special rides have a hoarding
of their own, round them till they're activated.

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

### The world's own fence

The world's model (`terrain/base.MD2`) has meshes named `basic hoard01`
on (80 to 90 of them in each world), low walls 10 units high along the
cells' edges round the new park, in `jho_fnt1` and the world's own
textures.

### The pieces (`hoarding.wad`)

Each playable world's `hoarding.wad` (`levels/<world>/hoarding/`) has
nine models with their height maps and textures:

| Model | What it is |
|---|---|
| `ho_fos1` to `4` | A straight: a wall 20 units high along x 10, z 0 to 20, its face (`jho_low*`, `jho_top*`) on the +x side, its props (`jho_bac1`) on the −x side |
| `ho_fob1` to `4` | A corner cut across: from (10, 20) to (20, 10), its face away from (10, 10) |
| `ho_end1` | A wall from (10, 10) to (10, 20), propped at its end |

Each is two cells square (a cell is 10 units), its middle at (10, 10),
a point where four cells' corners meet *(inferred)*. So a straight
reaches a cell each way along the edge from its point, a corner piece
a cell along each edge, and an end one cell. The fences' corners all
lie at even cells from the first, two cells apart at least, so their
runs are whole straights between corners. The four looks are different
pictures on the same shape: Land of Invention's plants, gears and
posters.

### A special ride's hoarding

`Info.Hoarding` in a ride's settings is a grid of its shape's size
(`rides/vict_hs/vict_hs.sam`):

```text
F^^^^7
[....]
[....]
L_.._J
```

`F`, `7`, `L` and `J` are the corners, top left to bottom right, with
their two edges; `^` and `_` a cell's top or bottom edge; `[` and `]`
its left or right; `.` none: gaps where its entrance and exit are, and
the log mill's sides, where its track would come in *(inferred)*.

## In Reluva

`Park::loadLand` reads a world's `hoardings.map`, and `growLand` draws a
mission's fence (`src/engines/tpw/park/land.cpp`) and keeps its corners
as the park's fence (`Park::fence`); the settings files' fences aren't
read ([land.md](../systems/land.md)).

`Park::fencePieces` lays a fence's edges in pieces: a corner piece
where it turns, an end where it stops, straights every other point
between. A run of an odd number of edges, as hoardings can have, ends
with an end piece against the corner, where the game's own pieces
would overlap *(guessed)*. A straight's face looks into the park, and
a ride's hoarding's out from the ride *(guessed)*; a corner's and an
end's go as their turn has them. Each piece's look is picked by its
place *(guessed)*. Once a mission's fence stands, `ParkScreen` hides
the world's `basic hoard` meshes and draws the pieces in its place
*(inferred)*, and draws each special ride's hoarding till it's
activated. `reluva-tool tpw park-shot ... -mission 1` shows Land of
Invention's first new fence; `-mission 3`, the log mill's and the Meka
Bird's hoardings.

## Sources and checks

- **Where the facts come from:** the files and the missions' settings
  and comments ("Boundary set when mission is complete", "New hoardings
  are loaded").
- **How it was checked:** each world's `Hoardings.sam` drawn at every
  offset and in each orientation against its `hoardings.map`; Land of
    Invention's missions' fences drawn over its attribute map, along its
  outer walls; the pieces' meshes listed with `md2-check` and drawn with
  `md2-shot`; Land of Invention's fences and hoardings drawn in the park
  with `park-shot`, the pieces meeting end to end along the world's
  walls.

## Open questions

- Why `Hoardings.sam` and the map differ by a few cells; the map's
  diagonal edges aren't a fence of straight runs.
- What `EndHoardings.sam` and `cem_Hoardings.sam` are for.
- How the game lays the pieces: which look where, which way their faces
  look, and what it does with runs of an odd number of cells; whether
  the world's own fence goes when a mission's comes.
- What the pieces' height maps (`ho_fos1.hmp` and the rest) are for.
