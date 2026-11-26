# SimCoaster: the park's land

> **Status:** partial · **Code:** `src/engines/tpw/park/land.cpp`, `src/engines/tpw/park/missions.cpp` · **Checked against:** the worlds' and missions' files, the map pictures · **Confidence:** inferred unless marked

## Overview

A park is built only inside its fence: a new park's the world's own
land, round the prebuilt park, and the rest of the world lies beyond,
the special rides and obstacles there among it. The president's letters
promise more: "If you can get {0} people into the park at one time,
we'll let you open up a whole new area" (`MISSION_DESC` 0), "we'll add
the land beyond it to your park" (3). Missions that do give a new fence
(`Completion.LoadNewHoardings` 1, "New hoardings are loaded", and their
`BoundaryClicks`, "Boundary set when mission is complete").

## How it works

- **A new park's land** is the cells inside its world's
  `hoardings.map` ([hoardings.md](../formats/hoardings.md)) *(confirmed)*.
- **A mission done** in this world with new hoardings adds the cells
  inside its fence. Each mission's fence is the whole park's, the last
  one's taking in the earlier land but for a few cells; Reluva keeps
  every cell the park had *(guessed)*.
- **Beyond the fence** nothing is built or laid, and visitors don't
  walk; the prebuilt park's paths and objects there stay, drawn as they
  are.

Land of Invention's grows with Operation Expand! (the land beyond the
gates), Operation Plughole! (the lake to the west), and missions 8 and 9
(beyond the fallen trees, across the river); Polar Zone's with missions
3, 11, 12 and 13; Arabian Nights' with 5, 6, 15 and 16, as the
missions' letters place them.

The Park Map darkens the land beyond the fence, by guess: the recording
shows only a park that hasn't grown ([park-windows.md](park-windows.md)).

## In Reluva

`Park::owns` and `growLand` (`src/engines/tpw/park/land.cpp`); a cell
beyond the fence is `Ground::kOutside`. `reluva-tool tpw window-shot
<game folder> <world folder> <out.png> map -mission <n>` draws the Park
Map as the missions before n leave the land. The unit tests
(`tpw.unit.park`) grow a park's land with a mission's fence.

## Sources and checks

- **Where the facts come from:** the worlds' `hoardings.map` and
  `Hoardings.sam`; the missions' settings and comments; `MISSION_DESC.str`.
- **How it was checked:** `window-shot map -mission` 0, 1 and 2 over Land
  of Invention, 5 and 7 over Arabian Nights, 3 and 4 over Polar Zone: the
  land grows where the letters say, its edges along the pictures' walls,
  ridges and rivers.

## Open questions

- Whether a mission's fence replaces the land or adds to it, where it
  leaves out a few of the earlier cells.
- How the fence is drawn: the hoardings' models (`hoarding.wad`'s
  `ho_fob`, `ho_fos` and `ho_end` pieces) aren't drawn yet.
