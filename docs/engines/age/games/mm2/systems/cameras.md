# Midtown Madness 2: cameras and the dashboard

> **Status:** done · **Code:** `src/engines/age/ui/view_controls.cpp`, `src/engines/age/render/dashboard.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The near, far and in-car cameras, and the dashboard seen from inside the car. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

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

## Sources and checks

- **Where the facts come from:** `tune/camera/*.camTrackCS` and `.camPovCS`, `<car>_dash.pkg` and `.asnode`; public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** the cameras compared with the recordings by eye; `age.unit.driving`.
