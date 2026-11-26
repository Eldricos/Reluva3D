# Star Trek: Armada: mining dilithium

> **Status:** partial · **Code:** `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, its frames every second and draws every five, as a Mining Station was built and its freighter worked · **Confidence:** seen, where the freighter mines inferred

## Overview

Dilithium comes from the dilithium moons (`mdmoon`, class `scrap`): a
Mining Station (`fmining`) built by a construction ship comes with a
Dilithium Freighter (`ffreight`), which mines the moon and carries what
it mines back to the station, where it's added to the player's
dilithium.

## How it works

The original was run to 266 seconds of its clock: a construction ship
sent to build a Mining Station about 300 from the first moon (at
1118.5, 577), the camera moved there by a click on the minimap.

- **The station** was paid for as the ship got there (1000 dilithium,
  250 crew, 4 officers) and stood whole by about 108 seconds. Its
  builder waited 100 from its middle, where it had waited 53 from a
  Pulse Cannon's: each the station's ring's half side and 37
  *(inferred)*.
- **Its freighter** came with it, out of the station, unasked: a second
  ordered from the starbase wasn't needed for anything seen here.
- **Its round,** read from its model placed back in the world from the
  draws: it flew out past the moon's side and out of view beyond it, a
  little more than 100 from the moon, mined there, came back and docked
  at the station's middle (920, 796), and unloaded.
- **The dilithium** rose by 150 each time, the freighter's `maxScrap`,
  by 15 a second for ten seconds (2000 to 2150 from 143 to 153
  seconds), the station's `dilithiumTransferRate`. The rounds began
  unloading at 143.0, 187.3 and 231.5 seconds: one each 44.3 seconds,
  the first about 33 after the freighter came out.
- **Its beam,** `gminebm`, reaches 120, fires every 1.5 seconds, and its
  ordnance (`minebm`) does 15 a shot: 150 in ten shots.

## In Reluva

When a Mining Station is built, a freighter of its race (its letter
and `freight`) comes out of it and works the nearest moon
(`MissionView::work`): it flies to 100 from the moon, at its side
square to the way from the station *(inferred)*, takes a shot of 15 as
it gets there and one each 1.5 seconds to its 150, flies back to the
station's middle and unloads 15 a second into the player's dilithium,
up to `RTS_CFG.h`'s `cfgMaxDilithium` (999999); then again. With the
movement rules ([movement.md](movement.md)), its rounds come each 44.6
seconds, the first 33 after it comes out, as the original's. Its beam
isn't drawn yet.

## Sources and checks

- **Where the facts come from:** the original's frames every second, the
  dilithium counter in them, and its draws every five seconds from 60 to
  265; the definitions (`ffreight`, `freight`, `gminebm`, `minebm`,
  `fmining`, `mdmoon`).
- **How it was checked:** the unit tests (`storm3d.unit.play`) fly a
  freighter between the original's station and moon and check its
  deliveries' times and amounts; `engine-shot` placing the station as
  the original's run did.

## Open questions

- Where exactly the freighter mines, out of the original's view, and
  how it docks (it came into the station from its south-west).
- Whether a mining station must be near a moon, and how far.
- Freighters ordered from the starbase, more than one at a station, and
  moons running dry.
