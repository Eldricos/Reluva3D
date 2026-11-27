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
a stop. Warp is its top speed, `warpSpeed`: the physics files (the
destroyer's `destphys.odf` 100, the construction ship's 50) keep it
for a ship headed within `headingCloseEnoughForWarp` of its way and
further than `tooCloseForWarp` (10) from its end, flown at
`impulseSpeed` nearer *(inferred)*. A point behind it and nearer than
`tooCloseToTurn` (40) it backs to without turning, at
`maxReverseSpeed` (0.667) of its impulse *(inferred)*: the keys'
names say so; the original wasn't seen doing either. In the way of a unit that doesn't move, whose ring's half side
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
- **Keeping clear,** by `RTS_CFG.h`'s collision avoidance and pathing
  settings:
  - **the way,** set as a ship's sent, goes round each standing unit,
    asteroid field and black hole it would come too near, nearest first,
    each on the side away from it and on from there. An asteroid field
    is kept its `fieldRadius` and 150 off (`ASTEROID_BELT_PATHING_DISTANCE`:
    a belt costs 201 to path through, over the 200 nothing paths into),
    a black hole half again its size (`BLACKHOLE_PATHING_SAFETY_COEFFICIENT`,
    impassable). Nebulae, which cost 198, are flown through
    *(guessed)*: the settings price them, but the planner that uses the
    prices (a quadtree of cells 50 to `HIGHEST_QUADTREE_GRID_RESOLUTION`)
    isn't made;
  - **as it flies,** each moving ship looks 1.4 seconds ahead
    (`COLLISION_LOOK_AHEAD_TIME`) for any other unit it would come within
    1.2 times their sizes together of (`COLLISION_SAFETY_MARGIN`, their
    rings' half sides), and is pushed away from where it would be by how
    far inside it comes, the push 0.7 last frame's and 0.3 this one's
    (`OLD_AVOIDANCE_FORCE_WEIGHT`), at its speed, or 10 a second at a
    stop *(guessed)*: how the push moves it isn't known. It's pushed
    across the plane only: `Y_REPULSION`'s climbing and diving waits on
    ships changing height;
  - **the map's edge:** ships and where they're sent kept 100 inside it
    (`MAP_EDGE_BUFFER`).

  `engine-shot` sent both construction ships to one point: they came to
  it side by side, apart.

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

- How the original's collision avoidance moves a ship by its push, and
  its path planner over the quadtree with the terrain costs: whether a
  nebula is gone round or through.
- How the program steers: its `turnRadius`, `pathLeadDistance`,
  inertias and coefficients, and its path finding round more than one
  obstacle, or round other ships.
- How ships climb, and to what height.
- How a group moves: formation, or each to the point.
- Whether the original backs up so, and how its other physics files'
  ships fly (only the construction ship's flights were measured).
