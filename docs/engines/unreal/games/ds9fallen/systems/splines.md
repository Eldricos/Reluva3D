# The Fallen: splines

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_splines.cpp` · **Checked against:** the code (Engine.dll) and the cut scenes against a recording · **Confidence:** from the code unless marked

## Overview

Cut scenes move their cameras and actors along splines.

## How it works

The Fallen moves its cameras and its actors in cut scenes along splines.
These are chains of `CTVE_Spline` nodes; `CTVE_Director` is a camera's,
`CTVE_SplineAnim` an actor's. Their scripts ask the native
`GetSplineData` each tick where along the chain they are at a time
(`vm_splines.cpp`), which Engine.dll answers in `ACTVE_Spline::GetData`
and `ComputeRotation`; Reluva does as those do. UnrealEd saves with each
node the `Length` and `Duration` of the segment after it, its `Tangent`
(on The Fallen's, the head's is exactly half the way to the next node,
Catmull-Rom's) and `AveSpeed`, `Length` over the travel time.

- **Timing.** From the node it's asked of, each node's `Duration` is
  counted off in turn along `Next` (round again on a closed chain) until
  the time falls within one, or the chain ends (then at its last node).
  A node first waits its `Pause`; the rest of its `Duration` is travel.
  Travelling, the distance covered after t seconds is `Speed` · t +
  (next `Speed` − `Speed`) · `AveSpeed` · t² / (2 · `Length`), the speed
  changing evenly from this node's to the next's, and `TimeAlpha` is
  that over `Length`, stretched by t / (t + `ExtraDuration` · that); if
  `AveSpeed` is 0 or the node's own `Speed`, it's the time over the
  travel time. Kept to 0-1. (CM10_SWK_L3's Sisko waits 8 seconds at a
  node whose duration is 10.1, its 178 units at 85 the other 2.1.)
  `TimeLine` and `MaxTime`, also saved, aren't used.
- **The path.** The cubic through the two nodes, leaving and arriving
  along their tangents, at `TimeAlpha` itself (not spaced out by
  length), or a straight line with `Interpolation` linear. With `Focus`
  actor follow, the node isn't on its path but at the actor tagged
  `FocusActorTag`, offset by `FocusOffset` turned with it and
  `FocusFollowOffset` (turned too with `bFocusFollowLocal`).
- **Facing**, by the node's `Focus`:
  - Direction (the class's default): the node's own facing (the arrow
    UnrealEd draws on it).
  - Spline: along the curve, towards the point a hundredth further on
    (from one a hundredth back, past halfway).
  - Actor and actor follow: at the actor tagged `FocusActorTag` (kept in
    `FocalActor` while it's still so tagged), offset by `FocusOffset`
    turned with it, and its `PrePivot`; along the curve if there's none.
  - World: at the point `FocusOffset`. Orient: the way `FocusOffset`
    points.
  - Rolled by `RollDegrees`, changing evenly to the next node's.
  - If the next node has `bSmoothRotate` (the default) it turns, by
    `TimeAlpha`, into the next node's facing at its start: the two as
    quaternions, slerped (C3DInterpolateRotationX, the same as
    `Actor.InterpolateRotation`). Not between two spline-focused nodes,
    nor two looking at the same actor.
  - Then the first node's (the one asked) `LocalRotation` is added.
- `TimeAlpha` is also what directors take the field of view between
  their nodes by.

On M01_KiraL1A this plays the opening as written: cameras glide through
the monastery while Kira and the monks walk their paths. After about 80
seconds the letterbox goes and she can be moved.

## In Reluva

`vm/vm_splines.cpp` answers `GetSplineData`.

## Sources and checks

- **Where the facts come from:** Engine.dll's `ACTVE_Spline::GetData`
  and `ComputeRotation`, read for what they do.
- **How it was checked:** the cut scenes' cameras against a recording
  ([research/playthrough.md](../research/playthrough.md)).
