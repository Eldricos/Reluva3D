# Jedi Knight: particle clouds (`.par`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/sprite.cpp` · **Checked on:** US retail, all 3 files · **Confidence:** confirmed unless marked

## Overview

A cloud of points, each drawn with the same material, all one size:
`sphere.par` and `sphere2.par` (100 points about a sphere, with
`00teleport.mat`) and `test.par`. They are in `Res2.gob`'s `misc\par\`,
written in the models' style ([3do.md](3do.md)). A thing's template
names one with `particle=` (`0x4cf07d`) *(from the code)*; in retail only
`+force_shield` does, with `sphere.par`: the shield of the Force power
Protection, which `force_protection.cog` makes at the player and
attaches to them. The teleport's sparkle is not one of these: it is
`+telesparks`, a cloud the game makes itself
([Clouds the game makes](#clouds-the-game-makes)).

## Layout

Line by line; a `#` starts a comment to the line's end.

| Section | Lines |
|---|---|
| `HEADER` | `PAR 1.0`; `SIZE <s>`, each point's size (0.002 to 0.01); `MATERIAL <file.mat>`; `LIGHTINGMODE <n>`, 0 in all three |
| `GEOMETRYDEF` | `RADIUS <r>`; `INSERT OFFSET <x> <y> <z>`; `VERTICES <n>` then `<i>: <x> <y> <z> <cel>` |

The last number of a point, which the files' comment calls `i`, is the
cel of the material it's drawn with: the game reads it as a whole
number, and a cel the material hasn't stops the file loading
(`0x46c4e3`, `0x46c4f6`) *(from the code)*. The files write `0.000000`,
read as cel 0. RADIUS is the cloud's size, by which the game leaves out
a cloud out of view (`0x46c78a`) *(from the code)*. INSERT OFFSET is
read and kept at bytes `0x44` to `0x4c` of the cloud (`0x46c40d`,
`0x46c430`), but nothing reads it there but the format's writer
(`0x46c66b`), which nothing in `JK.EXE` calls: not the drawing
(`0x46c750`), nor the copy a `type=particle` thing makes of a cloud
(`0x46c090`) *(from the code)*.

## Behaviour

In `JK.EXE`: drawing a cloud, `0x46c750`; the clouds the game makes,
`0x4f1c30` to `0x4f2079`.

**A cloud from a file doesn't move its points.** Each frame, every point
is taken through its thing's position and orientation, then drawn as a
square SIZE across, facing the camera, with its cel (`0x46c847`,
`0x46c876`) *(from the code)*. Nothing changes the points: the cloud
moves and turns only as its thing does. It doesn't grow: growing is
done only for things of `type=particle` ([Clouds the game
makes](#clouds-the-game-makes)), and the shield is `type=cog`.

**The shield turns by its `angvel`** while attached to the player
*(from the code)*. `force_protection.cog` attaches it with
`AttachThingToThingEx(shield, player, 0x8)`, which gives it the attach
flags `0x4` (to a thing, `0x4ce2ed`) and `0x8` (`0x503643`). Every
frame, a thing with `move=physics` gets its physics (`0x4cc44d`)
whatever it's attached to; one with neither of the attach flags `0x1`
and `0x2` (`0x4f595a`), as the shield, and not in water
(`0x4f5972`) takes the general case (`0x4f6270`), which turns it by its
`angvel` times the frame's time (`0x4f64ea` to `0x4f6569`). Its `angvel`
is in degrees a second (the angles go through `0x431fbd`, whose
quarters are 90, 180 and 270), so the shield turns 60 degrees a second
about each axis. Turning a thing also turns what is attached to it
(`0x4e768b` to `0x4e76e8`), so the shield turns with the player too.
Its own velocity doesn't move it, by the attach flag `0x8`
(`0x4cc4cc`).

### Clouds the game makes

A thing of `type=particle` makes its own cloud from its template
(`0x4f1da0`) *(from the code)*: `count` points, each `elementsize`
across, drawn with `material`, each at a random direction and a random
distance from `minsize` to `range`. The direction's pitch and yaw are
random over `pitchrange` and `yawrange` each way, or all round when
they are 0. The cloud's size is `range`. A template that also names a
`particle=` file takes that file's points, count and material instead
(`0x4f1dbc`); none in retail does. What its `typeflags` do, read from
the code (`0x2`, on most, isn't read there):

| Flag | What it does |
|---|---|
| `0x1` | Grows: the thing's scale, 1 at the start, grows by `maxthrust` a second; the scale is put into the thing's orientation, which the drawing takes the points through, so they spread out from the thing's centre (`0x4f1cfe`). A scale below 0.01 is put back to 0.01 *(from the code)* |
| `0x4` | Each point starts at a random cel of the material; without it, at the material's own current cel (`0x4f1f85`) *(from the code)* |
| `0x8` | At the end of its `timer`, the cloud loses `rate` / 10 points (at least 1) every 1 to 100 ms, at random, until it hasn't that many left; then it goes (`0x4f2010`). Without it, it goes at once *(from the code)* |
| `0x10` | Lighting mode 0 for the cloud; without it, 2 (`0x4f1e0c`) *(from the code)* |
| `0x20` | Each frame, each point may step to its next cel, at random: more likely the more cels it has left and the nearer the thing is to its end (`0x4f1c4b`) *(from the code)* |

Cog verbs change these on a running cloud: `SetParticleGrowthSpeed`
sets the growth (`0x504da0`), `SetParticleSize` the points' size
(`0x504d20`), `SetParticleTimeoutRate` the rate (`0x504e20`) *(from the
code)*; each acts only on a thing of `type=particle` (`0x504d3f`).
`force_heal.cog` sets a growth of -0.8 on `+force_heal`, which draws the
cloud in toward the player.

The teleport's `+telesparks`: 256 points of `00teleport.mat`, 0.002
across, out to 0.03, with `typeflags=0x3f` and `maxthrust=80`, so in
its 0.12 seconds it grows to about 10.6 times its size, out to about
0.3, then loses 25 points every 1 to 100 ms until it goes, as the
template and the code above give it *(inferred)*.

## In Reluva

`parseParticles` (`src/engines/sith/formats/sprite.h`) reads one into
`Particles`, each point's cel a whole number as the game takes it. The
data viewer's Sprites page draws each point as a square
of its size facing the camera. `reluva-tool sith formats <install>`
reads them all.

## Sources and checks

- **Where the facts come from:** the 3 files and their comments; the
  clouds drawn in the data viewer; `JK.EXE`'s cloud reader
  (`0x46c230`), its drawing (`0x46c750`), the particle things' template
  fields (`0x4f1a60`), making (`0x4f1da0`), each frame (`0x4f1c30`), end
  (`0x4f2010`) and cog verbs (`0x504ce0` to `0x504e50`); the physics
  (`0x4f5900`, with `angvel` read into a thing's bytes `0x1e0` to
  `0x1e8` at `0x4f60a8`) and `AttachThingToThingEx` (`0x503600`), all
  read from the machine code; every level's templates, and the cogs
  that use them.
- **How it was checked:** every file read by `reluva-tool sith formats`;
  `sith.unit.formats` on a cloud made by the test; a script listed every
  template in the 35 levels that names `particle=` (only
  `+force_shield`) or has `type=particle`, with their fields. Readers
  of INSERT OFFSET were searched for in the cloud code (`0x46c000` to
  `0x46ca20`) and at every load of the object a thing draws; no call to
  or pointer at the writer was found. The shield's turning isn't seen
  running.

## Open questions

- What INSERT OFFSET meant to the tool that wrote the files: the game
  never reads it. A recording of Force Protection would confirm the
  shield's turning.
