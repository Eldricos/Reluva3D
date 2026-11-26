# Star Trek: Armada: building

> **Status:** partial · **Code:** `src/engines/storm3d/play/mission_view.cpp`, `src/engines/storm3d/play/hud.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, its frames and draws as its starbase built a construction ship · **Confidence:** seen, the rules for what's lit and where ships go inferred from one build

## Overview

A unit with a `buildItem0` in its definition (a starbase, a
construction ship) has the build button on its control panel. Pressed,
the control panel shows what it can build; an item pressed is paid for
at once and built over its `buildTime`, after which the new unit flies
out.

## How it works

### The build menu

From the original's draws after its starbase's build button:

- **Its items** are the definition's `buildItem0`, `buildItem1` and on,
  but not the officers' quarters (`fedoff`, `borgoff` and the rest,
  class `upgrade`), which are the officers' button's. Each is a button
  from the control panel's first place on: its icon `b_<item>` in the
  button grey, its border `bb_build` added on in the border grey
  ([gui-config.md](../formats/gui-config.md)).
- **A dark one:** the starbase's Dilithium Freighter was near black, as
  an order a unit can't give. The player had no mining station, and the
  computer player's build list (`federation_build_list.aip`) builds one
  before each pair of freighters: a freighter waits for a mining station
  *(inferred)*.
- **Back:** `b_back` in the last place, in its standard border.

### An item built

The construction ship's button pressed at 46.0 seconds:

- **Paid at once:** dilithium 3000 to 2500, crew down 500, officers 4 to
  6 of 120: its `dilithiumCost`, `crewCost` and `officerCost`.
- **While it's built,** its button is drawn in (214, 174, 131) with its
  border in (254, 235, 142); a cancel button, `b_cancel`, takes the place
  before back; the rail over the info panel shows it, in its build
  border, and over it the icon again in orange (255, 153, 51) as far
  round from twelve o'clock, clockwise, as it's got, a fan from its
  middle (its angle within a few degrees of its share of 360°); and the
  info panel's construction bar fills, 13.3 of its 48 pixels at 60.0
  seconds and 46.9 at 95.0, as the time since the press over its
  `buildTime` of 50 seconds, exactly.
- **Done,** about 96 seconds, the new construction ship came out of the
  starbase and flew to 131 to its −x and 110 to its −z, 15 above its
  plane, where it stopped: placed back in the world from the main view's
  draws at 99, 101 and 103 seconds, from one build *(inferred)*.

### A station built by a construction ship

From the original's frames every second and its draws at 46, 48, 50,
55, 62, 70, 80 and 88 seconds, a construction ship building a Pulse
Cannon (`fturret`, 250 dilithium, 2 officers, 25 seconds):

- **Its menu:** the construction ship's ten items, the first six lit
  (Starbase, Shipyard, Sensor Array, Pulse Cannon, Mining Station,
  Research Facility), the last four dark (Science Station, Advanced
  Shipyard, Torpedo Turret, Temporal Research). Their definitions don't
  say why: what each needs is *(guessed)*, a research facility, and a
  shipyard for the advanced one.
- **Placing:** the item pressed is drawn in (254, 235, 142), and over the
  map the station's model follows the pointer on the plane its builder
  is on, where it will stand: not moved to a cell's middle. Under it, on
  the plane y = 0, the placement grid's cells (`RTS_CFG.h`'s
  `STATION_PLACEMENT_GRID_SIZE`, 50): the cells round the starbase in
  yellow outlines, its own in green. The cells' texture is the
  program's, made in memory; Reluva draws its like them
  *(guessed)*. The yellow block spans the starbase's ring's square and
  a cell round it, across x; along z, a cell more than that, not
  accounted for.
- **Sent:** a click there placed it, and the ship flew to 53 from the
  site, on its side, and stopped. Nothing was paid yet: dilithium was
  3000 until between 58 and 59 seconds, when the ship had got there, and
  2750 after; officers 4 to 6.
- **Built:** worker bees flew round the site, the station's model came
  bit by bit, and it was whole by 88 seconds, 25 after it was paid for.
  All the menu's items were dark meanwhile, and cancel lit.

## In Reluva

`MissionView` opens the menu from the build button (`Hud::controlAt`
finds the button under a click), lists the items (`buildListOf`, lit by
`buildMenuOf`), pays for one (`orderOf`, `pay`) and queues it on its
builder; each builder's first item is built on by the frame's time, and
when it's done, `MapScene::addUnit` adds the unit at its builder, which
flies to `launchPointOf` by the movement rules
([movement.md](movement.md)). A second item pressed is queued after the
first. Cancel takes off the last ordered and pays it back *(guessed)*.

A ship that moves places a station instead: the station's model at the
pointer with the cells (`takenCellsOf`; a click on a taken cell does
nothing), then the ship flies to `standOffOf` the site, pays as it gets
there, and the station is added when its `buildTime` has passed. Its
cancel gives the site up, paid back if paid. The worker bees and the
station's model coming bit by bit aren't drawn yet.

## Sources and checks

- **Where the facts come from:** the original's frames every second and
  its draws at 46.5, 60, 75, 95, 99, 101 and 103 seconds; the
  definitions; the computer player's build lists.
- **How it was checked:** `engine-shot` pressing the same buttons, set
  beside the original's frames: the menu to the pixel, and at 25 and 52
  seconds the counters, the buttons, the rail and the bar, and the new
  ship flying out the same way. The unit tests (`storm3d.unit.play`)
  check the menu, the costs and the launch point.

## Open questions

- What makes other items dark, and whether a menu item is dark without
  the dilithium or crew for it.
- What cancel takes off and pays back, and how the queue is shown past
  its first.
- Where other builders' units come out, and what the tooltips say.
- The rail's wipe's angle, a few degrees from the bar's share.
- What makes the construction ship's last four items dark, and the
  other races' equivalents.
- Which cells a unit takes, along z, and whether ships take any.
