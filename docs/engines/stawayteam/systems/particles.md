# Star Trek: Away Team: particle systems

> **Status:** partial · **Code:** `src/engines/stawayteam/game/particles.cpp`, `src/engines/stawayteam/ui/map_particles.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

Sparks, embers, smoke and the like are particle systems: a
`CGenericParticleSystemAI` among a part's AIs, 182 in the maps (mostly on
invisible `Editor/Relay` parts) and 27 in the models, such as the EM
pulse grenade's sparks over what it holds. Each makes bursts of
particles, small models leaving from a start shape in a cone, at a
speed, for an age; each particle floats on, falls and bounces, or
swirls about the emitter.

## How it works

### Envelopes

Most of a system's settings are envelopes (`CEnvelope`: the cone angles,
`CFacingAngleEnvelope`, `CTrajectoryEnvelope`, `CSpeedEnvelope`,
`CTimeEnvelope`, `CLengthEnvelope`): a value from its `Start Value`
toward its `End Value` at its `Speed` a second. There its `Ending
Behavior` says what next: `Stop at end`, `Restart from beginning`, or
`Reverse Direction`, back toward its start (`0x4aa500`). At a `Speed` of
0 its value is its `Start Value` (`0x5258c0`), as for nearly all in the
game. A system's envelopes move on with its time, while it's enabled.

### Bursts

A system that's `Enabled` makes bursts at its `Burst Frequency` a second,
give or take its `Burst Frequency Variance`, drawn afresh after each
(`0x5249a6`). It stops after `Num Bursts To Stop After` of them, if that
isn't 0 (45 make one burst). Starting, or turned on, its envelopes go
back to their starts and its first burst comes a random part of a period
off (`0x524870`). Each burst makes, for each of its `Particles` (a
`CParticleInfo`), its `Num Particles` give or take its `Num Particles
Variance` (`0x524b4e`), and plays one of its `Sounds`.

Each particle:

- **Leaves** off the cone's axis by the `Inner Cone Angle` and a random
  part of the `Outer Cone Angle`, at any turn about the axis (`0x525040`).
  The axis points by `Face Angle` about the ground, plus the owner's
  facing with `Orient To Parent's Rotation`, and up by `Cone Trajectory`
  (`0x5250e0`; 90 is straight up).
- **Goes** at the `Speed` envelope's value, give or take `Speed
  Variance`, for the `Age` envelope's value, give or take `Age Variance`.
- **Starts** at the emitter point, its owner's place moved by `X Offset`
  and `Y Offset` and raised by `Height` (`0x524750`), and, by its `Start
  Point Type`:
  - `CPointParticleStartPositionType`: there.
  - `CLineParticleStartPositionType`: anywhere within its `Center Length`
    either way across the axis (`0x522ee0`).
  - `CDiskParticleStartPositionType`: on a ring about the axis, from its
    `Inside Radius` out to `Inside Radius` + `Outside Radius`, the one
    way across scaled by its `YX Ratio`; with `Use Cone Angle For Start
    Pos`, at the particle's own turn (`0x522640`).
  - `CCylinderParticleStartPositionType`: the disk's, and up the axis by
    up to its `Height of Cylinder` (`0x523740`).

The map's ground is foreshortened by 0.71277 (`0x616fbc`, the data's Y
To X Ratio): a particle's place across the ground is worked out as if
from above and drawn so, raised by its height (`0x524e75`).

### Particles

A particle's `AI` moves it:

- **`CFloatParticleAI`**: the base particle's: straight on at its speed.
- **`CGravityParticleAI`**: falling by its `Gravity` × 20.749 pixels a
  second each second (`0x51faa0`), bouncing off the ground by its
  model's bounce (`0x51fc10`).
- **`CSwirlParticleAI`**: turning about the emitter point at its `Angular
  Velocity` give or take its `Angular Velocity Variance`, radians a
  second, worked out unforeshortened and by the start shape's `YX Ratio`
  (`0x5208e0`).

Its picture is its `Model`'s sequence: `Animated` with `Obey Sequence
FPS`, at the sequence's rate over and over; `Animated` without it, its
frames once over its life; else its first (`0x51f1d0`).

### Messages and made entities

A `CSendMessageAction` of "Particle System On" (any case) to a part
turns its systems on and starts them again, "Particle System Off" turns
them off (`0x525410`): the maps' 41 systems that start off are force
fields' sparks, turned on as someone touches them.

A `CCreateCustomEntityAction` makes its `Entity` a part of the map at its
`New Location`: the EM pulse grenade's makes "Em Pulse" over each it
holds, an invisible entity with sparks and a `CActionAI` deleting it 15
seconds on.

## In Reluva

`ParticleSystem` (`game/particles.cpp`) is a system as above; the map
screen takes on each `CGenericParticleSystemAI` of its parts, of the
parts its models put down, and of the entities actions make
(`ui/map_particles.cpp`). One runs while its part is active and not
deleted. Its random numbers are its own *(inferred)*; a burst's sound is
one of its Sounds by chance, `Choose Sound Based On Burst` aside
*(inferred)*. The particles are drawn over the models, not sorted among
them *(inferred)*; a falling one that bounces slower than 1 pixel a
second lies still *(inferred)*.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled, at the
  addresses above; the maps' and models' systems for what they hold.
- **The unit tests:** envelopes moving and ending each way; bursts at
  their rate, stopping after their number and turned on again; a
  particle rising, falling at Gravity × 20.749 and bouncing, swirling at
  its distance; cylinder and line start shapes; on a map, a system
  turned on by its message, and an entity made by an action with its
  own system, deleted on time ([TESTING.md](../TESTING.md#unit-tests)).
- **Real data:** Mission 01's emitters run, a force field's sparks
  turned on, and the EM pulse grenade's Destroyed Action making its
  sparks over a held Klingon, looked at; the 41 maps of every mission
  load and run with their systems.

## Open questions

- The cone's axes' senses: Face Angle's zero and turn, and which way
  across the axis a line and a disk's YX Ratio lie *(inferred)*.
- How particles sort among the models, and how `Choose Sound Based On
  Burst` chooses.
- `Rotate Velocity`, `Tries To Collide`, `Collidable` and `Use Particle
  As Shot` aren't there.
