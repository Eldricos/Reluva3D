# Midtown Madness 2: the car's simulation

> **Status:** partial · **Code:** `src/engines/age/game/car.cpp` · **Checked against:** the game's figures and recordings, not its code · **Confidence:** inferred unless marked

## Overview

How a car drives: its files, and Reluva's model of the simulation they tune (suspension, tyres, engine, gearbox, drag). The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

### The car's files

| File | What Reluva takes from it |
|---|---|
| `tune/<car>.info` | the name the menus show (Description), its colours (Colors, one a paint job) |
| `tune/vehicle/<car>.vehCarSim` | the simulation's numbers ([tune.md](../formats/tune.md) form) |
| `tune/vehicle/<car>.vehStuck` | when a car turned over is set upright |
| `tune/vehicle/<car>.vehCarDamage` | its damage and smoke |
| `geometry/<car>_WHL0-3.mtx` | each wheel's centre at rest; its radius is half its bounds' height |
| `bound/<car>_BOUND.bnd` (or `.bbnd`) | the body's collision hull ([bnd.md](../formats/bnd.md)) |
| `tune/camera/<car>_near.camTrackCS`, `_far.camTrackCS`, `<car>.camPovCS` | the cameras |
| `geometry/<car>_dash.pkg`, `tune/<car>_dash.asnode` | the dashboard seen from inside |
| `aud/cardata/player/<car>.csv` | the engine's sound, the horn and the clutch |

The wheels are 0 front left, 1 front right, 2 back left, 3 back right.

### The simulation

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
  it. The engine pushes no harder than the grip the cornering leaves: a
  wheel asked for more spins (heard as a skid) but keeps its grip, as a
  tyre held near its best slip does, rather than sliding. Rolling
  resistance is 1.5% of each wheel's load, and off the throttle in gear
  the engine holds the car back by another 4%. The handbrake
  (**HandbrakeCoef**) brakes the back wheels and takes their grip down to
  45%, so the back slides out. **BrakeCoef** scales each axle's brakes.
  **SteeringLimit** is the front wheels' lock, less at speed (lock / (1 +
  speed / 25 m/s)), eased in at 3 a second.
- **What the road is made of** (below): its friction scales the tyres'
  grip, and its drag (water) slows them.
- **Engine**: **MaxHorsePower** at **OptRPM**, a third of it at
  **IdleRPM**, two thirds at **MaxRPM**, none past it. The drive force is
  power over speed (at least 3 m/s), shared by the driven wheels
  (**DrivetrainType** 0: back, 1: front, 2: all four: the Beetle is 1,
  the 4x4s are 2).
- **Gearbox** (Trans): **AutoNumGears** gears. **Low** and **High** are
  read as the speeds in miles an hour at the redline in first and top
  gear, the gears between spread evenly in ratio; **Reverse** reverse's.
  It changes up at 93% of MaxRPM and down below half OptRPM, with no
  drive for half (up) or a quarter (down) of **GearChangeTime**.
  Holding the brake when stopped engages reverse; the throttle then
  brakes. With **manual gears** (the M key) it changes only when asked
  (E up, Q down; down from first when stopped is reverse) and the
  throttle drives either way.
- **Aero**: **Drag** and **Down** read as newtons per (metre a second)
  squared: drag against the motion, down force pressing the car to the
  road (the police car and the Panozes have 1). Many cars have no drag
  (0); none is enough to hold a car below its top gear's redline, so
  every car's top speed is High.
- **Body collisions**: spheres of 0.2 m inside the hull's corners (moved
  in so they touch its box's faces), and at the middles of the box's
  front, back and sides (low and halfway up, so that a bollard or a cone
  doesn't pass between the corners), are pushed out of the city's
  triangles, with an impulse using the lesser of **BoundElasticity** and
  the surface's elasticity, and friction from **BoundFriction** times the
  surface's. A contact pushing up more than sideways (its normal over
  0.5 up) is taken as ground: straight up, by the depth it would have
  lifted the sphere. Kerbs and the 15 cm steps at bridge ends then lift a
  car instead of throwing it back. The hardest knock each frame is
  kept, with where it was, for the sounds and the damage.
- **Stuck** (`.vehStuck`): tilted past 60 degrees, slower than
  **MoveThresh** (1.75 m/s) and no further than **PosThresh** (1.25 m)
  from where it tipped after **TimeThresh** (2 s), a car is set upright,
  facing the way it did, a metre up. **Turn** is 1.57 (90 degrees) or
  3.14 for most cars, 2.06 for the city bus and 0.74 (42 degrees) for the
  double-decker: read as how far over counts, two thirds of it up to 90
  degrees, so 60 for most (as Reluva had it) and 28 for the double-decker,
  which tips over sooner. **Rotation** (0 for all) is read as a turn to
  add as it's set upright. **Translation** (0.1, the classic Mini's
  0.164) would be the lift; Reluva keeps its metre, so it lands clear of
  the ground.
- Left alone and nearly stopped, a car holds still.

#### Against the game's figures

On a flat plane at full throttle, every one of the 20 cars tops out
at its High (within 1%), in 9-22 seconds, and does 0-60 mph in 6-12
seconds (the Panoz GT 6.7, the Beetle 6.9, the double-decker bus 9.8).
The `.info` files' Horsepower, Top Speed, Durability and Mass are the
menus' bars, not the simulation's: the Panoz GT's "240" against High's
180, its "850" mass against 1,200 kg. Two easy readings hold cars well
below High, so neither is the game's: a "rolling" drag that grows with
speed (as much as the air's at 60 mph), and wheelspin that keeps a car
at its sliding friction for good (the Bullet's back tyres' is 1.4 / 3,
so it crawls).

**How the cars rank** is what the bars can check. `reluva-tool age cars`
drives every car on a flat plane (0-60 and 0-100 mph, the quarter mile,
top speed, 60-0 braking) and ranks them, and the game-files test keeps
its output (step `cars`). Over the 20 cars, as Spearman's rank agreement
(1: the same order):

| Compared | Agreement | What it says |
|---|---|---|
| The bars against their files: Top Speed and High, Horsepower and MaxHorsePower, Mass and Mass, Durability and `.vehCarDamage`'s MaxDamage | 0.92, 0.79, 0.73, 0.80 | Each bar ranks the cars much as its file's figure does, but isn't a scale of it |
| Reluva's top speed and the Top Speed bar | 0.91 | As High is |
| Reluva's quarter mile and power to weight (MaxHorsePower / Mass) | 0.79 | Past 60 mph the cars are as quick as their engines and weight make them |
| Reluva's 0-60 and power to weight | 0.42 | Below 60 nearly every car is held by its tyres' grip, not its engine |

So off the line Reluva's cars are closer than their engines would make
them: 0-60 in 5.7 to 12 s, the 4x4 and the Ford pickup (550 hp for
2,500 kg) in 6.4 and 6.6 s against the Panoz GT's 6.8. The Horsepower
bar tracks the engine alone (the semi's 900 is among the slowest cars,
rightly, for its 3,500 kg). Whether the game's cars are as close off the
line, so whether the grip (StaticFric / 3) or the engine's units are
right, needs a recording of a few cars' starts *(inferred)*. The 4x4's
60-0 mph of 89 m, against 37-57 for the rest, is its file's: its front
BrakeCoef is 0.062.

Aero's **AngCDamp**, **AngVelDamp** and **AngVel2Damp** are a damper's
three terms per axis (x pitch, y yaw, z roll): a constant against the
turning, one with the turning and one with its square (the older layout
has linear CDamp, VelDamp and Vel2Damp beside them, so "Vel" is the
turning, not the road speed). Read as a torque of the car's mass times
that (so an angular deceleration of 12 x it over the inertia box's two
sides squared): as plain radians a second squared the Bullet's yaw 5
stops it turning at all with Reluva's grip, as newton metres they'd do
nothing. They're used for pitch and roll: the trucks' and the police
car's (6, 0, 6) keep them from tipping, the DB7's and GTR-1's large
pitch constants hold them level. Yaw's aren't: the AI cars' 5.8-9 would
keep them from cornering at Reluva's grip (which is StaticFric / 3; at
StaticFric itself they'd fit), so that's left until the grip's settled.
Damping pitch only in the air was tried: San Francisco's hill circuits
came out as before, but London Run's opponents worse (2 of 7 unfinished
in 900 s); damped always, every opponent finishes, London's circuits
faster with fewer resets (London Run 697 s to 602), San Francisco's a
little slower (Take It Easy 505 to 585).

**SSSValue** and **SSSThreshold**: speed-sensitive steering. Most cars
have 1 and 0, read as Reluva's lock of 1 / (1 + v / 25); the Minis 0
(no less lock with speed); two AI cars 0.4 from 50 or 30 (mph). Reluva's
lock is 1 / (1 + SSSValue x the speed over the threshold / 25), the
opponents steering with the same. **TireDragCoefLong** (0.02, the DB7's
rear 0.07) is the wheels' rolling resistance as a share of the load
(Reluva had 0.015 for all).

Not used: the engine's and drivetrain's inertias (Engine AngInertia 1,
plausible as kg m2 for the engine alone; Drivetrain 2,000-30,000 can't
be kg m2 at the wheels, which would weigh the car 7-66 times over), the
tyres' displacement and damping (two sets: 0.125 / 0.25 for the
player's cars, 0.075 / 0.75 for the AI's: a contact patch's give and
its damping, it seems), TireDragCoefLat (0.05), OptimumSlipPercent (a
share: 0.2 front, 0.14 back for most, the trucks 0.42-0.74, the AI's
fronts 0.01-0.03), CamberLimit (-1, 0, 0.2, 0.4, 0.49, 0.17: too large
for camber in radians), WobbleLimit (0 for all) and the axles'
TorqueCoef and DampCoef (1 and 0.5 on eight cars: an anti-roll bar,
perhaps).

`.vehGyro` (20 cars): **Drift**, **Spin180**, **Reverse180**, Pitch and
Roll (0 for all; the fire truck's all 0). Read as help turning round:
turning (radians a second squared, at full steering, about the car's up,
on the ground) added while the handbrake's held at speed (Spin180: 0.15
for the Freightliner to 1.6 for the new Mini), backing at more than
3 m/s (Reverse180, the nose swinging the other way: 0.07 for the GTR-1
to 6.1 for the classic Mini) and while the back wheels slide (Drift,
0.03-0.73). Racing with it, the opponents still finish every race
measured; San Francisco's circuits came out a little faster, London's
City Circuit slower (600 s against 566) with more resets.

Across the 48 `.vehCarSim` files (and 20 `.vehGyro`s) these narrow
down only so far without watching the game:
- Most are the same for every car: the tyres' drag terms (0.02 along,
  0.05 across), CarFrictionHandling (1), Engine AngInertia (1 in 39 of
  40), the brakes' static coefficients (1.2), the gyro's Pitch and Roll
  (0); the axles' TorqueCoef and DampCoef are 0 in 32 of 40.
- AngVelDamp and AngVel2Damp are (6, 0, 6) in 10 cars: the first and
  third (pitch and roll, the second being yaw) are what's damped; the
  city bus (6, 0, 0) and the Freightliner (3, 0, 0) damp pitch only; few
  cars damp yaw (the Bullet's AngCDamp 5).
- The tyres' displacement and damping come in two sets (0.125 and 0.25,
  or 0.075 and 0.75); OptimumSlipPercent runs 0.01-0.42.
- Drivetrain AngInertia is 2,000 in 23 cars, 30,000 in the 5 heavy ones.
- Seven files are an older layout (DrivetrainLeft, Spin*, Drift*
  fields), not read.

## In Reluva

`CarSim` (`src/engines/age/game/car.{h,cpp}`) steps the car, with
`CarSetup` and `AxleSetup` read from its files. The Drive page drives
any car round either city. The handling hasn't yet been compared with
the game's ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

`testCarSim` (`tests/engines/age/unit/driving.cpp`) checks a car with
Reluva's default setup (250 hp, 1,000 kg, a 90 mph top gear) settles on its springs, accelerates, tops
out within 5% of top gear's speed (it does 0-60 in 8 seconds and tops
out at 89.8 mph), turns the way it's steered, backs up, changes gear by
hand and is set upright on its roof.

## Sources and checks

- **Where the facts come from:** each car's `.vehCarSim`, `.vehStuck`, `.info`, wheel `.mtx` and bounds; the menus' figures (top speed, horsepower) to compare with; public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.driving`; the Drive page, every car in both cities; the cars' top speeds against their `.info` figures; `reluva-tool age cars`, every car's runs ranked against the game's figures (the game-files test's `cars` step).
