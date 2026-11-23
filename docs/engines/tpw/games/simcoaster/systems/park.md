# SimCoaster: the park

> **Status:** partial · **Code:** `src/engines/tpw/park/park.cpp` · **Checked against:** a recording, and the four worlds' files · **Confidence:** inferred unless marked

## Overview

After a world is chosen on the globe ([globe.md](globe.md)) and its
loading screen ([loading-screen.md](loading-screen.md)), the game
flies the camera along the road to the park's gates, shows the
mission's briefing ("Current Objective") and the park runs: from 1:34
in the recording ([research/footage.md](../research/footage.md)). A new
park is the world's own land and buildings, with what its settings and
its mission give it. Reluva sets one up and shows it running, with its
money and its date, and a camera that moves over it; building, guests
and the rest are still to come ([PLAN.md](../PLAN.md#phases)).

## How it works

### The park's cells

The park is laid out in cells 10 units square: cell (x, y) of the
settings is the world's square from (10x, 10y) to (10x + 10, 10y + 10)
along its x and z, on the ground at height 0 *(inferred)*. The world's
attribute map ([attribute-map.md](../formats/attribute-map.md)) has a
tile for each cell, at row x and column y, and set so it matches what
the settings say is there: the entrance's two cells (47, 17) and (48,
17) are its path's tiles; the ticket booths' cells (45, 8) and (45, 11)
its booths'. Drawn over the world, the booths' cells fall on the booths
in the world's model, the bus stops' by its shelters, the entrance in
the gap of the park's wall; and the attribute map's river tiles fall
where the world's river emitters are ([omp.md](../formats/omp.md)),
x 610 to 950 and z 130 to 460 in Land of Invention. The ground's cells
([lnd.md](../formats/lnd.md)) are a quarter of a park cell, 2.5 units.

### What a new park has

- **The world's fixed items** (`levels/<world>/Standard.sam`,
  `FixedItemInfo`): two ticket booths (`TicketBooth[0]` and `[1]`, with
  an angle of 512, a half turn of 1,024 perhaps *(guessed)*), the
  entrance's two cells, three bus stops, the crossings' cells on the
  bus's side and the park's, and a strike area. The entrance's and the first booth's cells are the
  same in all three playable worlds. All but the gates are the world's own model's
  (`terrain/base.MD2`); the gates are the world's `features/Gates`
  object, `ParkName.GateObjectId` 1601 in its `global.sam`.
- **The mission's** (`missions/missNN.sam`; Operation Expand!, the first,
  is `miss00`): its objective (`ExitStatistic[0]`: `Type` 8 with
  `TargetValue` 40, "Get 40 people in the park at once", as its briefing
  says), the obstacle it clears (`Obstacle.Type` 1700), and the park's
  land as `BoundaryClicks`, 43 corners in the first mission, from (0, 0)
  to (−50, 66): offsets from the entrance, it seems *(guessed)*.
- **Its money**: `BankAccountInfo.InitialCash` 50000 of the general
  settings (`levels/Standard.sam`), and what each visitor pays to come
  in, `InitialAdmissionFee` 25. The recording's money is § 50025 as the
  briefing closes: the first visitor's fee, it seems *(inferred)*.

### Time

The park opens on 1 January 2000; the HUD's date reads "14.Jan.00" at
2:30 in the recording, 19 Jan at 2:50, 21 Jan at 3:00, 26 Jan at 3:20,
30 Jan at 3:35: a day every 4 seconds *(inferred)*, the first day back
at about 1:38, as the camera flies in. Over the whole recording the
days come slower, 4.6 seconds each (19 October at 24:00): the game
stops its clock, it seems, while some windows are open.

### The HUD

At 640 × 480: the money at the top left in `CASHSMALL`, "§ 50025",
without separators; the date in its black box at the top right, under
the compass, in `DATESMALL`, as "30.Jan.00" (the month's name from
`MONTHS.str`); the thermometer of the park's mood at the left, its face
(`face1` to `face6` of `ui.wad`) beneath; the toolbar along the bottom
and the buttons down the left.

## In Reluva

`Park` (`src/engines/tpw/park/park.cpp`) sets up a new park from the
general settings, the world's and the mission's, and keeps its clock;
`ParkScreen` (`src/engines/tpw/ui/park_screen.cpp`) draws its world, as the viewer draws it
([lnd.md](../formats/lnd.md#in-reluva)), through a camera that starts
on the road before the entrance, the fixed items marked (the entrance
yellow, the booths red, the bus stops blue), the money and date over
it. `playPark` runs it after the loading screen: the arrow keys move
the camera, Q and E turn it, Page Up and Page Down or the wheel zoom,
Home goes back, Esc leaves the game. Its first mission's, whatever the
player's objective, for now.

`reluva-tool tpw park-shot <game folder> <world folder> <out.png>
[seconds] [yaw pitch distance]` draws a new park, its clock run on;
`reluva-tool tpw park-run <game folder> <world folder>` plays one
headless, moving the camera by its keys, with a checksum of the
screens, which the game-files test checks (`tpw.gamedata.park`). The
unit tests (`tpw.unit.park`) set a park up from settings built in the
test and check its calendar.

## Sources and checks

- **Where the facts come from:** the general settings, the worlds' and
  the missions'; the worlds' attribute maps and placed objects; the
  recording from 1:25 on, its HUD's money and dates read off its frames.
- **How it was checked:** `park-shot` from above Land of Invention's
  entrance: the booths', bus stops' and entrance's cells on the world's
  booths, shelters and the wall's gap.

## Open questions

- What the boundary's clicks are measured from, and how the park's
  land and its fence are drawn by them.
- The fly-in along the road and the briefing window; the toolbar, the
  thermometer and the rest of the HUD; the gates' model at the entrance.
- How fast the clock runs at each of the game's speeds, and when it
  stops.
- How visitors arrive (`Arrival` in the general settings), what they
  pay and do: the park's running, the rest of this phase.
