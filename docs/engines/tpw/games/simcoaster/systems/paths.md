# SimCoaster: paths

> **Status:** partial · **Code:** `src/engines/tpw/park/path_tiles.cpp` · **Checked against:** the worlds' path textures, and a recording · **Confidence:** inferred unless marked

## Overview

Visitors walk the park's paths, a park cell of path at a time
([park.md](park.md#the-parks-cells)). The player lays them with the
path tool, the hand on the left of the screen ("Path mode", `UIHELPTEXT`
117): "Build path. Click to build a length of path. Click onto existing
path to finish" (147, 148). Each cell costs the general settings'
`Costs.PathCell`, 10 (`levels/Standard.sam`). Each world draws them
with its own textures, a cell each, picked by which of the cell's
neighbours are path.

## How it works

### Where path can go

The attribute map marks the entrance's path, tiles 8
([attribute-map.md](../formats/attribute-map.md#the-tiles)): its two
cells and the four rows in from them. The recording's new park has much
more as its briefing closes at 2:34, before the player has spent
anything: a long path from the entrance, two cells wide, turning, with
branches. It's the game's prebuilt park's, `Data/PreBuilt/prebuilt.TPWS`
([tpws.md](../formats/tpws.md)), a saved park of Land of Invention: 76
cells of path, the same shape, and 5 of a queue to a ride beside it
*(inferred)*. Reluva lays its path and queue in a new park of the world
it's of, its cells' tiles being that world's attribute map's; the other
worlds start with their maps' path *(guessed)*.

Reluva lays path on the attribute map's open tiles (0), the park's own
bare land, and nowhere else *(inferred: the edges, river and road are
other tiles)*.

### The textures

Each world's `terrain.wad` has 19 in `pathtex/` (and the smaller
`spathtex/`), 128 × 128, each a whole cell: stone slabs, with grass
where the path stops. Read for where their grass is, they make a set
that covers every arrangement of neighbours, turned a quarter at a time
*(inferred: the set is complete, and the drawn paths join without
seams)*. A notch is a corner of grass in a cell whose two sides there are
path but whose diagonal neighbour isn't.

| Path on the sides | Texture (as drawn unturned: north up) |
|---|---|
| None | `ns_squ1`: a square of slabs in grass |
| North | `ns_end1` |
| North and south | `ns_str1`, `ns_str2`, `ns_pathcnr`: the same shape, different slabs |
| North and east | `ns_cnr1`; with a notch north-east, `ns_cnr2` |
| North, east and south | `ns_edg1`, `ns_edg2`; notched south-east `ns_tju3`, south-west with the path west `ns_tju4` (its mirror); notched both, `ns_tju1` |
| All four | `ns_ctr1`; one notch `ns_icn1`, two side by side `ns_tju2`, two opposite `ns_icn2`, three `ns_xrd2`, four `ns_xrd1` |

`dark.fsh` is grass, darker. Where several textures fit, which the game
picks isn't known; Reluva picks by the cell's place, so it holds still.

### Laying it

The path tool lays a length at a time: a click starts it, the next lays
path from there to the cell clicked and goes on from its end, until a
click onto path already laid finishes it, as the help says. Reluva lays
each length straight, along whichever way it's the longer
*(guessed)*; a right click or Esc drops the length being laid. A cell
already path costs nothing; the length stops being laid when the money
runs out.

### Queues

A queue is drawn by models, a cell each, of the world's `queue.wad`
(`levels/science/queue/`): 10 units square, laid from a corner, planks
with rails: `questra` a straight (along z unturned), `quebnd1` and
`quebnd2` bends (open to +x and +z), `queend` a straight with white
posts at one end, `quebin1` and `quebin2` bends with bins, `quedead`
closed but at one end. The saved park says each cell's piece and angle
([tpws.md](../formats/tpws.md#the-grid)): 2 a straight, 4 a bend, 5
where the queue meets the path. Reluva draws 2 as `questra`, 4 as
`quebnd1`, 5 as `queend`, its posts by the path: the shapes fit, the
bend's open sides and the posts' end with the angles turned as the
path's corners *(inferred)*.

## In Reluva

`Park` (`src/engines/tpw/park/park.cpp`) reads each cell's ground from
the attribute map (bare, path, or not the park's), lays lengths
(`pathRun`, `buildPath`) and charges for them. `pathTileFor`
(`src/engines/tpw/park/path_tiles.cpp`) picks each cell's texture and
turn; `ParkScreen` draws the path a twentieth of a unit over the ground,
and, while the path tool is chosen, the length that would be laid, the
cells it can't cross marked red, and finds the cell under the mouse.
`playPark` takes the tool's clicks. The prebuilt park's queue is drawn by
its pieces, and its cells and its objects' can't be laid over.

`reluva-tool tpw park-run` lays two lengths on the open land beside the
prebuilt path, four cells and three; the unit tests (`tpw.unit.park`) lay path on a map
built in the test, and check each kind of tile's texture and turn.

## Sources and checks

- **Where the facts come from:** the worlds' path textures, read for
  where their grass is; the help strings; the general settings' costs;
  the attribute map; the recording (the paths as its briefing closes).
- **How it was checked:** the starting path drawn as a seamless block
  of two by five; lengths laid in `park-run`, § 70 for seven cells.

## Open questions

- What the other worlds' new parks start with, and the prebuilt park's
  ride and the rest of its state.
- Whether a length can bend, and what the game does with a length over
  land it can't cross.
- Which of the textures that fit alike the game picks; what `dark` and
  `spathtex/` are for (a lower detail's, it seems, as `stexture/`).
- Laying queues (`Costs.QueueCell` 25), and the queue's other models:
  which save numbers `quebnd2`, `quebin1`, `quebin2`, `quedead` have.
