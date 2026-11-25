# Star Trek: Armada: how ships move

> **Status:** partial · **Code:** `src/engines/storm3d/play/movement.cpp`, `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, its draws every tenth of a second · **Confidence:** seen, the rules inferred from two flights of one ship

## Overview

A ship selected is sent somewhere with the right button: it turns
towards the point, gathers speed, flies round a station in its way,
and brakes to a stop there. How fast and how sharply comes from its
physics file, named by its definition's `physicsFile`
(`cnstphys.odf` for the construction ships); the rest was fitted to
the original's tracks.

## How it works

### Watching the original

A construction ship in a skirmish's first minute was selected, then
sent with the right button to a point on the map (a left click there
does nothing). The host wrote the original's draws every tenth of a
second, each with its frame's time ([hosting.md](hosting.md)). The
selection's ring, a quad on the plane under the ship, placed back in
the world through the overview camera ([camera.md](camera.md)), gives
where the ship was; its direction of travel, its heading. The ring
doesn't turn with the ship.

Two flights:

- **Straight ahead,** 145 units: it set off a third of a second after
  the click, was at 46 a second after one second and 49 to 50 after
  two, held it, and braked to a stop over its last 70 units, its speed
  about as the square root of the distance left.
- **To a point past the starbase,** whose straight line passed 52 from
  the starbase's middle, inside its reach: the ship flew up the side
  away from it, slowed to about 26 as it turned, then turned at about
  40° a second towards the point while gathering speed, and passed the
  starbase 75 from its middle before braking to a stop.

Each stopped within 3 to 6 units of the point where the click's ray
meets the plane its units stand on (y 38.5). The ship rose as it flew,
about 0.6 a second (its file's `maxVerticalSpeed` is 0.667), 2 to 3
units in all.

### The rules, as fitted

A ship's speed closes on the speed it wants at 4.5 a second (an
exponential approach), and its heading turns towards where it's going
at its `yawRotationRate`, in radians a second. The speed it wants is
the least of:

- `warpSpeed`, 50: its top speed *(inferred)*: it held 49 to 50;
- what lets it stop at the point, braking at 14 units a second, every
  second (√(2 × 14 × distance left)) *(inferred)*;
- `impulseSpeed`, 20, while the way it's going is more than
  `headingCloseEnoughForWarp` (30°) off its heading *(inferred)*.

It's there within 8 units of the point *(inferred)*, and then slows to
a stop. In the way of a unit that doesn't move, whose ring's half side
and the ship's together reach the straight line, it first flies to a
point square to the line from that unit's middle, on the side away from
it, 1.4 times those half sides out *(inferred)*; within 35 of that
point it heads on for the end *(inferred)*.

These, from both flights at once, put the ring within 3.3 units of the
original's, on average, at each tenth of a second. The file's own
`turnRadius`, `pathLeadDistance` and inertias may say the same in the
game's own way; how the program uses them isn't known.

A group keeps its shape: each ship goes to the point plus its place
from the group's middle *(guessed)*.

## In Reluva

- **Ordering:** the right button on the map, with units selected that
  move, sends them to where its ray meets the plane they're on
  (`MapScene::pointOn`); the cursor over the map is then the move
  cursor (`c_move`), as the original's was.
- **Flying:** `Movement` holds a ship's place, heading, speed, goal and
  way, and steps it by the rules above each twentieth of a second;
  `MapScene::moveUnit` turns and moves its model, ring and box.

## Sources and checks

- **Where the facts come from:** the original's draws every tenth of a
  second over two flights; `cnstphys.odf`; the model's box from
  `fconst.sod`.
- **How it was checked:** our flight, shot by `engine-shot` every second
  from the right click, set beside the original's frames at the same
  times: the way round the starbase, the turn and the stop match. The
  unit tests (`storm3d.unit.play`) check the straight flight's times and
  speeds, the way round, the click's point and a group's goals.

## Open questions

- How the program steers: its `turnRadius`, `pathLeadDistance`,
  inertias and coefficients, and its path finding round more than one
  obstacle, or round other ships.
- How ships climb, and to what height.
- How a group moves: formation, or each to the point.
- Warp, whose speed `warpSpeed` may be only at a distance, and the
  other physics files' ships.
