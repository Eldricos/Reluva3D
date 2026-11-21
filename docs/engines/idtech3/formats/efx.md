# id Tech 3: Raven's effect files (`.efx`) and their effects system

> **Status:** decoded · **Code:** `src/engines/idtech3/raven/effect_file.cpp`, `raven/effects.cpp`, `jk2/cgame_effects.cpp` · **Checked on:** Jedi Outcast 1.02a's 229 effect files (`reluva-tool idtech3 efx-check`, November 2026) and its multiplayer client game, run in Reluva · **Confidence:** the file's grammar confirmed on every file; how effects move, fade and draw published unless marked

## Overview

Jedi Outcast's sparks, smoke, blaster bolts, explosions and lights (and,
said to be the same, later Raven games', unchecked) are written as text
files in `effects/`, each a list of primitives: a particle, a line, a
sound, a light... Each value in a primitive is a range the effects
system picks from each time it plays the effect, so no two explosions
look alike.

The effects system belongs to the engine. In multiplayer, the client
game registers effects by name, plays them at a place facing a way, and
once a frame asks the engine to run them: parts whose delay is up are
made, every live part is moved, faded and drawn, and the dead dropped.
The client game also adds primitives of its own (a saber's trail, a
force power's curve) through the same system. Which calls those are:
[cgame-calls.md](../games/jk2/systems/cgame-calls.md).

## Layout

Text, in Raven's group format: `name value...` to the end of the line;
`name [ item item ... ]`, a list; `name { ... }`, a group. `//` comments.
Case doesn't matter. Media and effects are given as lists in all but one
of the 229 files.

The file is groups, one per primitive, named for its type: `particle`
(380 in Jedi Outcast), `line` (93), `sound` (93), `tail` (57), `light`
(50), `cameraShake` (46), `orientedParticle` (32), `decal` (29),
`cylinder` (26), `emitter` (18), `electricity` (18), `fxRunner` (17).
Five `flash` groups are skipped: multiplayer's engine leaves them out.

### A primitive's values

A value given as one number is that number; as two, a range picked from
at random. A vector is three numbers, or six (the least then the most of
each); four or five are refused.

| Key | Value | Meaning |
|---|---|---|
| `count` | range | how many are made (rounded) |
| `life` | range, ms | how long each lives (default 1) |
| `delay` | range, ms | how long after the effect each is made |
| `cullRange` | whole number | not made if the effect is farther than this from the eye |
| `origin`, `origin2` | vector | where it starts (and a line's other end), along the effect's forward, right and up |
| `radius`, `height` | range | the sphere or cylinder it starts on (default 1) |
| `velocity` / `vel`, `acceleration` / `accel` | vector | along the effect's axes |
| `gravity` | range | added to the acceleration's up |
| `rotation`, `rotationDelta` | range | degrees; degrees per 100 ms |
| `angle(s)`, `angleDelta` | vector | an emitter's model's turn (pitch, yaw, roll), and its turning per 100 ms |
| `bounce` / `intensity` | range | how much speed a bounce keeps; electricity's chaos; a camera shake's strength |
| `min`, `max` | vector | the box it bounces with (`useBBox`) |
| `density`, `variance` | range | an emitter's spacing (default 10 and 1) |
| `wind` | range | Jedi Outcast doesn't use it |
| `name` | text | the editor's |
| `flags`, `spawnFlags` | words | below |
| `shader(s)`, `model(s)`, `sound(s)` | name or list | what it draws or plays, one picked at random |
| `impactFx`, `deathFx`, `emitFx`, `playFx` | name or list | effects it runs: on a bounce, when it dies, along an emitter's path, an `fxRunner`'s |

Groups (`rgb`, `alpha`, `size` or `width`, `size2` or `width2`, `length`
or `height`) each hold `start` and `end` (a range, or a vector for
`rgb`; default 1), `parm` (`parms`), and `flags`: how the value goes
from start to end over the life. `linear`, `nonlinear`, `wave`, `clamp`
and `random`.

### Flags

The primitive's flags word (as the GPL source numbers it):

| Bits | Word | Meaning |
|---|---|---|
| 0–3, 4–7, 8–11, 12–15, 16–19 | (groups) | alpha's, rgb's, size's, length's, size2's change: linear 1, random 2, nonlinear 4, wave 8, clamp 12 |
| 0x100000 | `depthHack` | drawn in front of everything (a muzzle flash) |
| 0x200000 | `relative` | (Soldier of Fortune 2's; Jedi Outcast multiplayer ignores it) |
| 0x400000 | `setShaderTime` | |
| 0x800000 | `expensivePhysics` | traced every frame, not only where the point is solid |
| 0x1000000 | `useModel` | an emitter draws its model; electricity: `taper` |
| 0x2000000 | `usePhysics` | bounces off the world; electricity: `branch` |
| 0x4000000 | `useBBox` | bounces as its box; electricity: `grow` |
| 0x8000000 | `useAlpha` | the alpha is the colour's alpha, not a darkening of it |
| 0x10000000 | `emitFx` | an emitter runs its `emitFx` |
| 0x20000000 | `deathFx` | runs its `deathFx` when its life ends |
| 0x40000000 | `impactKills` | dies when it hits something |
| 0x80000000 | `impactFx` | runs its `impactFx` where it hits |

Spawn flags: `orgOnSphere` 1, `axisFromSphere` 2, `orgOnCylinder` 4,
`org2fromTrace` 0x10, `traceImpactFx` 0x20, `org2isOffset` 0x40,
`cheapOrgCalc` 0x100 and `cheapOrg2Calc` 0x200 (the offsets in world
axes), `absoluteVel` 0x400, `absoluteAccel` 0x800, `randRotAroundFwd`
0x1000, `evenDistribution` 0x2000, `rgbComponentInterpolation` 0x4000,
`affectedByWind` 0x10000. Seven files name `lessAttenuation`, which
multiplayer doesn't know either.

## Behaviour

### Playing an effect

An effect is played at a point facing a direction (the other two axes
made square to it), or with all three axes. For each primitive: none if
the eye is farther than its cull range; else `count` of them, each with
its own delay (spread evenly over the delay's range with
`evenDistribution`). One whose delay is under 1 ms is made at once,
others when their time comes, as late as they are.

Made, a primitive picks each of its values. Its start is `origin` along
the effect's axes (or the world's), then, with `orgOnSphere`, a point on
the ellipsoid of `radius` and `height` around it (its forward then out
from the middle with `axisFromSphere`), or with `orgOnCylinder` one on
the cylinder about the forward axis. Particles, oriented particles,
tails and emitters get a velocity and acceleration (gravity added to up);
made late, they start where they'd be by now. A line's or electricity's
other end is `origin2` from the effect, or, with `org2fromTrace`, where
the world stops a line from the start straight ahead (an impact effect
played there with `traceImpactFx`). Colours are picked per channel, or
with `rgbComponentInterpolation` one fraction along the line from the
least to the most.

### Bolted effects

The client game's `FX_PlayBoltedEffectID` (its call 261; jk2mp.exe
0x411860, 0x4167f0) plays an effect on a Ghoul2 model's bolt. It hands
over a structure: the instance (at 4), its model (8) and bolt (12), a
forward (16), an origin (28), the model's scale (40) and the entity (52).
The effect faces the forward, and its parts are a group (an "effect
cloud") whose update, each frame (0x411c20), puts every part at the
bolt before it moves (0x411e80): the bolt placed by `GetBoltMatrix` at
the effects' clock, with the model's scale, the instance's angles and
origin both taken from the first vector of the shared buffer, after the
entity's number is put in it at 24 (`TCGBoltPos`). The original never
asks the client game to fill that buffer here, so the vector is whatever
an earlier call left in it. It also starts the parts at, and culls them
by, a place it never sets; Reluva starts them at the origin given. No
release's client game calls it.

A decal is made at once as a mark on the world, by the client game; a
sound played; an `fxRunner` plays its effect there and then. A camera
shake does nothing in Jedi Outcast's multiplayer engine: it works out how
hard to shake and stops there.

### Each frame

A part past its life dies (running its death effect if it has one). The
others are moved, then their values faded:

- **Moving:** the velocity gains the acceleration times the frame's
  time; the part then moves by the velocity times the time *plus half
  the time squared*: the velocity where the acceleration belongs. Raven's
  code does this; Reluva does too, as it changes how far things fly.
- **Bouncing** (`usePhysics`): if the new place is in something solid
  (or always, `expensivePhysics`), the move is traced; on a hit the
  impact effect runs, then the part dies (`impactKills`) or bounces: the
  velocity mirrored in the surface and scaled by `bounce`, coming to rest
  on a floor once it's moving up at less than 4 units a second.
- **Fading:** each value goes from start to end by a fraction `perc`
  from 1 to 0, `start * perc + end * (1 - perc)`:
  - with no flags, it stays at its start;
  - `linear`: `perc` is the share of the life left;
  - `nonlinear`: full until the parm's time, then the share left of the
    rest; `clamp`: the share left until the parm's time, then 0; with
    `linear` too, either is averaged with it;
  - `wave`: `perc` times the cosine of the time since it was made times
    the parm;
  - `random`: `perc` (for alpha, the value) times a random fraction.

  The parm is given in hundredths of the life (`parm 50` is half way), or
  for a wave half turns a second: Raven's code makes it a time, or `parm
  × π / 1000`.
- **Colour:** the alpha, kept from 0 to 1, darkens the colour (for
  additive shaders) unless `useAlpha` makes it the colour's alpha.

Then each is drawn, unless it's behind the eye or very near it
(particles 12 units, oriented particles 16, polygons 24):

| Primitive | Drawn as |
|---|---|
| particle | a sprite, `size` its radius, turning by `rotationDelta` |
| oriented particle | a square facing its forward |
| line, electricity | a line `size` wide; electricity as below, reaching its end over its life with `grow` |
| tail | a line from where it is back along its path, `length` long |
| cylinder | from its start along its forward `length` units, `size2` round at the start and `size` round at the end |
| emitter | its model, scaled by `size`, turning; an effect left each time it has gone `density` (± `variance`) units |
| light | a dynamic light `size` units round, coloured |

The client game's own primitives work the same way: lines and sprites
(white, fading as asked), polygons of up to four corners (moving after a
delay, turning as they go), curves (a cubic Bézier of 16 pieces facing
the eye, fading to nothing along its length, its control points moving)
and a saber's trail (two triangles, their texture sliding as it fades;
the second's first two corners keep the first's alphas, as the GPL
source's one array of corners does *(inferred)*: jk2mp.exe's own code
wasn't traced).

In Jedi Outcast multiplayer's renderer an effect's sizes sit in
`refEntity_t`'s common part: a line's or electricity's width in its
radius, electricity's chaos in the first axis' first word, a cylinder's
end width in its radius and its start width in its rotation, from the
old origin to the origin.

A saber's blade is two entities from the client game: its core, a line,
and its glow, a type of its own (5) the renderer draws as sprites facing
the eye (jk2mp.exe 0x494903): from the blade's tip (its length at byte
180, along the first axis from the origin) back to the hilt, each sprite
0.017 wider than the one before and 0.65 of its width further back, the
first as wide as the entity's radius; then one at the hilt 5.5 to 5.75
across, a little different each frame. Each is a square of the entity's
shader and colour.

## In Reluva

`raven::parseEffectFile` reads a file; `raven::EffectSystem` registers
effects (by name without `effects/` or `.efx`; numbered before the
effects they run, so an effect running itself ends), plays them, and
each frame hands over what's seen as shapes, polygons and lights, which
the client game's host puts in its scene as Jedi Outcast's `refEntity_t`
(`jk2/cgame_effects.cpp`). Decals and the places of entities followed are
asked of the client game through the shared buffer, as Raven's engine
does. Its random numbers are the C library's `rand()`, as jk2mp.exe's
effects' (0x4d554b), from the Play page's generator, seeded from the
clock as the original's is (0x44d9a7), so neither repeats a run
(`raven/random.h`).

**Electricity** is drawn as Raven's renderer draws it (`raven::electricityStrips`;
the GPL source's `RB_SurfaceElectricity`, checked against jk2mp.exe's,
0x494c70). From its start to its end in 20-unit steps (a bolt shorter
than one isn't drawn), each step's point is pushed off the straight
line by an offset that builds up from (10, 10, 10). Each step adds a
random 3 along the line and 7 times its chaos to each side, with each
word its own random number (the vector macros evaluate it three
times), all from the seed the effect gave it, so the path holds still
over its life. Each step is drawn as three flat strips through two
kinks (a third and two thirds of the way, a little to either side,
made anew every frame from `rand()` as a -1 to 1 draw: jk2mp.exe's two
thirds and a third along, give or take 0.1, and 0.07 to the side, give
or take 0.025 (0x4956b0); jk2sp.exe's along fixed, and 0.08 to the
side, give or take 0.02 (0x486750); then each a further 0.02 back),
across the plane through the eye and both
ends. With `taper` (the renderer's 0x8000) the width falls as 1 − how
far squared, to nothing at the end. `branch` (0x4000) would fork it,
but 1.02a's fork count is never set (only read and lowered, 0x494f4f),
so it never does, and Reluva doesn't either.

Bolted effects are a group in the effects system (`playBolted`), whose
bolt the client game's host works out each frame as above (the shared
buffer read as the original reads it). Not done: wind.

## Sources and checks

- **Where the facts come from:** the grammar, the keys and flags from
  Jedi Outcast 1.02a's own files; how they're read, played, moved, faded
  and drawn from the GPL Jedi Outcast source's multiplayer effects system
  (`FxTemplate.cpp`, `FxScheduler.cpp`, `FxPrimitives.cpp`, `FxUtil.cpp`,
  `FxSystem.cpp`) and its renderer's `refEntity_t` and effect surfaces
  (`tr_types.h`, `tr_surface.cpp`), read as documentation; the code is
  new.
- **How it was checked:** `efx-check` reads and registers all 229 files
  with every effect they run (none fails; the shaders, models and sounds
  they name are all found), recorded by the game-files step
  `jk2-effects`; a unit test (`idtech3.unit.formats`, "effects") parses a
  made-up file and plays an effect, checking where a particle has moved,
  its fade, a delayed line, and its death. In 1.02a's multiplayer
  (`game-run -cgame -fire`, recorded by `jk2-game`, and live on the Play
  page) the blaster's bolt, muzzle flash, sparks bouncing on the floor,
  smoke, burn marks, its light on the wall, and the saber's trail are
  drawn. Another ("electricity") checks a bolt's strips: their count and
  ends, the same path for the same seed and another for another, the
  taper, and none for a bolt shorter than a step. A bolt hasn't been seen
  drawn in a match yet. Not yet compared with the original running.
