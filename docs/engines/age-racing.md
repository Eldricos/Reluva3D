# Racing in the Angel Game Engine

How Reluva plays Midtown Madness 2's races: the Race page
(`race_viewer.cpp`), the opponents (`opponent.cpp`) and the road graph
they route along (`road_graph.cpp`). The race files are described in
[race.md](../formats/age/race.md). As with driving
([age-driving.md](age-driving.md)), the game's own rules are in its
encrypted program; what's here is Reluva's reading of its files, to be
compared with the game.

## The Race page

A menu (city, event, race, Amateur or Professional, car) over a view
circling the race's start; Enter starts it. The race's time of day and
weather set the lighting. A three-second countdown, then:

- **Checkpoints** are the waypoints after the start, in order: within
  the gate's radius (plus 2 m) and 10 m of its height counts. Blitz and
  Checkpoint races end at the last; a circuit's lap ends back at the
  start line, for its NumLaps laps.
- **Blitz**: no opponents (the files give none), the time limit counts
  down; past it, "Time's up".
- **Circuit and Checkpoint**: against the race's opponents. Position is
  checkpoints reached, then nearness to the next.
- The game's own models mark the way: its checkpoint stand (`pt_check`)
  at both ends of the next two gates, and its HUD arrow (`hudarrow01`,
  `hudarrow_blitz01` in a Blitz) above the car pointing to the next
  checkpoint.
- The results list the racers by where they got to, with their times.

- **Traffic, pedestrians and police** are as the race's settings say
  (its Ambient, Peds and Cops columns, and its aimap's exceptions and
  police): see [age-traffic.md](age-traffic.md). Busted by the police,
  the race is over.

The game's menus and HUD pictures (`tune/widget.csv` lays out the menus'
widgets, `texture/*.tga` has their pictures, `mmlang.dll` their text)
aren't redrawn yet; nor are unlocking, Crash Course or the other modes.

## Opponents

Each opponent drives its `.opp` path in the car the race's aimap gives it
([aimap.md](../formats/age/aimap.md)), aiming for its skill there as a
share of the car's top-gear speed (0.70-0.75 at Amateur and 0.93-1.00 at
Professional in San Francisco's first race; at least 0.6). Where the
aimap doesn't say, it's every third car in the list after the player's,
at 85% (Amateur) or 95% (Professional):

- The path's points are joined along the roads by the **road graph**:
  points along the middle of every road strip and divided road, joined in
  order; a point in the middle of each block with road fans (an
  intersection), joined to the road ends within its fans' reach plus 4 m;
  and road ends within 3 m of each other. Routes are A* by distance from
  the road point nearest one path point to the one nearest the next.
  Without it the straight lines between points cut through buildings at
  every diagonal.
- The driver steers for the next point (twice the angle off it, full
  lock at 0.5 radians), counts a point reached within 14 m or once past
  it, and slows for the sharpest turn within 60 m beyond it: from 42 m/s
  for a straight down to 12 m/s for a right angle, braking at 7 m/s²
  in time. Well off its line it keeps to 9 m/s.
- Stuck (under 1.5 m/s for 2.5 s) it backs out for 1.2 s with the
  steering the other way; stuck for 6 s, or 8 s without getting 2 m
  nearer its point, it's put back at the point before facing the next,
  and after two of those at the same point, on the point itself.
- Cars bump each other: three spheres along each, pushed apart and
  bounced.

Measured over every circuit and checkpoint race of both cities at
Professional with every opponent at 95% in one car (scratch runs of 900
seconds each, not part of the repository, before the aimaps were read),
254 of the 289 opponents finish: all of every checkpoint
race's, and all but those of six circuits (San Francisco's "Floor It!",
London's "Parks But No Parking", "London Run", "Gimme Some Royalties",
"Soho Mojo" and "Zany Zigzag"), whose opponents get round, but slowly,
with many resets. The game's road map for its traffic (`.bai`, read for
the traffic now) has lanes, and would likely route them better.
