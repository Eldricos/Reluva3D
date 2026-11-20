# The Fallen: movement and physics

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_physics.cpp`, `game/collision.cpp` · **Checked against:** the code (Engine.dll), and every map run · **Confidence:** from the code unless marked

## Overview

How actors move through the level and each other: moves, placing,
the physics modes (walking, falling, swimming, flying, The Fallen's own),
and the AI's latent moves. Climbing ladders and ledges is in
[climbing.md](climbing.md).

## How it works

Actors move through the level's BSP ([collision.md](collision.md))
and each other (`vm_physics.cpp`), as Engine.dll's `ULevel::MoveActor`
and the actors' `phys*` functions do it (read for what they do, written
here anew). The Fallen's ledge and ladder tests (`TestForLedge`,
`CheckClimbing` and their helpers) are read from it the same way
([climbing.md](climbing.md)).

- **A move** (`MoveActor`) sweeps the actor's box 2 units further than
  asked, through the world if it has `bCollideWorld`, then against the
  cylinders of actors that block it (`bBlockActors`, `bBlockPlayers` for
  players, not its owner or what it owns) and movers. Hitting something,
  it stops 2 units back along its way (not off the wall), and doesn't
  move if that's no further than 2. A mover is collided with through its
  brush's own BSP (UnrealEd builds one for each mover), with the move
  turned into the brush's coordinates where the mover is now. A mover
  without one is a box. Traces count movers as the world, so a pawn
  stands on a lift.
- **After a move**, in the engine's order:
  - if an actor stopped it, `Bump2` (hit location and normal) to that
    actor and then to the mover, then `Bump` likewise;
  - what it now touches (`Touch` and `UnTouch` both ways, the `Touching`
    list);
  - last, zone changes (`ActorLeaving` to the old zone, `ZoneChange`,
    `ActorEntered` to the new).
- **HitWall** goes through the engine's `processHitWall`:
  - none for a pawn running into a pawn;
  - for a pawn, only while it pushes (some acceleration) and the wall
    faces back along its way to its `Destination` (dot under
    `MinHitWall`; level, when walking);
  - a pawn whose script has no `HitWall` of its own just gives up its
    move (`MoveTimer` −1, `bFromWall`), unless falling.
- **Smooth moves** (`MoveSmooth`): the move, then along what stopped it,
  then, told `HitWall` only now, along both walls (`TwoWallAdjust`: the
  crease between them if they face apart). Three moves at most, never a
  step.
- **Placing** (`SetLocation`, and spawning an actor that has
  `bCollideWorld` or `bCollideWhenPlacing`) finds a spot as Engine.dll's
  `ULevel::FindSpot` and `AdjustSpot` do. A spot whose point is in the
  world's solid is refused. If the actor's box fits, it stays. Otherwise
  six tries in turn (down, then along X and Y, then up and the other
  ways), and then towards the eight corners. Each try draws a line from
  the spot a little past the box's edge that way (its extent + 2; to the
  corner itself for the corners). If the world or a mover is in the way,
  the spot is pushed back off it along the face's normal by (1.05 −
  how far along it was) × that extent (the corner's distance + 2), and
  kept if the box fits there. The pushes add up, so a later try starts
  where the last one left it. Then, if all that moved it less than
  √1.5 × the extent's length and it fits, it stays there. Else it is
  raised from the spot 20, 40 and 60 units.
  As SpawnActor does, an actor that fits nowhere isn't spawned (`Spawn`
  returns None, and the log says so). Over all maps' first 3 seconds,
  that's 4 times: a Cardassian hypospray 60 units wide where nothing that
  wide fits, and the like. Before, it was left at the world's origin.
  Starts are placed for the
  default pawn height (40), and The Fallen's characters are taller (Worf
  is 52), so a player starting on a lift begins a little into its floor
  and is lifted onto it.

Physics each tick, after the actor's scripts:

| Physics | What it does |
|---|---|
| Walking, the player | `calcVelocity` (below), then one move along the floor's slope (`FloorNormal`) with up to three slides. Each wall is told `HitWall` as The Fallen's player's walking state reads it: X how head-on (the move's direction against the normal), Y its flat speed, Z how fast it slides on. Head-on (under −0.8), it stops. A ledge or ladder there is tried first ([climbing.md](climbing.md)); then a step up (in Engine.dll, `TestForLedge`'s small-step branch, here the walk's own up, across and down). Then down onto the floor for real, `MaxStepHeight` + 2, sliding down a floor steeper than 0.65. No floor: back up, `Falling`, then falling. Its velocity is then what it really moved. |
| Walking, other pawns | `calcVelocity`, then in steps of at most 0.05 s when going far, up to 4 (8 for pawns not The Fallen's). What stops a move is stepped up (`stepUp`: up `MaxStepHeight`, across, down; a square wall tried as a step again, else slid along; landing on a slope steeper than 0.7 undone). It's kept 1.9 to 2.4 above the floor (0.7 or flatter). With no floor, a pawn that can't jump (`bCanJump`) or is walking (`bIsWalking`) goes back, stops, and gives its move up (`MoveTimer` −1). Others fall with the rest of the tick. Velocity is what it moved, flat. A CTVE pawn's rise or drop on a step is eased out of how it's drawn (`PivotOffset`, into `PrePivot`, at 10 a second). |
| Falling, the player | Gravity 1.4 times the zone's, times 1 − 2.2·dt (kept to 0.5-1), `AirControl` of `AccelRate` across, up to the terminal velocity. One move with up to four slides, `HitWall` (head-on-ness, speed) at the first only. Landing on anything flatter than 0.65. Its velocity is then what it moved. |
| Falling, everything else | Steps of at most 0.1 s (8 at most), each moving at the average of its velocity at the start and end, split at the top of an arc. A pawn's air control is less with something just ahead. Landing on more than 0.7 (`Landed`, then walking, or still for an actor, and the rest of the tick walked). Steeper: `HitWall` and along it, then along both. `bBounce`: `HitWall` and its script bounces it. |
| Swimming, flying | A pawn's only: `calcVelocity` in the fluid (`WaterSpeed` or `AirSpeed`, the zone's fluid friction; swimmers float by `Buoyancy` against `Mass`, and don't brake). A flier: one move, stepping up a low wall, else sliding. An AI swimmer (APawn::physSwimming, `Swim`): the move with the zone's current in up to three goes; out of the water, back to the water line (`findWaterLine`, by halving) or back where it was; head on into a wall (`MinHitWall`), `HitWall` and stop, else along it; its velocity what it moved. It never leaves the water itself: its scripts' `ZoneChange` do that. The player's is The Fallen's own (ACTVE_PlayerPawn::physSwimming, 0x10371a60), below. |
| Projectile | Its velocity plus acceleration; `HitWall` on the world. |
| Rotating | `RotationRate` each second, or toward `DesiredRotation` with `bRotateToDesired`. Only in this physics (and The Fallen's single-axis ones) does an actor turn, after its move. An AI pawn turns after its move (APawn::physicsRotation; not climbing walls; Newtonian ones as actors do): set to turn to `DesiredRotation`, its yaw at `RotationRate.Yaw` a second, its pitch the desired's within `RotationRate.Pitch` of level, and with a `RotationRate.Roll` a lean into turns at speed (the sideways change of its velocity, 4096 walking or 28000 else over `AccelRate`, at most the roll rate, eased in at 5 a second, back level at 8). Then its `MoveTimer` counts down. |
| Moving brush | While its script has it going (`InterpolateTo` sets `bInterpolating`): a mover from its old key toward its next by `PhysAlpha`, eased in and out if it glides by time. It carries the actors based on it and sends `InterpolateEnd` at the end. |

**`calcVelocity`**, the velocity towards the acceleration:
- **Acceleration:** only capped at `AccelRate`, at 30% of it walking or
  crouching.
- **Steering:** the velocity is turned towards the acceleration by the
  friction times dt.
- **Braking,** with no acceleration:
  - the player in one go, stopped under a speed of about 2.2;
  - the AI in steps of 0.03 s, stopped under 10.
- **Top speed:** `GroundSpeed`, `WaterSpeed` or `AirSpeed`, eased down to
  30% when walking or crouching.
  - The player's is ×0.5 strafing while walking or crouching (else ×0.8),
    ×0.8 backing, ×0.75 crawling.
  - Others' are times `DesiredSpeed`, which the player's isn't. Before,
    Reluva held the player to `DesiredSpeed` too, a third of its run.

The Fallen added its own modes (past 10): the player's climbing,
free flight, fish, interpolation and one for each kind of effect.
`vm_climbing.cpp` and `vm_effect_physics.cpp` work them. The player's
three (Climbing, Hanging, Clambering) are as Engine.dll's
`ACTVE_PlayerPawn::physClimbing`, `physHanging` and `physClambering`.
The rest are each a function of Engine.dll's (`AActor::performPhysics`,
0x10358a90, switches on the mode), all read: Interpolating, Trailer,
Particle, Newtonian, Fish, the single-axis turns, and each effect's
([effects.md](effects.md)).

A pawn moves only in the modes its own class's switch has: The Fallen's
player (0x10368c80) walking, falling, swimming, flying, on a path
(Interpolating, CTVE_Interpolating) and climbing, hanging or clambering;
The Fallen's other pawns (0x10363310) walking, falling, swimming,
flying, on a path, Spider and Newtonian; a plain pawn (0x10361a70)
those and SimpleWalking and Fish. In any other mode a pawn stays where
it is.

| Physics | What it does |
|---|---|
| Interpolating, CTVE_Interpolating | Along a path of interpolation points (`SpecialEvent`'s player paths, the intro's camera), as physPathing and CTVE_physPathing: from `Target` to its `Next` by `PhysAlpha`, at `PhysRate` times the two points' `RateModifier` blended by the way along (backward with a negative rate). With a point before and after, the place is a spline through the four (weights x^4/16 - x^2/2 + 1 at t+1, t, t-1, t-2, summing to 1), as is the turn (Interpolating: the rotators' numbers as they are; CTVE: slerped between the two); else straight. Past a point: `InterpolateEnd` on the point, then the actor, then on to the next (or the one before), past those with `bSkipNextPath`, with the time left over. A pawn's `ViewRotation` follows; the velocity is what it moved. Each tick the points blended by the way along set the level's `TimeDilation` (`GameSpeedModifier`), and for a player its `FovAngle` (its class's, times `FovModifier`) and screen flash (`FlashScale`, `DesiredFlashScale` from `ScreenFlashScale`; `FlashFog`, `DesiredFlashFog` from `ScreenFlashFog`). |
| Trailer | Through everything. A sprite: at its owner's place plus its `PrePivot` (`bTrailerPrePivot`), or its `Mass` behind the owner along its facing (`bTrailerSameRotation`), or the place. Anything else: at the place, turned as the owner (`bTrailerSameRotation`) or facing away from where the owner's going (straight up when it's still). |
| Climbing | By its acceleration (the scripts': `aForward` x 0.02 up the view's Z, 120 a second), while a ladder is 1.6 radii ahead at the head going up (4 under it) or the feet going down (4 over them). Onto a floor: `Landed` (the scripts walk). At the top with no ladder at the head: 4 units on, and if that's blocked by a ledge (`TestForLedge`), `ChangePhysics(Clambering)`. At the bottom with none at the feet: `ChangePhysics(Falling)` (the scripts let go). Its velocity is what it moved. |
| Hanging | Along the ledge by its acceleration (`aStrafe` x 0.008): a hand's box (`HandCollisionHeight`) swept over the ledge, moved only where there's a hand hold. No ledge under the hands (`TestForHangLedge`): the scripts' `Falling`. |
| Clambering | Up at `DodgeClickTimer` units a second (its scripts' `PlayClambering` sets 100 or 130; Kira's 104 or 130) until the feet are over `LedgeHeight`, feeling 4 units forward; clear there (or 4 over), `Landed` with the normal of what it felt (none if clear), which sets the rise to 0. Then forward at 120 a second until stopped, when `ChangePhysics(Walking)`; its scripts' `AnimEnd` walks too. Blocked rising: `ChangePhysics(Falling)`. |
| RotatingYaw, RotatingPitch | Turning about the one axis by `RotationRate` a second, only with `bFixedRotationDir` (round and round) or `bRotateToDesired` (the shorter way to `DesiredRotation`, then `EndedRotation`). Before, Reluva turned them twice, so ARD_TortureMachine turned at double speed and never backward. |
| SimpleWalking (a plain pawn's: the leeches) | As APawn::physSimpleWalking: forward the way it faces at `GroundSpeed` (its acceleration has no say; `AccelRate` held at 300), along the floor found 10 over and under where that takes it, 2 above it. Blocked going down, the plain move; level or up, 60 a second up then across; no sliding. No floor ahead, or none 10 under it after: `Falling`. On a slope steeper than 0.6: `HitWall`. The Fallen's own pawns don't move in it. Spider (11): no script uses it. |
| Particle | As physParticle (0x1035d430): no gravity; its velocity changed by its acceleration (a ship lifting off, debris settled), and, if a `CTVE_Generator` owns it, slowed by its `DeltaSpeed` and pushed by its `Forces` and wind too; moved through everything, its zone not looked at, with no events. In its last second its `DrawScale` and `ScaleGlow` fade with its `LifeSpan` (as the generator's `bFadeSize`, `bFadeGlow` say; both without one). One that makes copies stays where its scripts put it. |
| Newtonian | Free flight by momentum. A pawn (the Vulture, the Wraith, the Arachnid): `calcVelocity` with no friction (its velocity steered toward its acceleration, at most `AccelRate`, up to `AirSpeed` times `DesiredSpeed`; a player never moves in it); an actor (a thrown drum) keeps its velocity, which only its scripts change. On a wall a pawn steps up a low one; else the wall is told (`processHitWall`) and it slides, a second wall told and the move bent along both. A pawn's velocity is then what it moved. |
| Fish | As an AI swimmer (kept in the water), then pulled on toward its `Destination` when that's over 20 away (`AccelRate`; the pull also kept in `LastSeenPos`), held to `WaterSpeed` and facing the way it swims with no roll; its velocity not what it moved. |
| EXP_Trail | An explosion's trail: by its velocity and acceleration, through everything; then curling (its copies' base: `Angle` the torque, `Acceleration` the axis its scripts change), the axis crossed with the way it goes by dt x Angle x 2500, kept to its speed. |
| The rest | Only their copies move (see [effects.md](effects.md)). |

**The player swimming** is The Fallen's own (`ACTVE_PlayerPawn::physSwimming`,
0x10371a60), not the engine's pawns':
- The chest (`ChestHeight` of its height) is kept in the water
  (`AdjustToWaterSurface`, 0x10371240). Out of it after a move, the
  player is moved back down to the water line and 0.1 under: along the
  move if it started under the line, else straight down, sliding along a
  wall in up to three goes. If the chest is still dry, the player is told
  `Landed((0,0,1))` and its scripts take it from there (on a shore it
  stands and walks). In the water, the surface over it is measured: the
  water line up to 100 over its height (the player's `findWaterLine`,
  0x10370f70, keeps it).
- Near the surface (the chest under it by less than 100) the upward
  acceleration is held to the depth, so it slows as it gets there; within
  1 the player is at the surface: it doesn't rise out (an upward move's
  Z is dropped unless against a slope), and its Velocity's Z is 0.
- The velocity is the player's own `calcVelocity` with `WaterSpeed` and
  the friction of the zone its chest is in; no buoyancy, no gravity.
- Up to three moves, while over 0.01 of the tick is left, with the
  zone's current: sliding along a wall, then `TwoWallAdjust`; at the
  surface against a slope (its normal's Z between 0 and 0.65) the surface
  is the second wall, a ceiling. At the surface a wall of the level can
  be a ledge to clamber onto (`TestForLedge`, then `ChangePhysics(13)`)
  or a ladder (`CheckClimbing`, then `ChangePhysics(14)`), as in
  [climbing.md](climbing.md). No `HitWall`.
- Its Velocity is what it swims at, not what it moved.

So the player stays in the water until it swims to the surface and onto
a ledge, a ladder or a shore. On M01_KiraL1B from (-1840, -5572), the
bottom: Space takes it up 30 to the surface, where it floats; swimming on
west, it reaches the shore at -1938 and stands. A ledge its scripts
won't clamber onto, or a step no higher than `MaxStepHeight` + 1 that
`TestForLedge` steps the player up onto (up at least 1 and on by the
move, again higher if stopped on a slope, put back if it can't go on;
0x1036badd), are followed by a step down of `MaxStepHeight` + 2: with
the chest then out of the water, the player lands and walks
(0x10371fa7). No swim in The Fallen's maps meets such a step: every wall
`unreal ledges` lists to swim into rises over 40.

Walls are told as the engine's `processHitWall`: nothing for a pawn
hit, or with no acceleration; a pawn going (toward its `Destination`;
a CTVE pawn, its acceleration) into it more head on than `MinHitWall`
(walking, both flattened, the event given the flat normal), else its
`MoveTimer` ends (`bFromWall`) if it has no `HitWall` of its own. A
smooth move's second wall is told as it is.

**Out of the world** (zone 0: the BSP's last node's zone for the side
the point is on, as `UModel::PointRegion` has it), checked before each
move as Engine.dll's physics does: an AI pawn walking, or anything but
The Fallen's player falling, is told `FellOutOfWorld` (an actor's script
destroys it, a pawn's dies, `'Fell'`); a pawn flying or Newtonian that
collides with the world is destroyed ("flew out of the world"), a
player's only doesn't move; any other Newtonian actor colliding with the
world is destroyed, and a projectile whatever. The Fallen's player has
walking and falling of its own (`ACTVE_PlayerPawn::physWalking`,
`physFalling`), which don't look at the zone.

That matters on M07_WorfL1: its player is a pawn saved in the map
(`DS9_WorfBeaten0`, which `GameInfo.Login` takes up, as it does any
player pawn with no player) parked inside the wall at (928, 531, 1059)
(the editor's own saved `Region` there is zone 0, no leaf) while the
opening plays with a stand-in (`CIN_WorfBeaten`): the camera chain, the
Jem'Hadar's lines (the last, KAR's "Leave him here...", sends
`GCM07_JemExit`), the chain that starts, whose commander sends
`GCM07_WorfGetup`, and that chain's last commander, at its end, sends
`WorfPlaystart`: a one-point spline (`CTVE_Spline0`, its `MoveActorTag`
the pawn's tag, which the pawn hooks itself to in `PreBeginPlay`) puts
the player at the start, (−2.5, 582, 1059), about 110 seconds in. From
there Worf walks.

The input axes are 20 times the bindings' speeds (`aBaseY` 6000 for a
held MoveForward), as UE1's input scales a held key's axis. The
climbing speeds above are the scripts' factors of those.
Natives: `Pawn.StopWaiting` (a `Sleep` its state code is in ends),
`Move`, `MoveSmooth`, `SetLocation`, `SetPhysics`, `Trace`
(the world and actors' cylinders), `FastTrace`, `PointCheck`, `Pawn`'s
latent `MoveTo`, `MoveToward`, `TurnTo`, `TurnToward`, `WaitForLanding`
and `FinishRotation` (each moves the pawn by its acceleration until it
gets there or gives up, then its state code carries on), and
`pointReachable` / `actorReachable` (a box swept there), and The
Fallen's `MoveTo2` (taken as `MoveTo`), `StrafeTo` and `StrafeFacing`
(there, facing a place or an actor all the way).

The loops over every actor (blocking, touching, what stands on a mover)
read each class's collision properties from where they are in its
values, found once; looking them up by name took a quarter of a busy
level's time.

## In Reluva

`vm/vm_physics.cpp` moves the actors, through the level's BSP as
[collision.md](collision.md) describes; The Fallen's own modes are in
`vm/vm_climbing.cpp` and `vm/vm_effect_physics.cpp`.

## Sources and checks

- **Where the facts come from:** Engine.dll's `ULevel::MoveActor` and the
  actors' `phys*` functions, read for what they do. The Fallen's modes
  other than the player's three are Reluva's reading of what the scripts
  set and leave undone *(inferred)*.
- **How it was checked:** `reluva-tool unreal run` over all 74 maps
  ([level.md](level.md)), and by hand with `reluva-tool unreal play`.
