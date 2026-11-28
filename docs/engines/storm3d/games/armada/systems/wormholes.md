# Star Trek: Armada: wormholes

> **Status:** partial · **Code:** `src/engines/storm3d/play/wormholes.cpp` · **Checked against:** Federation 1's Premonition and Borg brought through its temporal gates in Reluva · **Confidence:** from the code unless marked

## Overview

A wormhole (`classLabel = "wormhole_obj"`: the maps' `mwrmhole`,
Federation 1's temporal gates `mprmgate`) carries ships sent into it out
of its partner, anywhere on the map. Wormholes pair as they're made,
open for a ship in their queue, and close when no ship is left.

## How it works

### The definition

The class reads its keys at 0x44aaf0; the defaults, for one that gives
none: `AnimationTime` 2, `ClosedScale` 10, `OpenScale` 50,
`SpeedEffectRange` 50, `MaxSpeedFactor` 4, `ScaleEffectRange` 50,
`MaxScaleFactor` 2.5, and `openSound` and `closeSound`. Its scale grows
or shrinks by (OpenScale − ClosedScale) / AnimationTime a second
(0x44ac14). `AnimationRadius`, `TransportRadius`, `DisableTimer` and
`Rotation` are read and kept, but nothing was found that uses them
*(inferred)*, no code reading them being found: the disable timer starts at 0 and is
only counted down, so it never holds one shut. Both definitions in the
install open from 0.7 to 2.5 in 2 seconds.

### Pairing

- **As they're made** (0x4511f0): one global holds the wormhole waiting
  for a partner. A new one pairs with it, each pointing at the other,
  and the slot empties; with none waiting, the new one waits. So the
  first pairs with the second, the third with the fourth. Federation 1
  builds its two gates one after the other and they pair so.
- **The maps** keep each wormhole's partner too; on the 22 maps checked
  it's the wormhole next to it in the file, as making them in order
  pairs them *(inferred)*.
- **`LinkWormHoles(a, b)`** (0x4516a0) makes each the other's partner;
  their old partners keep their links one way.
- **One destroyed** (0x4516f0): its partner has none, and waits for the
  next made if none is waiting already.

### Going through

- **The order:** a move order to a wormhole, or to a point within 5 of
  the nearest (0x451830), is an order through it (0x425b7e); a path the
  planner routes through a wormhole's link (cost 500) is too. Only
  ships go; a ship passing near without such an order isn't taken
  *(inferred)*.
- **Joining its queue:** the queue's point is 100 from the wormhole's
  middle towards the ship, or where the ship is if it's within 100
  (0x54dae0). When the move ends within the ship's `tooCloseToTurn` of
  it, across the plane, the ship joins the queue (0x426bc0); otherwise
  it flies to the point. In the queue, the ship is "in queue" to the
  scripts' `InQueue` (0x434e90).
- **Opening:** a closed wormhole with a partner and a ship in its queue
  plays `openSound` and opens, with its partner (0x451960); its scale
  grows to `OpenScale`. Ships don't wait for it.
- **In:** along a Hermite curve from the ship to the middle, at first
  along its heading (tangent its heading × 0.3 d), at the end straight
  down (0, −0.42 d, 0) (0x54d2f0). Its speed is its class's top speed
  times MaxSpeedFactor + d² (1 − MaxSpeedFactor) / R² within
  `SpeedEffectRange` R of the middle, so 4 times as fast at the middle
  (0x54df84). Within `ScaleEffectRange` it's stretched along its length
  by `MaxScaleFactor` at the middle the same way (0x54dfd2). An order
  given meanwhile is held until it's out.
- **Through:** at the curve's end the ship is put at the partner's
  middle, facing as it was, with no wait (0x54dc10).
- **Out:** along a curve 100 ahead of it across the plane, tangents its
  heading × 45 at each end, at the same speeds (0x54d3f0). At its end the
  ship carries out the order it held, or stops; it's out of the queue
  (0x54dcf0).
- **Closing:** an open wormhole whose queues and its partner's are all
  empty plays `closeSound` and closes, with its partner (0x451a80).

## In Reluva

`MissionView` keeps the wormholes (`play/wormholes.cpp`), each a unit
(Federation 1's gates, built by its script) or a map object, paired as
above. A script's `Goto` to one, or the player's right click on one,
sends the ships through; `InQueue` is whether a ship is going in or
coming out; `LinkWormHoles` re-pairs. A gate's model is drawn at its
scale as it opens and closes. Wormholes aren't fought: no unit takes
one as its target, and the computer players don't count them
*(inferred)*, as the program's wormhole is neither a craft nor a building.

Not yet: a ship's stretch (`MaxScaleFactor`; Federation 1's gates have
none), the path planner routing through a wormhole's link, and
`EnableWormholeTravel`, which gives the planner a player's leave to.

## Sources and checks

- Armada.exe's code at the addresses given, read in the disassembly.
- The definitions `mwrmhole.odf` and `mprmgate.odf`, and Federation 1's
  scripts' imports and calls.
- Federation 1 run in Reluva: the Premonition built by the entry gate,
  sent through, comes out by the exit gate, and the script's next rule
  (the Premonition within 400 of the exit and not in the queue) fires.

## Open questions

- What a ship sent into a wormhole without a partner does; Reluva's
  stops at its middle, a choice of Reluva's own.
- What `Rotation` and `AnimationRadius` do, if anything; the model's own
  looping animation turns it.
