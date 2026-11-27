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

A group, as the original sent its two construction ships, 53 apart
side by side, to a point 300 away (both selected with a box, then the
right button): the one on the right came to the point (to 4 units,
placed by its model in the draws), the other to 100 left of it, its
side kept, its gap now `RTS_CFG.h`'s `FORMATION_SPACING`. So the first
goes to the point, and each other keeps its way from it, at least 100
off *(inferred)*: one flight; which ship is first, and whether a group
further apart keeps its shape, weren't seen. The files' formation moves
(`FORMATION_LEADER_SLOWDOWN` 0.9, `FORMATIONS_RELATIVE_TO_SHIP`) are off
by default (`DEFAULT_TO_FORMATION_MOVE` 0) and no key in `Input.map`
asks for one.

### Height, and moving in a fight

From the code *(from the code)*:

- **Height:** `RTS_CFG.h`'s `SHIP_COMBAT_FLOOR` (10) and
  `SHIP_COMBAT_CEILING` (67) make the plane ships fly on, their middle,
  38.5: where a ship is sent, and every point the program makes for
  ships to go to, is at that height. In a fight the steering is three
  dimensional, but its upward part is dropped while a ship is above the
  ceiling and its downward part while it's below the floor (0x42b53d,
  0x42c457 and five more places).
- **Centres of combat:** ships fighting are gathered to centres, one
  within twice `CENTER_OF_COMBAT_PERSISTENCE_CUTOFF` (24) of a ship
  taking it (0x422960); each centre's ships are given parts, in turn 3,
  2 and 1, 1 for one without weapons, and others 7 (0x422e1f). A ship of
  part 7 is steered round its centre, at the plane's height: towards a
  point `ANCHOR_RADIUS` (4) out from it, turned
  `ANCHOR_PURSUIT_POINT_ANGLE` (30 degrees) from the way to the ship,
  with `ANCHOR_PURSUIT_FORCE` (1) (0x42c2e0). What the other parts do
  wasn't followed.
- **`DOGFIGHT_RANGE`** (70) is read from `RTS_CFG.h` and never used.

Seen: a Defiant sent to the computer player's base fought its starbase
from where it stopped, within reach, at 38.5, neither circling nor
climbing ([combat.md](combat.md#a-fight-with-the-computer)).

## The path planner

From the code *(from the code)*, the program keeps what it costs to fly
over each part of the map in a quadtree (`A_Star_Graph_Map`), over the
map and 500 past each edge (0x408d38), split down to
`HIGHEST_QUADTREE_GRID_RESOLUTION` (50); each part costs
`DEFAULT_TERRAIN_VALUE` (100) but where an object marks a square
(0x4097c0, its middle and a half side, the square kept within the map):

| What | Its square, each way | Its cost |
|---|---|---|
| A nebula (`nebula_obj`) | its `effectRadius` × `NEBULAE_PATHING_SAFETY_COEFFICIENT` (1.8) | its own `pathing_cost`, 199 if it says none (0x449934) |
| An asteroid belt | half `ASTEROID_BELT_PATHING_DISTANCE`, 75, one each 75 along it (0x433026) | `ASTEROID_BELT_PATHING_COST`, 201 |
| A black hole | its size × `BLACKHOLE_PATHING_SAFETY_COEFFICIENT` (1.5) (0x44dd25) | `BLACKHOLE_PATHING_COST`, impassable |
| A wormhole | its size and 80 (0x451415) | 500 |

Off the map costs `OFF_MAP_PATHING_COST` (90,000), and no way goes into
a part dearer than `CANNOT_PATH_INTO_COST` (200) or impassable. So the
radioactive nebulae (`pathing_cost` 2000), and those at 400 and 800, are
gone round; the Metaphasic Nebula's 2 is cheaper than open space; a plain
nebula, 199, is gone through if going round costs more. The settings'
`NEBULAE_PATHING_COST` (198) is read but never used. The search is an A*
(`A_Star_Search`), and its way is then smoothed
(`PERFORM_RUBBER_BANDING`).

What a step costs in the search isn't read from the code yet; Reluva
takes its length times its cell's cost over the default *(inferred)*,
and smooths the way by going straight on from each point to the
furthest the straight line reaches for no more than the way there
costs *(inferred)*. That the black hole's and wormhole's sizes are
their models' is *(guessed)*: the program's fields for them aren't
named. Reluva marks an asteroid field's squares over its whole
`fieldRadius`, not along a belt's line.

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
  - **the way,** set as a ship's sent, by the planner below
    (`Movement::plan`) over the map's terrain (`Movement::Terrain`), cells
    of 50 rather than the program's quadtree, which comes to the same
    cells at its finest; each leg of it then goes round each standing
    unit it would come too near, nearest first, on the side away from it;
  - **as it flies,** each moving ship looks 1.4 seconds ahead
    (`COLLISION_LOOK_AHEAD_TIME`) for any other unit it would come within
    1.2 times their sizes together of (`COLLISION_SAFETY_MARGIN`, their
    rings' half sides), and is pushed away from where it would be by how
    far inside it comes, the push 0.7 last frame's and 0.3 this one's
    (`OLD_AVOIDANCE_FORCE_WEIGHT`), at its speed, or 10 a second at a
    stop *(guessed)*: how the push moves it isn't known. It's pushed up
    and down by `Y_REPULSION` (0.5) of it, going no higher once above the
    ceiling nor lower once below the floor, as the game's steering in a
    fight, and comes back to the plane at a fifth of its pace
    *(guessed)*;
  - **the map's edge:** ships and where they're sent kept 100 inside it
    (`MAP_EDGE_BUFFER`).

  `engine-shot` sent both construction ships to one point: they came to
  it side by side, apart.

## Sources and checks

- **Where the facts come from:** the original's draws every tenth of a
  second over two flights; `cnstphys.odf`; the model's box from
  `fconst.sod`; the program's code, for height and moving in a fight.
- **How it was checked:** our flight, shot by `engine-shot` every second
  from the right click, set beside the original's frames at the same
  times: the way round the starbase, the turn and the stop match. The
  unit tests (`storm3d.unit.play`) check the straight flight's times and
  speeds, the way round, the click's point and a group's goals, and a
  ship pushed up or down kept within the floor and ceiling.

## Open questions

- How the original's collision avoidance moves a ship by its push; what
  a step of its A* costs, and how its rubber banding smooths the way;
  the fields its black holes' and wormholes' squares are sized by, and
  the line an asteroid belt's squares follow.
- How the program steers: its `turnRadius`, `pathLeadDistance`,
  inertias and coefficients, and its path finding round more than one
  obstacle, or round other ships.
- How fast a ship pushed off the plane comes back to it, and whether it
  climbs away from others out of a fight too.
- What the parts a centre of combat gives its ships do, but for part 7's
  circling, and which ships take 7; none of it is made.
- Which ship of a group goes to the point, and how a group further than
  100 apart, or of ships of different speeds, moves.
- Whether the original backs up so, and how its other physics files'
  ships fly (only the construction ship's flights were measured).
