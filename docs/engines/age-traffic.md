# Traffic, pedestrians and police in the Angel Game Engine

How Reluva fills Midtown Madness 2's cities: the traffic
(`traffic.cpp`), the pedestrians (`pedestrians.cpp`, `ped_model.cpp`) and
the police (`police.cpp`), run together round the player by `CityLife`
(`city_life.cpp`) on the Drive and Race pages. They go by the game's
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
- **Driving**: along the lane at its road's speed limit (15 m/s, about 34
  mph), accelerating at 3 m/s² and braking at 7. Heading is smoothed
  over 2 m either side, so they turn rather than snap at each point.
- **Junctions**: at a lane's end a car picks a lane leaving the junction
  ahead on another road (back along the same road only at a dead end)
  and crosses on a curve between the two (a Hermite curve, slowing to 6
  m/s for a right angle). A junction is taken by the cars from one road
  at a time, or at a crossroads from two opposite roads (the junction
  lists its roads in order round it, so opposite roads are two apart),
  as a green light would; the others stop at the end of their lane. Held
  6 seconds, a car goes anyway.
- **Keeping clear**: a car slows to stop 8 m behind anything within 30 m
  ahead of it and 2.2 m of its way: other traffic, the player, the
  opponents, the police. Held up 10 seconds by something that doesn't
  move, it drives on through it for 3 seconds.
- **Knocks**: a driven car bumping one (its three spheres against the
  traffic car's box, from `Size`) pushes them apart by their masses and
  bounces them; the traffic car leaves its lane, slides to a stop (6
  m/s²), spinning, and stays there.

Measured over five minutes of 60 cars in each city (scratch runs, not
part of the repository): an average speed of 11 m/s, with 1-10 of the 60
stopped at any time, mostly waiting at junctions.

The game's traffic has lights, lane changes and headlights at night,
none of which Reluva does yet.

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

- **Waiting** where the aimap puts them, until the player runs into one
  or goes past within 40 m (the aimap's `CopChaseDistance` where given)
  at more than half as fast again as the speed limit (22.5 m/s, about 50
  mph).
- **Chasing**: along the road graph ([age-racing.md](age-racing.md)) to
  where the player is, routed again when the player has gone 20 m from
  it (or every 5 seconds), and within 30 m with nothing of the city in
  between, straight at a point a little ahead of the player, braking to
  box it in once it's slow; against something for 2 seconds, it backs
  out. Sirens: the model's `SRN` glows, in two pairs that alternate four
  times a second, and `police1fastsirenloop` quieter with distance.
- **Busted**: held under 2 m/s within 9 m of a chasing car for 3
  seconds. On the Drive page the car is put back at the start; in a race
  it's over ("Busted").
- **Giving up**: left more than 300 m behind for 8 seconds.

In scratch runs (a car driven away from a cop at 30 m/s, then stopped),
the cop caught up and busted it in three of four, and lost it in the
fourth.

The game's police also patrol (the comment in the aimaps speaks of
patrol routes) and talk on the radio (`mm2audex.ar`); `copchase.csv` (a
route in the waypoint files' form: 24 points in San Francisco, 7 in
London) isn't read yet.
