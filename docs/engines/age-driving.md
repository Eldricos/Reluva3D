# Driving in the Angel Game Engine

How Reluva drives Midtown Madness 2's cars: the Drive page, `car.cpp`,
`collision.cpp`, `city_collision.cpp`, `engine_sound.cpp`. The numbers
are the game's own; what they do is **Reluva's model**, written to drive
like the game rather than taken from it, because the game's program is
encrypted (see [age.md](age.md)). Each choice below is to be checked
against recordings of the original and tuned.

## The car's files

| File | What Reluva takes from it |
|---|---|
| `tune/<car>.info` | the name the menus show (Description), its colours (Colors, one a paint job) |
| `tune/vehicle/<car>.vehCarSim` | the simulation's numbers ([tune.md](../formats/age/tune.md) form) |
| `geometry/<car>_WHL0-3.mtx` | each wheel's centre at rest; its radius is half its bounds' height |
| `bound/<car>_BOUND.bnd` | the body's collision hull ([bnd.md](../formats/age/bnd.md)) |
| `tune/camera/<car>_near.camTrackCS`, `_far.camTrackCS`, `<car>.camPovCS` | the cameras |
| `aud/cardata/player/<car>.csv` | the engine's sound |

The wheels are 0 front left, 1 front right, 2 back left, 3 back right.

## The simulation

A rigid body stepped 120 times a second.

- **Mass**, and **InertiaBox** as the width, height and length of a box
  of that mass for the turning inertia; **CenterOfGravity** where forces
  turn it about.
- **Suspension** (WheelFront, WheelBack): each wheel casts a ray down
  2 x **SuspensionExtent** plus its radius from above its rest position.
  A spring holds a quarter of the car at mid travel (times
  **SuspensionFactor**), damped by **SuspensionDampCoef** plus 0.3 of
  critical. Its force pushes along the ground's normal.
- **Tyres**: each step a tyre cancels half of its contact point's
  sideways sliding, and brakes cancel half of its rolling, within a
  friction circle of **StaticFric** / 3 times its load (3 is most tyres'
  number; that makes it 1, a dry road's), **SlidingFric** / 3 once past
  it. The handbrake (**HandbrakeCoef**) brakes the back wheels and takes
  their grip down to 45%, so the back slides out. **BrakeCoef** scales
  each axle's brakes. **SteeringLimit** is the front wheels' lock, less
  at speed (lock / (1 + speed / 25 m/s)), eased in at 3 a second.
- **Engine**: **MaxHorsePower** at **OptRPM**, a third of it at
  **IdleRPM**, two thirds at **MaxRPM**, none past it. The drive force is
  power over speed (at least 3 m/s), shared by the driven wheels
  (**DrivetrainType** 0: back, 1: front, 2: all four: the Beetle is 1,
  the 4x4s are 2).
- **Gearbox** (Trans): **AutoNumGears** gears. **Low** and **High** are
  read as the speeds in miles an hour at the redline in first and top
  gear, the gears between spread evenly in ratio; **Reverse** reverse's.
  That reading isn't settled: High is close to the menus' top speed for
  the Beetle (90 and 91) but not for others (the Panoz GT: 180 and 240).
  It changes up at 93% of MaxRPM and down below half OptRPM, with no
  drive for half (up) or a quarter (down) of **GearChangeTime**.
  Holding the brake when stopped engages reverse; the throttle then
  brakes.
- **Drag** is set so that full power tops out at top gear's redline
  speed (the Aero block's Drag isn't used yet).
- **Body collisions**: spheres of 0.2 m round the hull's corners are
  pushed out of the city's triangles, with an impulse using the lesser of
  **BoundElasticity** and the surface's elasticity, and friction from
  **BoundFriction** times the surface's. A contact pushing up more than
  sideways (its normal over 0.5 up) is taken as ground: straight up, by
  the depth it would have lifted the sphere. Kerbs and the 15 cm steps
  at bridge ends then lift a car instead of throwing it back.
- Left alone and nearly stopped, a car holds still.

Not used yet: Aero's damping and down force, the engine's and
drivetrain's inertias, the tyres' displacement and damping terms,
OptimumSlipPercent, camber and wobble, the axles' torque, SSS, damage
(`.vehCarDamage`), the gyro and stuck files.

On a flat test plane a car with Reluva's default setup (250 hp, 1,000
kg, a 90 mph top gear: near the Beetle's numbers) settles on its springs,
does 0-56 mph in about 10 seconds and tops out at 78 mph (88% of its top
gear's speed); `tests/test_age.cpp` checks it settles, accelerates, tops
out within 75-110% of top gear's speed, turns the way it's steered and
backs up.

## The city's collisions

`buildCityCollision`:

- every triangle generated from the PSDL (roads, ground, walls, roofs);
- the facade bounds (PSDL attribute 7) as walls from their vertices'
  height down a metre to their height: they stand where placed building
  fronts are (`sw_`, `nw_`, `gw_`... models have no bounds of their own);
- the bounds of the placed models that have one (`bound/<model>_BOUND.bnd`:
  207 of San Francisco's 3,763, 1,263 of London's 1,997).

In a grid of 8 m cells. The tunnels' walls and ceilings and the
bridges' railings are among the city's surfaces ([psdl.md](../formats/age/psdl.md)),
so they're collided with; the water under the bridges and the roads'
far-off low-detail copies aren't.

## Cameras

`camTrackCS` (near and far): the camera sits at **Offset** from the car
turned only with its heading (0, 1, 4.06 for the Beetle's near: a metre
up, four behind), eases towards it (at 6 a second), is kept between
**MinDist** and **MaxDist** of the point **TrackTo** above the car, and
looks at that point; **CameraFOV** is its field of view. `camPovCS` (in
the car): at **Offset** in the car, looking along it. The many other
numbers (approach, drift, hill and reverse behaviour) aren't used yet.

## Engine sound

`aud/cardata/player/<car>.csv`:

```
Horn wave name,Horn volume,flags,Num Engine Samples,clutch wave name,clutch volume
VWHORN,0.95,0,4,REVERSE,0.93
Engine wave name,Min Volume,Max Volume,fade in start RPM,fade in end RPM,fade out start RPM,fade out end RPM,Min Pitch,Max Pitch,Pitch shift start RPM,Pitch shift end RPM
VWIDLE,0.55,0.835,1,800,2500,7000,0.85,2,1,7000
...
```

Each sample (`aud/aud22/engines/<wave>.22k.wav`, else
`aud/aud11/<wave>.11k.wav`) loops, faded in over its fade-in RPM and out
over its fade-out RPM, its pitch from Min to Max over its pitch-shift
RPM. Reluva reads Min and Max Volume as the volume with the throttle
closed and open; that's a guess. The horn and clutch sounds aren't
played yet. The mixer gained a pitch control for this
(`Mixer::setPitch`).
