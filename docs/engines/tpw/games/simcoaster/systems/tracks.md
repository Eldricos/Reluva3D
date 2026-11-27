# SimCoaster: go-kart and water tracks

> **Status:** partial · **Code:** `src/engines/tpw/park/tracks.cpp`, `src/engines/tpw/ui/park_screen.cpp` · **Checked against:** the three worlds' settings, models and strings · **Confidence:** guessed unless marked

## Overview

Six rides run on a track the player lays: each world's go-karts
(Miniature Dreadnoughts, Lobster Laps, Scarab go karts) and water ride
(Giant Waterwheel, Cruise water track ride, Nile Cruise). The player
buys the ride's station, then lays its track from beside it, round and
back. The game's strings for it: the ride's menu's "Build/Edit track"
and "Delete a section of track" (`UITEXT` 527, 528, beside "Build/Edit
line"); "Edit track", "Click to build the track / Click again to finish
/ Click onto a track end to connect" (`UIHELPTEXT` 151, 152); the
letter "You've built a coaster or ride, but haven't built any track.
You'll need to do so before the ride can be opened!" (`TAG_SYSTEM` 256,
257); and the tips "When building go-kart and water ride tracks, you can
drag over some existing track to make a crossroads, or drag over paths
and lines to build bridges" and "you can press the Backspace key to
undo sections which you have already placed" (`TOTD` 1, 4). The
recording builds none, so how it all looks and works is guessed from
these and the files.

## How it works

### The settings

A ride's own settings (`rides/gokarts/Gokarts.sam`):

| Setting | What |
|---|---|
| `Bumper.WhichTrackType` | "0=no track, 1=car track, 2=water track"; the coasters and log flumes have 3 *(confirmed)* |
| `Bumper.BumperType` | the cars: −4 "go karts", −5 "RIDETYPE_LOSTWORLD_WATERTRACK" |
| `Bumper.NorthXAdjust`, `NorthYAdjust`, and East, South, West | "some constants to line up the Track sections with the rest of the co-ordinate system": (−2, 1), (1, 4), (4, 1), (1, −2) for all six |
| `SupplementalMeshes[n].FileName` | the cars' models: the go-karts' `gk_blue.md2`, `gk_green.md2`, `gk_red.md2`, `gk_yell.md2`, the water rides' `wr_ring.md2` |
| `Research.Type` | 5, "Has Track" |

The general settings' `Costs.KartTrackCell` (40) and `WaterTrackCell`
(50) are the track's cost; `ParkAward.GokartSpeed` (5, "just the longest
straight section of track at the moment") and `WaterLength` (50) its
awards ([park-awards.md](park-awards.md)).

### The pieces

Each ride's folder has its track's pieces, two cells square each, as
the hoardings' are ([hoardings.md](../formats/hoardings.md)): the
go-karts' `gk_trck*`, the water rides' `wr_trck*`:

| Piece | What it is |
|---|---|
| `S` | A straight, along z: a go-kart's floor at height 5 (`wood02`) between walls 10 high; a water track's water at 3.5 (`water01`) |
| `B`, `B2` | A bend from the +x side to the +z |
| `X`, `X2` (water) | A crossing |
| `Q` | A straight closed at its −z end: the go-karts' with a wall (`gk_trkend01`), the water's half a block, its end round *(inferred)* |
| `M` (go-karts) | A straight like `S`, with fewer positions |
| `H`, `H_U`, `H_D`, `H_A` | A bridge, its ramps up and down, and a bridge's middle, 11 high |

The water track's pieces have a second model each, `m` after their
names (`wr_trcksm.MD2`), which Reluva can't read. The station's model
is four cells square (`gokarts.MD2`'s `base4x4`).

### Laying it

Reluva's rules, all guessed: a track is a run of blocks two cells
square, the first beside the station where `Bumper.NorthXAdjust` and
`NorthYAdjust` put it from the station's first cell, turned as the
station is. That's the middle of its west side unturned, so it runs
along that side, north and south. From there the player clicks: each
click lays a straight run of blocks to the one clicked, each block's
four cells bare land, at `KartTrackCell` or `WaterTrackCell` a block. A
run never turns back on itself, nor turns where the track crosses
itself. It may cross the track straight across a straight, making a
crossing, but not stop on one. The track is whole when a run comes back
into the first block going the way the track left it; then the ride can
run. Backspace, or "Delete a section of track", takes up the last block,
for nothing back.

"Drag over paths and lines to build bridges. You can also drag paths
and lines under existing track, and bridges will be built" (`TOTD` 1):
a block may be laid over path or a line, which stay, so long as the
track goes straight on through it, not stopping or turning there (its
first block over bare land only); and path or a line may be laid under
a straight block of track with blocks on both sides of it. A block over
them is a bridge: one alone the hump `H`, a run of them `H_U` rising,
`H_A` level and `H_D` coming down, their floors 4.4 higher, a hump's
5 *(inferred)*. Taken up, the path or line under a track leaves the
track there. Ramps turned so the track climbs the way it's laid, as they
look right drawn so *(inferred)*.

A new track ride's track is laid as soon as its line is. A day after
one's built without a whole track, its letter comes, once.

### Running

The station's script waits on `BUMP 5`, the track valid, before it lets
anyone on (`Gokarts.RSE`, [script-instructions.md](script-instructions.md)).
While it's not whole, the ride stands waiting. Its riders' cars go round
the track, one a rider, spaced along it, through the middles of the
sides its blocks meet at, at 30 units a second for the karts and 12 for
the boats, each facing the way it goes (a kart's front is its model's
−z).

### Add-ons

The worlds' `upgrades/` hold add-ons for their track rides
(`AddOn.UpgradeType` 1, `AddOn.UpgradesId` the ride): Land of
Invention's go-kart jump and tunnel (`dr_jump`, `dr_tun`) and water
ride's drop (`mg_drop`), Polar Zone's `ll_jump`, `ll_tun` and `ic_berg`,
Arabian Nights' `gk_jump`, `gk_tun` and `wr_cats`; § 800 each,
`CostOfResearch` 50, or 100 in Arabian Nights. Each is four cells square (`*++*`), its model
holding two blocks of track down its middle. Reluva offers them in the
ride's "Upgrade Ride" (`UITEXT` 10, by their names), placed over two
blocks of straight track where the player clicks, the track going on
straight either side, not over a crossing, a bridge or another add-on;
drawn in place of those blocks, each adds 10 to its ride's excitement
*(guessed)*. They're on sale without research, and taken up with the
track under them.

## In Reluva

`Park::layTrack`, `undoTrack`, `trackStart`, `trackComplete`,
`trackPieces` and `trackCircuit` (`src/engines/tpw/park/tracks.cpp`);
a track's cells are `Ground::kTrack`, and go with the ride when it's
taken down. The track a block at a time, `TrackModel`: the first block
`M` (go-karts) or `S` (water), straights `S`, bends `B`, crossings `X`,
an unfinished track's end `Q`, and bridges `H`, `H_U`, `H_A`, `H_D`. `ParkScreen` draws the pieces and
the cars, and tells each ride's script whether its track's whole.
The prebuilt park's waterwheel has its track in the game's save, which
isn't read: it runs as it did. `reluva-tool tpw track-shot <game folder>
<world folder> <out.png> [seconds] [-water]` places a world's go-kart
ride, or water ride, lays a loop of eight blocks beside it, and draws
it; with `-bridge`, a longer loop over a band of path, bridged; with
`-addon`, a longer loop with the ride's first add-on. The unit tests (`tpw.unit.park`) lay a go-kart track that crosses
itself, refuse the wrong runs, take a block up, and take the ride down;
and bridge a path, and lay path under the track.

## Sources and checks

- **Where the facts come from:** the rides' settings and their
  comments; the general settings; the pieces' and cars' models (their
  meshes' sizes and textures); `UITEXT`, `UIHELPTEXT`, `TAG_SYSTEM` and
  `TOTD`; the go-karts' script.
- **How it was checked:** `track-shot` in Land of Invention and Polar
  Zone, the pieces meeting edge to edge round the loop, the cars on the
  floor and in the water.

## Open questions

- Everything about the game's own way of laying it: the grid, where the
  track meets the station, whether it starts and ends there, what each
  of `NorthXAdjust` and the others is for, and the piece models' uses
  (`M`, `B2`, `X2`, `Q`).
- What a block costs: a cell, or the piece.
- Whether the game's bridges need the track straight, and how its ramps
  go on a bridge of one block.
- The cars: how many, how fast, their riders, and what `BUMP`'s other
  commands do.
- How add-ons are bought and placed, and what they do to a ride; their
  research.
- How the go-kart award's "longest straight section" and the water
  ride's length are counted.
