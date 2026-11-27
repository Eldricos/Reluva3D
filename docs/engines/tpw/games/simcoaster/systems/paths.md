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

A queue ("line" in the US game's strings) leads from an attraction's
entrance to the path: its cells from the one by the entrance out, the
last beside a cell of path. It's drawn by models, a cell each, of the
world's `queue.wad` (`levels/science/queue/`): 10 units square, laid
from a corner, planks between rails 3 units high, and white posts 4.9
high where visitors join. Unturned, with north −z and east +x, as their
bounds and their groups' bounds show them *(confirmed)*:

| Model | Shape |
|---|---|
| `questra` | A straight, open north and south |
| `quebnd1`, `quebnd2` | A bend, open east and south; their planks laid each way |
| `queend` | A straight, its posts at its north end |
| `quebin1` | A bend open east and south, its posts at the south |
| `quebin2` | The same, its posts at the east |
| `quedead` | Open at its south alone |

Each cell's model and angle follow from the queue's shape: a straight
or a bend along it; at its last cell, by the path, `queend` or the bend
with its posts towards the path; a dead end where it stops short of the
path, open back along it. Which of the two bends is which way the queue
runs isn't known: Reluva draws the bend whose east side faces the path
as `quebnd1`, the other `quebnd2` *(guessed)*. The saved park gives
each cell's piece and angle too ([tpws.md](../formats/tpws.md#the-grid)),
2 for a straight, 4 the bend, 5 the piece by the path: the pieces
Reluva works out from its queue's shape are those, every one, at those
angles, 5 of 5 *(confirmed)*.

A queue costs `Costs.QueueCell`, 25 a cell. A new attraction's queue
is laid as it's placed ([building.md](building.md)): "Edit line. Click to build a
line. Click again to finish. Click onto existing path to connect"
(`UIHELPTEXT` 149, 150). Reluva lays it so once a ride's placed, and with the path tool too: a
click on an attraction's entrance that has no queue, or on the loose
end of one that doesn't reach the path, starts a line; each further click lays a straight length on from its end, over
bare land, joining the path if it meets it, which finishes it; a click
on its end finishes it short *(guessed)*. The prebuilt park's one
entrance without its queue is behind its hoardings, so has none to
lay.

## In Reluva

`Park` (`src/engines/tpw/park/park.cpp`) reads each cell's ground from
the attribute map (bare, path, or not the park's), lays lengths
(`pathRun`, `buildPath`) and charges for them. `pathTileFor`
(`src/engines/tpw/park/path_tiles.cpp`) picks each cell's texture and
turn; `ParkScreen` draws the path a twentieth of a unit over the ground,
and, while the path tool is chosen, the length that would be laid, the
cells it can't cross marked red, and finds the cell under the mouse.
`playPark` takes the tool's clicks, and lays queues as above, the line
being laid drawn as it would be. "When building paths, lines, tracks
and track supports, you can press the Backspace key to undo sections
which you have already placed" (`TOTD` 4): Backspace takes up the
lengths of path laid while the path tool's chosen, the last first, and
laying goes on from where that one began; and a line's lengths, while
it's being laid; what they cost paid back *(guessed)*. `Park::queuePieces`
(`src/engines/tpw/park/queues.cpp`) works out each queue's pieces; the
prebuilt park's queue is followed from its entrance. Queues' and
objects' cells can't be laid over.

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
- Which of `quebnd1` and `quebnd2` the game uses which way, and the
  save's numbers for them and the other pieces; how the game lays a line
  once attractions can be placed.
