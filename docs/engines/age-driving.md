# Driving in the Angel Game Engine

How Reluva drives Midtown Madness 2's cars: the Drive and Race pages,
`car.cpp`, `collision.cpp`, `city_collision.cpp`, `materials.cpp`,
`engine_sound.cpp`, `damage.cpp`, `dashboard.cpp`, `hud_map.cpp` and
`hud_pictures.cpp`. The numbers are the game's own; what they do is
**Reluva's model**, written to drive like the game rather than taken from
it, because the game's program is encrypted (see [age.md](age.md)). Each
choice below is to be checked against recordings of the original and
tuned.

## The car's files

| File | What Reluva takes from it |
|---|---|
| `tune/<car>.info` | the name the menus show (Description), its colours (Colors, one a paint job) |
| `tune/vehicle/<car>.vehCarSim` | the simulation's numbers ([tune.md](../formats/age/tune.md) form) |
| `tune/vehicle/<car>.vehStuck` | when a car turned over is set upright |
| `tune/vehicle/<car>.vehCarDamage` | its damage and smoke |
| `geometry/<car>_WHL0-3.mtx` | each wheel's centre at rest; its radius is half its bounds' height |
| `bound/<car>_BOUND.bnd` (or `.bbnd`) | the body's collision hull ([bnd.md](../formats/age/bnd.md)) |
| `tune/camera/<car>_near.camTrackCS`, `_far.camTrackCS`, `<car>.camPovCS` | the cameras |
| `geometry/<car>_dash.pkg`, `tune/<car>_dash.asnode` | the dashboard seen from inside |
| `aud/cardata/player/<car>.csv` | the engine's sound, the horn and the clutch |

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

### Against the game's figures

On a flat plane at full throttle, every one of the 20 cars now tops out
at its High (within 1%), in 9-22 seconds, and does 0-60 mph in 6-12
seconds (the Panoz GT 6.7, the Beetle 6.9, the double-decker bus 9.8).
The `.info` files' Horsepower, Top Speed, Durability and Mass are the
menus' bars, not the simulation's: the Panoz GT's "240" against High's
180, its "850" mass against 1,200 kg. Before this, two things held cars
well below High: a "rolling" drag that grew with speed (as much as the
air's at 60 mph), and wheelspin that took a car to its sliding friction
for good (the Bullet's back tyres' is 1.4 / 3, so it crawled). The low
cars also sat on their hull's spheres. Reluva's AI drivers finish the
races 15-25% sooner than before, with fewer resets.

Not used: Aero's **AngCDamp**, **AngVelDamp** and **AngVel2Damp** (read
as the car's turning slowed about its three axes, by a constant, with
the turning and with its square, in radians a second squared, the
Bullet's 5 of yaw stops it turning at all: their units must be other),
the engine's and drivetrain's inertias, the tyres' displacement, damping
and drag terms, OptimumSlipPercent, camber and wobble, the axles'
torque and SSS.

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
  third (roll and pitch, the second being yaw) are what's damped; only a
  few cars damp yaw (the Bullet's AngCDamp 5).
- The tyres' displacement and damping come in two sets (0.125 and 0.25,
  or 0.075 and 0.75); OptimumSlipPercent runs 0.01-0.42.
- Drivetrain AngInertia is 2,000 in 23 cars, 30,000 in the 5 heavy ones.
- Seven files are an older layout (DrivetrainLeft, Spin*, Drift*
  fields), not read.

`tests/test_age.cpp` checks a car with Reluva's default setup (250 hp,
1,000 kg, a 90 mph top gear) settles on its springs, accelerates, tops
out within 5% of top gear's speed (it does 0-60 in 8 seconds and tops
out at 89.8 mph), turns the way it's steered, backs up, changes gear by
hand and is set upright on its roof.

## What the city is made of

`city/materials.mtl` is blocks of `mtl <name> { ... }`: **elasticity**,
**friction**, **effect**, **sound**, **drag**, **width**, **height**,
**depth**, **ptxindex** and **ptxthreshold** (particles). There are
eight: `deepwater` (depth 100), `_default` (friction 0.9), `grass`
(sound 2), `water` (sound 1, drag 0.119), `dirt`, `sand`, `cobblestone`
and `wood`. `city/materials.csv` lists textures and their material
("none": `_default`). Reluva gives the city's triangles their texture's
material: friction relative to `_default`'s (so roads keep their grip),
its sound (which surface of the tyres' tables), its drag and depth.
Deep water (depth 10 or more: the bay, the Thames) isn't collided with,
so a car sinks and is put back. A model's bound polygons keep their own
friction and elasticity and take the sound, drag and depth of the city
material of their material's name. **effect** is `none` for every
material, so it isn't used.

**ptxindex** and **ptxthreshold** are two pairs: what a wheel on the
material throws up, and from how much slip. The thresholds are 0.25 and
0.5 for most, 0.25 and 0.3 for cobblestone, 0 and 0 for water. Reluva
reads them against the wheel's slip (0 gripping, 1 sliding at 10 m/s or
spinning): the first effect once the wheel slips past the first
threshold, the second past the second, from none at the threshold to 8
particles a second slipping fully. A threshold of 0 (water) means
whenever the wheel rolls (over 1.5 m/s), more the faster up to 10 m/s.
The indices aren't named anywhere: they aren't the `tune/effects` rules
in alphabetical or archive order. Reluva pairs them by what the
materials are, and draws each with the particle texture that looks like
it:

| ptxindex | used by | rule (`tune/effects`) | texture |
|---|---|---|---|
| 1 | grass, sand (first) | `dust` | `fxpt8` |
| 2 | grass (second) | `grass` | `fxpt4` |
| 4 | `_default`, cobblestone (first): every road | `smoke` | `fxpt8` |
| 5 | sand (second) | `dirt` | `fxpt11` |
| 6 | water (second) | `splash` | `fxpt9` |
| 7 | cobblestone (second) | `rock` | `fxpt14` |

The rules' frame numbers (grass 6-9, splash 16-21) are of a sheet
bigger than any of the textures, so a particle takes any frame of its
texture; their radii (1-2 m, for clouds) are halved. **width** (water
2.42, grass and sand 0.45) spreads where the particles start across the
tyre's foot, and **height** (grass 0.04) lifts it: Reluva's reading of
them.

## The city's collisions

`buildCityCollision`:

- every triangle generated from the PSDL (roads, ground, walls, roofs),
  with its material (above);
- the facade bounds (PSDL attribute 7) as walls from their vertices'
  height down a metre to their height: they stand where placed building
  fronts are (`sw_`, `nw_`, `gw_`... models have no bounds of their own);
- the bounds of the placed models that have one (`bound/<model>_BOUND.bnd`
  or `.bbnd`: 207 of San Francisco's 3,763, 1,263 of London's 1,997).

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
the car): at **Offset** in the car, looking along it, with the
dashboard over the view.

The rest of `camTrackCS`, by Reluva's reading of the names (the numbers
are alike across the cars, so they're checked by how the camera
behaves):

- **Approach** (ApproachOn 1 for all): how fast the camera catches the
  car up, a share of the way a second. **MinAppXZPos** at **MinSpeed**
  to **MaxAppXZPos** at **MaxSpeed** across (most cars 1.8 at rest to
  12 at 35 m/s: about 6 at 15 m/s, the rate Reluva used before), rising
  by at most **AppInc** a second and falling by **AppDec**; **AppYPos**
  up and down. It goes round the car, its bearing and distance eased
  apart, so it swings round rather than through the car.
- **Drift**: steering past **MinHardSteer** (0.8 of full lock) for
  **DriftDelay** (0.3 s), the rate is **FrontRate** (0.55) of itself, so
  the car swings out before the camera follows.
- **Hill**: the offset is tilted with the car's slope, its sine kept
  within **HillMin**..**HillMax** (-0.56..0.56 for most), easing
  **HillLerp** of the way a frame (at 30 a second).
- **Reverse** (ReverseOn): backing for **RevDelay** (2 s), the camera
  swings round to look the way the car goes, at **RevOnApp**'s rate,
  and back at **RevOffApp**'s once the car goes forward.

- **RearRate** (0.5): backing, before it swings round, the rate is that
  share of itself, as FrontRate drifting.
- **AppRot** (30 or 60): the most its bearing round the car turns, in
  degrees a second (not swinging round). **AppPosMin** (0.25 m) and
  **AppRotMin** (0.01 rad): moves smaller than these are left alone.
- **LookAbove** (0 for most, 0.2-0.66): metres it looks over TrackTo.
- **SteerOn**, **SteerMin**, **SteerAmt**: turned with the steering past
  SteerMin, SteerAmt degrees at full lock; SteerOn is 0 for every car,
  so it's never on.
- **CameraNear** (0.5 behind, 0.1 in the car): the near clip.
- **BlendTime** (1.2 s for all): a change of camera blends over it, from
  the last view to the new.

Not used: **FlipDelay** (0.5 for all: the camera stays level whatever
the car does, so there's nothing to delay), **AppXRot** and **AppApp**/
**AppAppOn** (tilt and the approach's own approach: Reluva's camera
doesn't tilt, and AppInc/AppDec already ease the rate), **VertOffset**,
and **CameraFar** (137-600 m: Reluva's far clip is the Graphics
Options' visibility, and the game's own screens show the city farther
off than 600 m).

## The dashboard

`geometry/<car>_dash.pkg` is flat parts in the camera's space: `dash`,
`roof`, `wheel`, `speed_needle`, `tach_needle`, `damage_needle` and
`gear_indicator`, each with a `.mtx` whose origin is its pivot.
`tune/<car>_dash.asnode` (the Beetle's is `.asNode`) places them:

- **DashPos**, **RoofPos**, **WheelPos**: where the dash, the roof and
  the steering wheel (from the dash) are, in metres, the camera looking
  down -z with the near plane at 0.1 m;
- **SpeedPivotOffset**, **TachPivotOffset**, **DmgPivotOffset**,
  **WheelPivotOffset**, **GearPivotOffset**: added to the parts'
  pivots;
- **RPMRotMin/Max**, **SpeedRotMin/Max**, **DamageRotMin/Max**: the
  needles' turns (radians, anticlockwise) at nothing and full scale;
- **WheelFact**: how far the steering wheel turns, times a quarter
  turn at full lock.

Reluva turns the speed needle to full scale at top gear's speed, the
rev counter's at MaxRPM and the damage needle's at wrecked; that's a
reading. The Beetle's speed needle points left at rest and turns
anticlockwise; its SpeedRotMax (3.614) reaches its dial's 140 mph mark,
well past its 90 mph top speed, and the RotMax values vary from car to
car up to 8.5, so they aren't each dial's sweep. The gear indicator's
texture is `R` for every car; Reluva shows `texture/r` backing up, `n`
standing in first and `d` otherwise. The parts are sized for a 4:3
screen; a wider view stretches them across.

## Damage

`tune/vehicle/<car>.vehCarDamage`:

- **MaxDamage** (the Beetle's 321,300), **MedDamage** (150,000),
  **ImpactThreshold** (1,500), **RegenerateRate** (0 for all);
- **TextelDamageRadius** (2.7): metres round a knock that look damaged;
- **SmokeOffset**, **SmokeOffset2**: where the smoke comes from, in the
  car;
- the smoke's birth rule, as `tune/effects/*.asBirthRule`: **Position**,
  **Velocity**, **Life**, **Mass**, **Radius**, **Drag**, **DRadius**,
  **DAlpha**, **Gravity**, **TexFrameStart**/**End**, **Color** (a
  signed ARGB: -167772161 is white, 0xF5 opaque) and their **Var**
  ranges.

Reluva reads the knocks' impulse (kg m/s) as damage: a knock over
ImpactThreshold adds what it's over by, up to MaxDamage; driving the
Beetle into walls for two minutes, its hardest knock was about 10,000. From MedDamage the car
smokes, from 10 puffs a second to 30 near wrecked, from each offset: a
billboard of a frame of `texture/fxpt8` (four smoke puffs, two by two;
frame 2 is the dark one), rising (Gravity is up: 15, where the falling
effects' are -9.8), growing by DRadius a frame (at 30 a second) and
fading over its life (and by DAlpha, in 255ths a frame), slowed by its
Drag. Which texture the frames are of is a guess. The same rules make
the props' and the wheels' particles (see
[age-traffic.md](age-traffic.md) and the materials above). At
MaxDamage the car is wrecked: its engine stops; the Drive page puts it
back mended after a moment, a race ends ("Wrecked"). Each knock's place
takes the body's parts within TextelDamageRadius to their damaged
textures (`<texture>_dmg`: the Beetle has them for its front, back and
sides), whole parts at a time rather than the game's painting round the
point. The damage needle, and a damage meter on the HUD, show it.

## The HUD

### The map

`tune/<city>.mmHudMap`: **Size** and **Pos** (shares of the screen:
0.21 by 0.25 at 0.78, 0.75, its top left corner, so it's the bottom
right corner), **ZoomIn** (starting near), **Approach Rate**, **ZoomInDist**
and **ZoomOutDist** (577 and 1,195 in San Francisco), **IconScaleMin**
and **IconScaleMax** (34 and 52.4), the same four for full screen (the
**FS** ones), and **Ocean Color**. "Approach Rate" and "Ocean Color" are
two words, read as a name and the rest. On the game's screens the map's
top left is at Pos, but it's drawn a little smaller than Size: 124 x
110 at 640 x 480 (Size gives 134 x 120), 245 x 165 at 1280 x 720; Reluva
draws it at 0.92 of Size. It turns with the player (whose arrow always
points up) and keeps the player in its middle.

`geometry/hudmap_sf.pkg` and `hudmap_london.pkg` are the map: the map
pictures (`s_SFMapNW_f`, ... the ones on the cities' bus shelters) laid
flat on the city's ground plan at height 0, so a point on the map is
that point in the city. `hudmap_tri` is the cars' arrow (28 long, its
point along -z) in ten paint jobs of colours, `hudmap_square` a dot in
nine (RED_DOT, BLUE_DOT, GREEN_DOT, GREY_DOT, YELLOW_DOT, FINISH_DOT,
GOLD_DOT, BANK_DOT, HIDEOUT_DOT). Reluva draws the map in its corner on
the ocean's colour, seen from the zoom's distance above the player with
a 30-degree field of view (a guess: 60 shows half the city), turned so
the way the player faces is up, easing between zooms at the approach
rate. The icon sizes read as lengths in metres from near to far (34 m at
577, 52 m at 1,195); the numbers fit that: the FS ones (15 and 18 at 786
and 1,581) come out a similar share of a full-screen map. The player is
yellow, the opponents red and the police blue; the race's checkpoints
are dots, the next green, the finish its own. **Z** zooms, **F** shows
it full screen.

### The pictures

The game's HUD pictures are `texture/*.tga`, made for a 640 x 480
screen: `SPEED` (a speedometer's panel; it has no alpha: what's past
its curve is pure black, its boxes' (8, 8, 8)), `DIGITAC_0`-`9` (41 x
56, and `_HALF`), `DIGITAC_GEAR_1`-`8`, `D`, `N`, `P`, `R` (20 x 23),
`TACOMETER TICKS_HALF`, `SPEED_TICKS` (129 x 43: rising bars, green to
red), `MPH`, `DAMAGE` (a green to red bar, 500 x 12) and `DAMAGE_LABLE`
("DAMAGE METER", 129 x 12), `DIGI_0`-`9` and `DIGI_COLON` (the clock;
no alpha), `opp_icon` (places 1-8 and a gold $, four to a row, each
stored upside down), and the dots.

Where they go is in the game's program, but its screens show it
(gameplay footage at 640 x 480, [age-footage.md](age-footage.md)):

- The speed in `DIGITAC_*` digits, three places ending at x 144, their
  top at y 366, with no leading zeros.
- The gear at (15, 424): `D` driving an automatic, the gear's number a
  manual, `N` standing, `R` backing.
- `SPEED_TICKS` at (8, 421), as many of its bars as the revs' share of
  their most (all of them revving in neutral).
- The damage meter at (8, 467): `DAMAGE_LABLE` over a 129-pixel window
  of `DAMAGE`, unscaled. Through a race the window's right end goes
  green, yellow, orange, red and its left end from green to orange, and
  it's green again at the next race: the window slides along the bar
  with the damage, from its green end to its red.
- The clock top middle (x 247-390 at y 0) in `DIGI_*`: minutes,
  seconds and hundredths, "01:02:19"; their black is see-through.

`SPEED`, `MPH`, `TACOMETER TICKS_HALF` and the `_HALF` digits aren't on
any of the screens seen, and aren't drawn.

## Sounds

### Engine

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
closed and open; that's a guess. The mixer gained a pitch control for
this (`Mixer::setPitch`).

### Horn, clutch, tyres and knocks

- The **horn** (the second line's first wave; **H**) loops while held;
  a police car's H is its siren.
- The **clutch** (REVERSE, a 0.2 s clunk, for most; TRUCKGEARSHIFT for
  the trucks) plays at its volume on each change of gear.
- The **tyres**: `aud/cardata/player/default_surfacedry.csv` (and
  `default_surfacewet.csv` in the rain) is the tunnel's surface number,
  then for each surface (the materials' sound numbers) its rolling wave
  (NOSOUND for the road), the speed it's loudest at, its volumes and
  pitches, its skids' volumes and how many skid samples, and those with
  the slip each plays over (`tireskid1_ps1` 0.55-0.65 ...). The wheels'
  surface picks the rolling sound, by speed; their slip (sliding
  sideways or braking past the grip, at 10 m/s for 1, or spinning) picks
  the skid.
- **Knocks**: `default_impacts.csv` is "bangers" (WALL, then lights,
  signs...) each with samples by force: the wall's `Carimpactsoft3`
  from 1,000, `Carimpactmed1` from 8,000, `Carimpacthuge1` from 20,000.
  Reluva plays the wall's for the hardest knock (the impulse, read as the
  force), at most four a second.
- The **suspension**: `suspensionaudio.csv` gives `Suspension3`, a
  velocity range (2-3) and volumes (0.85-0.9). Reluva plays it when a
  wheel's suspension moves at the range's low end or faster (m/s), louder
  towards its top; at most every 0.3 s.

The waves are under `aud/aud22/` (`engines/`, `horns/`, `surfaces/`,
`impacts/`, `suspension/`), else `aud/aud11/`. `tirewobble.csv` isn't
used.
