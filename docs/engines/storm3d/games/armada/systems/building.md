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

## In Reluva

`MissionView` opens the menu from the build button (`Hud::controlAt`
finds the button under a click), lists the items (`buildListOf`, lit by
`buildMenuOf`), pays for one (`orderOf`, `pay`) and queues it on its
builder; each builder's first item is built on by the frame's time, and
when it's done, `MapScene::addUnit` adds the unit at its builder, which
flies to `launchPointOf` by the movement rules
([movement.md](movement.md)). A second item pressed is queued after the
first. Cancel takes off the last ordered and pays it back *(guessed)*.

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
