# Midtown Madness 2: the car's sounds

> **Status:** done · **Code:** `src/engines/age/audio/engine_sound.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

The car's engine, horn, clutch, tyre and knock sounds. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

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
closed and open; that's a guess. The mixer's rate control plays
the pitch (`Mixer::setRate`).

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

## In Reluva

`CarSounds` (`src/engines/age/audio/engine_sound.{h,cpp}`) plays a
car's sounds: `EngineSound`, `SurfaceSounds` for the tyres and `ImpactSounds` for the knocks. The volume read from the
throttle and the one knock sound for the hardest knock are Reluva's
(above).

## Sources and checks

- **Where the facts come from:** `aud/cardata/player/<car>.csv`, the tyres' and suspension's sound tables, the surfaces' sound numbers.
- **How it was checked:** `age.unit.driving`; by ear on the Drive page.
