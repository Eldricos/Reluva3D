# Midtown Madness 2: traffic

> **Status:** partial · **Code:** `src/engines/age/game/traffic.cpp`, `traffic_lights.cpp`, `city_life.cpp`, `src/engines/age/render/car_actor.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The traffic: how much, where it drives, the lights it stops at, parked cars, London's trains, and the cars' lamps. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

### How many

The game's figure for traffic is a density: the races' Ambient column
(0-0.9; most 0.1-0.3), the lessons' AmbDensity (0-0.3), the aimaps'
`[Density]` where they have one (London's cruising `roam.aimap` 0.1, San
Francisco's lesson and stunt files 0-0.13), and per road the aimaps'
exceptions (0.00 or 1.0). The race menu shows it as a bar. None of the
files says how many cars it makes. Reluva reads it as a share of the
lanes' capacity:

- **1.0 is a lane at capacity**: about 2,000 cars an hour, the usual
  figure for a city lane, which at the city's 15 m/s speed limit is a
  car every 25 m (1.7 s apart).
- The cars wanted are the density times the lane near the player (within
  340 m, each road by its exception's share) over 25 m. So there are
  more downtown than on a bridge, and none where there's no road.
- About 12 km of lane lies within 340 m of a road point in either city
  (London's median 11.4 km, San Francisco's 13.1 km). So 0.1 makes
  about 50 cars round the player, 0.2 about 100. Measured in races:
  18-45 cars at 0.1, 48-104 at 0.2.
- At most 160 cars (Reluva's limit for drawing and steering them); the
  races at 0.5 and over reach it.
- New cars start on a piece of lane each as likely as its length.
  (Before, a lane was picked first, so short lanes were overfilled.)

| | Drive page | Race page |
|---|---|---|
| Traffic | Y steps through none, light, normal and heavy: 0, 0.05, 0.1 (the cruising file's) and 0.25 | the race's Ambient column; a cruise its `roam.aimap`'s `[Density]` (0.1; San Francisco's has none, so the same) |
| Pedestrians | 0, 0.05, 0.1 and 0.2 with those, times 300, at most 60 | the race's Peds column (mostly 0-0.1) times 300, at most 60 |
| Police | `race/<city>/roam.aimap`'s (O turns them off) | as many of the race's aimap police as its Cops column |

Before this, the density was a share of a fixed 60 cars, the same
downtown as on a bridge: a race at 0.2 had 12.

### Traffic

- **Models**: the city aimap's ambient models, picked by their shares,
  each in one of its 8 paint jobs; their size and mass from
  `tune/vehicle/<model>.aiVehicleData`. They stand on the ground at their
  origin, as the road map's lanes do.
- **Where**: new cars start on a random lane 140-300 m from the player
  (25-300 m when a page starts or the car is put back), at least 14 m
  from another car; cars further than 340 m are left out. A race's
  exceptions keep them off the roads with density 0.
- **Driving**: along the lane at its own pace, 85-105% of its road's
  speed limit (15 m/s, about 34 mph; 75% of that for the heavy ones, over
  3 tonnes: buses, lorries), accelerating at 3 m/s² and braking at 7.
  Heading is smoothed over 2 m either side, so they turn rather than snap
  at each point.
- **Junctions**: at a lane's end a car picks a lane leaving the junction
  ahead on another road (back along the same road only at a dead end)
  and crosses on a curve between the two (a Hermite curve, slowing to 6
  m/s for a right angle). What it waits for depends on the road end's
  control ([bai.md](../formats/bai.md)):
  - **Lights** (1): it stops for red, and for amber where it can still
    stop. Given its turn on green but not at the line yet, a red it can
    stop for takes the turn back.
  - **Give way** (0): it stops at the line first.
  - **None** (3), and after the above: a junction is taken by the cars
    from one road at a time, or at a crossroads from two opposite roads
    (the junction lists its roads in order round it, so opposite roads
    are two apart); the others stop at the end of their lane. Held 6
    seconds by that (not by a light), a car goes anyway.
- **Changing lanes**: held for 1.5 seconds within 25 m behind something
  at least 3 m/s slower than its pace, more than 10 m into its lane and
  40 m from its end, a car moves into a lane beside going the same way if
  there's nothing from 15 m behind to 20 m ahead there; it moves across
  over 2.5 seconds, indicating. On the Drive page always, in races where
  the aimap says (`AmbientLaneChanges`: only San Francisco's cruising
  files set it).
- **Keeping clear**: a car slows to stop 8 m behind anything within 30 m
  ahead of it and 2.2 m of its way: other traffic, the player, the
  opponents, the police. Held up 10 seconds by something that doesn't
  move, it drives on through it for 3 seconds.
- **Knocks**: a driven car bumping one (its three spheres against the
  traffic car's box, from `Size`) pushes them apart by their masses and
  bounces them; the traffic car leaves its lane, slides to a stop (6
  m/s²), spinning, and stays there.

- **Lamps**: brake lights (`TLIGHT`) when slowing; indicators (`SLIGHT0`
  left, `SLIGHT1` right, blinking) for the turn within 40 m ahead or a
  lane change; in the evening, at night and in fog and rain, headlights
  (`HLIGHT`) and tail lights, flares at the headlights (see below) and a
  pool of light on the road 9 m ahead.

Measured over five minutes of 60 cars in each city with the lights
(scratch runs, not part of the repository): an average speed of 7-9 m/s,
with 13-25 of the 60 stopped at any time, nearly all at red lights; none
entered a junction on red. Lane changes are rare in such runs (5 in San
Francisco's five minutes): cars are only held up mid-road by something
slow or stopped, such as the player.

- **Roads for traffic**: none on the road map's roads flagged 0x2 (the
  Underground, some ramps and alleys; [bai.md](../formats/bai.md)):
  the traffic doesn't start there, and turns into them only where
  there's no other way; police patrols likewise.

### Parked cars

The cities' parked-car lines ([pathset.md](../formats/pathset.md))
are filled when a page starts, about 545 cars in London and 215 in San
Francisco: nose to tail by the curb (a 2-4 m gap, as the line's end word
gives it), side by side across a car park's bays, one place in six left
empty, each in a random paint job and stood on the ground. London's are
its `sp_pcar` models, San Francisco's its traffic's lighter cars.
They're drawn within 340 m and stay put (they don't count towards the
traffic's numbers, and they aren't left out far from the player). The
traffic slows for one only if it stands within 1.6 m of its way (none
does in either city; five-minute runs in London with and without them
give the same speeds, within the runs' spread); the police and the
pedestrians don't see them. A driven car (or a train) knocks one as it
knocks the traffic: it slides to a stop, spinning, and is then kept clear
of like a knocked traffic car.

### Underground trains

London's eight train paths each carry a train of three `va_ug_l`
carriages, set on the path by two bogies 1.5 m in from each carriage's
ends. A train runs from where its path's start word puts it to the far
end and back, standing 15 seconds at each end, at up to 14 m/s,
gathering speed and braking at 1 m/s² (the path's speed byte, 2 m/s, is
too slow for a train; all of this is Reluva's). Its carriages are boxes
that driven cars bounce off (a car on the line is shoved along and out of
the way, the train unmoved), and spheres that push the traffic's cars
aside, knocking them loose. The traffic and the police stop for them as
for an open bridge. The nearest train is heard as the game's
`aud/ambient/subwaycar.csv` says: `LondonTube` looped, out to 150 m, at
0.98, louder as it speeds up. The Underground's tunnels are drawn from
the PSDL ([psdl.md](../formats/psdl.md)): walls 5.5 m high and an
arched roof 7 m up.

### Traffic lights

The cities' instance files place no traffic lights: they stand at the
road ends whose control is 1 (533 in San Francisco, 519 in London), on
the end's first point (on the curb on the right of the way in), with the
model's arm along the second point (a metre across the road) and its
glows facing the traffic. The models are the aimap's ([aimap.md](../formats/aimap.md)):
London's `sp_traflitsingle_ped_l`, a pole; San Francisco's file names
none, so Reluva uses `sp_traflitsingle_f` and, where more than two lanes
meet at the end, `sp_traflitdual_f`, whose arm is longer. The points are
placed for traffic keeping right; in London, which keeps left, Reluva
stands the pole on the other curb, as the game does: in the gameplay
footage London's poles stand on the left curb
([footage.md](../research/footage.md)).

A light's body is modelled about its middle and its glows as if it stood
on its origin ([pkg.md](../formats/pkg.md)); stood on the ground,
the San Francisco light's signal heads (2.0-7.6 m up) and its glows
(5.7-8.1 m) line up, as do London's (2.3-3.2 m and 1.7-3.8 m).

Each junction's lights take turns: at a crossroads the two opposite
roads together, elsewhere one road at a time (at least two turns). A
turn is 12 seconds green, 3 amber and 1 with every light red; each
junction starts at its own point in the cycle. The glows are the model's
`GREENGLOW`, `YELLOWGLOW` and `REDGLOW` parts, `DAY` or (at night, larger)
`NIGHT`; its pedestrian signal shows `WALK` while the road has red and
`NOWALK` otherwise. The timing is Reluva's.

### Lamps and flares

The driven cars (the player's, the opponents', the police's) show their
models' lamps in the same way: `HLIGHT` and `TLIGHT` with the headlights,
`BLIGHT` (and `TLIGHT`) braking, `RLIGHT` reversing. A police car's light
bar is its `SIREN0` and `SIREN1` glows, lit in turn.

The small square parts `HEADLIGHT0`/`HEADLIGHT1` (and a police car's
`SRN0`-`SRN3`) have plain colours and no texture: they mark where the
lamps shine from. Reluva draws a flare there, `texture/lt_glow.tga` facing
the camera and added to what's behind (1.4-1.6 m across, 2 m for sirens),
in the square's colour for sirens; headlight flares fade as the car turns
away. Headlights also light a pool of the road 9-10 m ahead (the
renderer's dynamic lights, the 16 nearest the camera).

## In Reluva

- `Traffic` and `TrafficNetwork` (`src/engines/age/game/traffic.{h,cpp}`):
  the road users and their lanes.
- `TrafficLights` (`src/engines/age/game/traffic_lights.{h,cpp}`).
- `CityLife` (`src/engines/age/game/city_life.{h,cpp}`) keeps the
  city's traffic, trains, boats and the rest together.
- `CarActor` (`src/engines/age/render/car_actor.{h,cpp}`) draws a car
  and its lamps.

## Sources and checks

- **Where the facts come from:** the road maps ([bai.md](../formats/bai.md)), aimaps ([aimap.md](../formats/aimap.md)), path sets ([pathset.md](../formats/pathset.md)); public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.city`; by hand on the Drive page.
