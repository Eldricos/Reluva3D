# Traffic, pedestrians and police in the Angel Game Engine

How Reluva fills Midtown Madness 2's cities: the traffic
(`traffic.cpp`), its lights (`traffic_lights.cpp`), the pedestrians
(`pedestrians.cpp`, `ped_model.cpp`), the police (`police.cpp`) and the
cars' lamps (`car_actor.cpp`), run together round the player by
`CityLife` (`city_life.cpp`) on the Drive and Race pages. They go by the game's
road maps ([bai.md](../formats/age/bai.md)), its AI settings
([aimap.md](../formats/age/aimap.md)) and its pedestrian files
([ped.md](../formats/age/ped.md)). How the game itself drives them is in
its encrypted program; the rules below are Reluva's, to be compared
with the game.

## How many

| | Drive page | Race page |
|---|---|---|
| Traffic | Y steps through none, light, normal and heavy: 0, 15, 30 and 60 cars | the race's Ambient column (0-0.6) times 60 |
| Pedestrians | a fifth of the traffic's share times 300, at most 60 | the race's Peds column (mostly 0-0.1) times 300, at most 60 |
| Police | `race/<city>/roam.aimap`'s (O turns them off) | as many of the race's aimap police as its Cops column |

The aimaps' `[Density]` (cruising in London: 0.1) isn't used; how it maps
to a number of cars isn't known.

## Traffic

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
  control ([bai.md](../formats/age/bai.md)):
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

## Traffic lights

The cities' instance files place no traffic lights: they stand at the
road ends whose control is 1 (533 in San Francisco, 519 in London), on
the end's first point (on the curb on the right of the way in), with the
model's arm along the second point (a metre across the road) and its
glows facing the traffic. The models are the aimap's ([aimap.md](../formats/age/aimap.md)):
London's `sp_traflitsingle_ped_l`, a pole; San Francisco's file names
none, so Reluva uses `sp_traflitsingle_f` and, where more than two lanes
meet at the end, `sp_traflitdual_f`, whose arm is longer. The points are
placed for traffic keeping right; in London, which keeps left, Reluva
stands the pole on the other curb. Whether that's what the game does
isn't known.

A light's body is modelled about its middle and its glows as if it stood
on its origin ([pkg.md](../formats/age/pkg.md)); stood on the ground,
the San Francisco light's signal heads (2.0-7.6 m up) and its glows
(5.7-8.1 m) line up, as do London's (2.3-3.2 m and 1.7-3.8 m).

Each junction's lights take turns: at a crossroads the two opposite
roads together, elsewhere one road at a time (at least two turns). A
turn is 12 seconds green, 3 amber and 1 with every light red; each
junction starts at its own point in the cycle. The glows are the model's
`GREENGLOW`, `YELLOWGLOW` and `REDGLOW` parts, `DAY` or (at night, larger)
`NIGHT`; its pedestrian signal shows `WALK` while the road has red and
`NOWALK` otherwise. The timing is Reluva's.

## Lamps and flares

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

## Pedestrians

- **Models**: the city aimap's pairs, the second (dressed for rain) when
  the weather is rainy; each in one of its 48 paint jobs.
- **Where**: on the road map's sidewalks, 70-120 m from the player (5-120
  m when starting), each a little to one side of the sidewalk's middle;
  left out beyond 140 m and drawn within 120 m.
- **Walking**: along the sidewalk at the walk's own speed (1.4 m/s for
  the men), and at its end on along another sidewalk starting or ending
  within 12 m (round the corner), else back.
- **Diving**: a car coming at one (over 3 m/s, passing within 2.4 m in
  the next 1.6 seconds) makes them dive away from its way (the walk to
  dive left or right, then onto the ground and up again, as the moves'
  table chains them), carried by the animations' own movement; up again,
  they walk on from the nearest point of their sidewalk. They aren't hit.

## Police

- **Starting** where the aimap puts them (their models too: `vpcop`, and
  in some Crash Course lessons sports cars, `vpauditt`, `vpdb7`,
  `vpmustang99`).
- **Patrolling** (on the Drive page): from the nearest lane going their
  way, along the traffic's lanes and on through the junctions, turning at
  random (not back along the same road where there's another way), a
  kilometre planned at a time, at the speed limit. They stop for a light
  that isn't green at the next lane end within braking distance, and
  keep 18 m behind anything ahead within 2.5 m of their way (the
  traffic, the player, the opponents, each other); held up like that the
  driver isn't asked, so it doesn't take the wait for being stuck. In
  races they wait where they're put instead (those places look chosen
  along the race's way). The aimaps give no patrol routes: their comment
  names a "Patrol Route" field, but the four numbers after a police car's
  heading (`0 15 0.5 50` in most lines; also `0 15 0 50`, `0 15 1 100`
  and `0 15 0 70`) don't say which car patrols or where.
- **Noticing** the player run into one or go past within 40 m (the
  aimap's `CopChaseDistance` where given) at more than half as fast
  again as the speed limit (22.5 m/s, about 50 mph).
- **Chasing**: along the road graph ([age-racing.md](age-racing.md)) to
  where the player is, routed again when the player has gone 20 m from
  it (or every 5 seconds), and within 30 m with nothing of the city in
  between, straight at a point a little ahead of the player, braking to
  box it in once it's slow; against something for 2 seconds, it backs
  out. Its light bar's two halves (`SIREN0`, `SIREN1`) and flares at its
  `SRN` squares, blue then red, alternate four times a second.
- **Busted**: held under 2 m/s within 9 m of a chasing car for 3
  seconds; `arrest` plays. On the Drive page the car is put back at the
  start; in a race it's over ("Busted").
- **Giving up**: left more than 300 m behind for 8 seconds, then back on
  patrol (on the Drive page) from where it is.

Measured over two minutes of San Francisco's and London's cruising
police on patrol, the player parked (scratch runs, not part of the
repository): every car patrolling, 830-900 m driven on average (the
slowest 221 m, held at lights and behind traffic), 11-12 put back after
being stuck, and 2 lights crossed on red in each city (turning red with
the car too close to stop). In chases (a car driven away from a cop at
30 m/s, then stopped), the cop caught up and busted it in three of four,
and lost it in the fourth.

### Sirens

The sirens follow the game's siren tables
(`aud/cardata/opponent/policesiren.csv`, and the player's
`aud/cardata/player/sfpolicesiren.csv` and `londonpolicesiren.csv`):
each names looped recordings, and for each, lines of "play time, next
index": how long to play it before going on to which. Reluva takes a
recording's lines in turn each time it comes round. The computer-driven
police in San Francisco use the opponents' table (`police1fastsirenloop`,
`police2hilowloop`, `police1whailerloop`); in London, which the
opponents' table has no recordings for, the player's London table
(`siren_london` and `siren_london2`, 15 seconds each). The nearest
chasing car's siren is heard, quieter with distance, out to 250 m.

Driving a police car on the Drive page (the menus' "Ford Mustang Cruiser",
`vpcop`), H turns its siren on and off: its light bar and flares, and the
player's table for the city.

### No radio

The game's audio has no police radio to play. `mm2audex.ar` and
`mm2aud.ar` hold engine and horn recordings, effects, the announcers'
speech (`aud11/as1`-`as5` for San Francisco, `al1`-`al6` for London: race
starts, results, each car, the weather and time of day) and the Crash
Course instructors' (`ccs`, `ccl`); the only police-related speech is the
announcers' for Cops and Robbers, a multiplayer mode
(`spchdata/CNRSF.csv`, `CNRLONDON.csv`: who has the gold, who dropped
it), and their lines about the police car itself
(`vpcop_results.csv`). The police sounds are the sirens and `arrest`.
`copchase.csv` (24 points in San Francisco, 7 in London, in the waypoint
files' form) is the way for a Crash Course lesson (San Francisco's
lesson 5, London's lesson 10, whose `crash<n>data.csv` names it), not a
patrol.
