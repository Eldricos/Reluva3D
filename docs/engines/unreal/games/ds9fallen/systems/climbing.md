# The Fallen: climbing, hanging and ducking

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_climbing.cpp` · **Checked against:** the code (Engine.dll), and places in the levels tried with `reluva-tool unreal play` · **Confidence:** from the code unless marked

## Overview

The Fallen's player climbs ladders, hangs from ledges and pulls up onto
them, ducks under low gaps and climbs out of water. The physics modes
(Climbing, Hanging, Clambering) are in [movement.md](movement.md); this
page is how the game finds a ledge or a ladder.

## How it works

**Finding a ledge or a ladder**, as Engine.dll does. Each search turns
the player to face into the wall before asking its scripts'
`ChangePhysics`, and puts the rotation back if they refuse.
- **A walk blocked by anything** (with the move left over), as
  Engine.dll's `physWalking` asks `TestForLedge` (0x1036b1c0) after every
  go of the move:
  - **The search** (`TestStepUp`, 0x10369b50): up, on by twice the
    class's `CollisionRadius`, and down onto a floor. Head on to an
    upright wall (the move and the view both within -0.85 of it), not
    crouching, and not in a level with `bNoActionLevel` unless climbing,
    it reaches up to twice `ArmCollisionHeight` + `StandCollisionHeight`
    with a box of `StandCollisionHeight`; else up to `MaxStepHeight` with
    the player's own. Standing and walking, the box is the ducked one,
    from the feet.
    - Against the level's wall it climbs only to the top of that wall's
      own polygon (`GetNodeHighPoint`, the polygon in the wall's plane
      that the box is on: `TestHitIntegrity`) when that's within reach,
      else all the way, failing if something is over it. So a step under
      a low lintel is found (M04_KiraL1's stairs down from the start).
    - On meeting another wall at once, it climbs again from there.
    - The floor found must be 0.65 or flatter (steeper only when it
      climbed to a polygon's top and has got on a tenth of the way), and
      the move must have gone 3 units or 0.99 of the way.
    - What it tells the scripts: `LedgeHeight` (the feet on it),
      `LedgeNormal` (its floor's normal, not the wall's), `LedgeDepth`
      (how far on, as a share of that), `HeadRoom` (the room over it, as
      a half-height up to `StandCollisionHeight`) and `LedgeItem`.
  - **A ledge:** higher than a step, head on as above, and deep enough
    (over 1.6 radii if more than 45 high, else 0.5), flat (0.9), with
    floor just past the wall's face (`TestForStrongLedgeFooting`), and
    with the wall and floor under the hands 8 units to either side
    (`TestForHandHold`, which makes it the player's Base). Then
    `ChangePhysics(Clambering)`: the walk ends if the scripts take it.
  - **A step** (no higher than `MaxStepHeight` + 1, at least 0.4 of the
    class's radius deep, a third of a unit's share or more, with room
    over it for the player's height): the player is moved up onto it (at
    least 1) and on by the move (at least 3 across); stopped at once on
    a slope, up `MaxStepHeight` + 1 instead; put back if it can't go on. What
    share of the move that took is done, and the walk goes on. There is
    no other step up for the player.
  - **Else, on the level's own walls, a ladder** (`CheckClimbing`): a
    ladder texture (`TT_Ladder`) at the chest and the head, 1.6 radii
    into the wall. The player is put
    against it, 2 units off. Then `ChangePhysics(Climbing)`.
- **In a jump** (only the scripts' reaching jump, with `CollisionHeight`
  at `StandCollisionHeight` + `ArmCollisionHeight`), after each falling
  move toward a wall 6 units ahead at the chest:
  - **A ladder there:** `Climbing`.
  - **Else a ledge for the hands:** found with the hands' box and a
    search of twice `ArmCollisionHeight` + `HandCollisionHeight`, at
    least 4 deep and flat. There must be room to get there, a hang
    ledge over it (`TestForHangLedge`) and a hand hold. Then `Hanging`,
    and the player is put there with the box's top at the ledge.
- **Ducking under** (`TestForLedge`'s other branches, `ForceDuck`): the
  player is standing, not ducking yet, and walking. The ducked box (from
  the feet, `DuckCollisionHeight` + 1 high) can go on where the standing
  one is stopped. Pushing on for over 0.3 s with no step there, the player
  ducks under it. The same happens at a step with less room over it than
  the player's height (and not crouching). Either counts as the move
  done (all of it, or the step's share), and the walk goes on.
  - `PushToAutoDuckTimer` grows by each walking frame's time, as
    Engine.dll's `physWalking` adds it. It goes back to 0 on a fall or a
    real move (over 10 a second). Reluva had only ever set it to 0.
  - Ducking sets `CollisionHeightOverride` (to the standing height, if
    unset), `ForceDuckLocation` (the place, its feet at the ducked
    height) and `bIsCrouching`. The scripts' walking state then shrinks
    the player and keeps it down until it has moved 20 away.
- **Ladders' edges:** put against a ladder, the player is kept 12 units
  inside its polygon's left and right edges (`GetNodeXOffsets`).
- **Seen on the stairs:** on M04_KiraL1, walking on from the start goes
  over the lip at the top of the stairs under the doorway (head on, so
  probed as a ledge; the lip's polygon top is in reach) and down them.
  Climbing all the way up first, the box met the doorway's lintel and
  found nothing.
- **Swimming** into a wall tries a ledge and a ladder as walking does
  (`physSwimming` asks the same tests).
- **The hit's node** (`TestHitIntegrity`, 0x10369610): Reluva's
  collision gives each hit its BSP plane's own normal and a node in that
  plane; of the polygons sharing the plane, the one the box is on is
  taken, as the engine's test does (its reach along the wall widened
  toward the box's corners on a slanted wall, and 0.1 more).
- **Seen:**
  - **The ladder:** on M01_KiraL2, walking into the ladder under
    `CTVE_LadderTop2` climbed it at 120 a second, pulled up over the top
    and walked on. Tried again in November 2026 from the floor at its
    foot (-704, 4049), it isn't climbed, with this walk or the one
    before it: the line ahead from the chest starts inside solid space
    (a semisolid brush, Brush1274, is there), so no ladder texture is
    found. Not yet looked into.
  - **Pulling up:** on M01_SiskoL1A, walking into the ledges at (-362,
    2981) and (-316, 3222), 56 and 60 up, goes through `PlayerClambering`
    onto them.
  - **Refused, as the tests say:** other places found there are walls
    that go on up past reach, or tops with nothing for the hands at
    their edge (`TestForHandHold` finds no wall 2 under the top: the face
    is set back up there).
  - **Hanging:** on M01_KiraL2 at (351, 3516), jumping at a ledge 122 up
    while running at it catches it (`PlayerHanging`, `LedgeHang`). She
    stays when let be, and shuffles along it with the strafe keys
    (`LedgeR`, 48 a second). Forward climbs up after half a second
    (`ledgeu`) and walks on 104 higher; back lets go. Held forward
    through the catch, she pulls up at once, as the hanging state's
    script asks.
  - **Ducking under:** on M03_SiskoL2 at (1515, -1693), walking into a
    low gap stops the player for 0.3 s, then the player crouches
    (`CrchWalkF`, height 32.5) and goes on under it until the ceiling
    slopes too low even for that. Walking back out, the player stands up
    again.
  - **Out of water:** swimming at the surface into a rim over the
    water, `TestForLedge` finds it and `PlayerClambering` pulls up onto
    it. The player has to swim up to the surface first: it doesn't float
    ([movement.md](movement.md)). Into a wall with no room over
    the water (M11_SiskoL1a), the player stays swimming.
  - **A ladder's edges:** on TrainingRoom, walking into the 32-wide
    ladder at (32, 1625) 15 off its middle puts the player 4 off it
    (12 inside the edge), and 14 off the other way, 4 off that way.
    Then up at 120 a second, over the top, and on.
  - **A ladder from the water:** on M06_SiskoL1B the ladder at (2816,
    -935) stands in shallow water. Wading into it climbs it. At its top
    the player stays on it: the ladder's own top (-1118) is 2 over the
    floor behind it, so the footing test, 2 under the ledge's top, finds
    no floor (`TestForStrongLedgeFooting`), as Engine.dll's tests give.
  - `reluva-tool unreal ledges <game> <map>` lists places to try (a wall
    straight ahead, a flat top 30 to 110 over the floor to pull up onto,
    or 115 to 160 to catch in a jump), low gaps to duck under, and walls
    to swim into (with the rim's height over the water). `unreal ledges
    <game> <map> ladders` lists the ladder polygons (`TexTag`
    TT_Ladder), their width, and whether their foot is in water.
    `unreal trace` sweeps a box through a level's BSP.

## In Reluva

`vm/vm_climbing.cpp` does the tests and the modes.
`reluva-tool unreal ledges` and `reluva-tool unreal trace` (above) find
places to try.

## Sources and checks

- **Where the facts come from:** Engine.dll's `TestForLedge`,
  `CheckClimbing` and their helpers, and `ACTVE_PlayerPawn::physClimbing`,
  `physHanging` and `physClambering`, read for what they do.
- **How it was checked:** the places under "Seen" above, played with
  `reluva-tool unreal play`.
