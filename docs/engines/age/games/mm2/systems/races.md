# Midtown Madness 2: races and opponents

> **Status:** partial · **Code:** `src/engines/age/game/race_screen.cpp`, `opponent.cpp`, `road_graph.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

Blitz, Circuit and Checkpoint races, and the opponents that drive them. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## The Race page

The game's menus ([menus.md](menus.md)): its splash picture,
then the main menu, the races and the Crash Course, and the garage to
choose a car, its colour and its gearbox. Where their files aren't
there, a menu of Reluva's own (city, event, race, level, car). The
race's time of day and weather set the lighting. A three-second
countdown, then:

- **Checkpoints** are the waypoints after the start: within the gate's
  radius (plus 2 m) and 10 m of its height counts. Blitz and Checkpoint
  races end at the last; a circuit's lap ends back at the start line,
  for its NumLaps laps. A circuit's are taken in order. In a Blitz or
  Checkpoint race alone the player takes them in any order, the finish
  last: "You can go through the checkpoints in any order... In a
  Checkpoint Race, the finish line does not appear until you clear all
  the checkpoints" (the game's help). The arrow points to the nearest
  left, or (S, X: Next and Prev. Checkpoint) the next or the one before
  in the list of those left. So do the opponents and the other players:
  an opponent clears any checkpoint left that it comes near, or passes
  where its path is nearest it, and finishes once it's through them all
  and the finish; in multiplayer the host checks each player's way
  against all the checkpoints it has left. Each racer keeps its own
  list of those cleared.
- **Blitz**: no opponents (the files give none), the time limit counts
  down; past it, "Time's up".
- **Circuit and Checkpoint**: against the race's opponents. Position is
  checkpoints reached, then nearness to the next.
- The game's own models mark the way: its gates, and its HUD arrow
  (`hudarrow01`,
  `hudarrow_blitz01` in a Blitz) above the car pointing to the next
  checkpoint. Each arrow has two paint jobs, its own colour and yellow:
  "The green compass arrow points to the nearest checkpoint, and turns
  yellow if it's behind you" (the game's loading tips), so it's yellow
  when the checkpoint is behind the car.
- **Gates**: `pt_check` (and `pt_finish` at the finish line) is a whole
  gate, two posts and a banner, 2 across and 1 high about its middle
  (`pt_blue`, `pt_red`, `pt_bank` and `pt_hideout` are alike, poles with
  banners). Scaled by the checkpoint's radius it spans the road, half as
  high, as on the game's screens; Reluva had drawn two of them unscaled at
  the gate's ends, and only their opaque part, so they never showed. The
  texture has alpha, so both its passes are drawn. Lessons' gates have
  the second paint job: `CCStand`, the Crash Course banner. The next two
  gates are drawn, or in a race in any order every one left.
- In a race in any order, a light blue (10, 230, 240) diamond over each
  checkpoint left, long and narrow: about 2.2 m across and 5.5 m tall,
  13 m up, drawn over everything (measured on the game's screens; no
  model or picture for it was found, so Reluva draws it).
- Over each opponent, the game's marker: a purple (192, 16, 255)
  triangle pointing down at it and, once the race is on, its place in
  `opp_icon`'s digits above (the place: the cars ahead of the player
  carry the lower numbers). Measured on the game's screens: about 1.5 m
  across and 2.3 m tall, its point 4.5 m over the car, drawn over
  everything.
- The HUD, as the game's screens show it ([footage.md](../research/footage.md)):
  the clock top middle; top left, lines 24 pixels apart from y 23, the
  game's words (strings 254-261) in green and their values in white:
  **Place** (against opponents), **Check** (this lap's checkpoints, or
  the race's), **Lap** in a circuit, then each lap's time ("1.
  0:45:21"), the lap under way last. After each checkpoint the time so
  far (this lap's in a circuit) shows under the car for a few seconds,
  with "Lap time" over it at a lap's end; "Final lap!" shows there too.
  A car put back after sinking gets "Sleep with the fishes!" (string
  30) over the road, as the footage shows off San Francisco's piers;
  in London Reluva shows "More tea, vicar?" (642, its neighbour in the
  list), not yet seen in footage.
  "Ready...", "Set...", "Go!" and the lessons' messages are over the
  road, at y 110. Through the countdown the camera comes down from high
  over the grid, looking down on the car, to its place behind it by the
  start, as all three 100% runs show for Blitz, Circuit and Checkpoint
  races ([footage.md](../research/footage.md)); Reluva's starts 28 m up and
  14 m back. The speed, gear, revs and damage meter are bottom left
  ([car-simulation.md](car-simulation.md)), the map bottom right. The game's
  text is Gill Sans MT (its string table names the fonts and sizes);
  Reluva uses its own font.
- The results list the racers by where they got to, with their times,
  a row a second as the game's fill in, the opponents by their grid
  slots ("Opponent 1"..., the game's strings 13-20).
- **Traffic, pedestrians and police** are as the race's settings say
  (its Ambient, Peds and Cops columns, and its aimap's exceptions and
  police): see [traffic.md](traffic.md). Busted by the police,
  or wrecked, the race is over.

## Opponents

Each opponent drives its `.opp` path in the car the race's aimap gives it
([aimap.md](../formats/aimap.md)), aiming for its skill there as a
share of the car's top-gear speed (0.70-0.75 at Amateur and 0.93-1.00 at
Professional in San Francisco's first race; at least 0.6). Where the
aimap doesn't say, it's every third car in the list after the player's,
at 85% (Amateur) or 95% (Professional):

- **The route.** The path's points are joined along the roads by the
  **road graph**: points along the middle of every road strip and
  divided road, joined in order; a point in the middle of each block with
  road fans (an intersection), joined to the road ends within its fans'
  reach plus 4 m; and road ends within 3 m of each other. Routes are A*
  by distance from the road point nearest one path point to the one
  nearest the next. Without it the straight lines between points cut
  through buildings at every diagonal.
  - Where that graph has no way (round Tower Bridge's north side, for
    one), the leg goes by the road map's lanes (`.bai`,
    [bai.md](../formats/bai.md)) instead.
  - A join of the graph whose straight line runs into a wall costs twenty
    times its length, so routes go round.
  - A route that goes out to a path point and straight back the same way
    (a point a little off the main road, reached up a side street) has
    that spur cut: no car turns round in a street.
- **Steering.** The driver steers for a point along its route, 8 m plus
  0.6 s of travel ahead (at most 35 m). That's pure pursuit: the wheels'
  angle for the arc through that point, from the car's wheelbase, its
  lock, and the lock `CarSim` takes off at speed. The point is brought in
  closer (but no nearer than 0.4 s) until the straight line to it passes
  within 4 m of every corner of the route on the way; a longer look cuts
  a junction's corner through its buildings. The arc is never tighter
  than the tyres hold at the car's speed (10 m/s²).
- **Speed.** Each corner in the 160 m ahead has a radius: how far the
  route's heading turns over 8 m either side of it (a junction turns over
  several short legs, not at a point). It can be taken at the speed
  where speed²/radius is 7 m/s² (at least 6 m/s). The driver brakes at
  7 m/s² to be down to each corner's speed 6 m before it, and gently
  (0.4) while steering hard, or it spins. Pointing well off its way, it
  keeps to 9 m/s.
- **A point is reached** within 14 m or once past it.
- **Stuck** (under 1.5 m/s for 2.5 s), it backs out for 1.2 s with the
  steering the other way. Stuck for 6 s, or 8 s without getting 2 m
  nearer its point, it's put back at the point before, facing the next.
  After two of those at the same point, it's put on the point itself.
- **Checkpoints.** An opponent's path doesn't always pass the race's
  checkpoints: "Round Westminster"'s misses its fifth by 121 m. Each
  checkpoint is matched to the point of the opponent's route nearest it,
  in order, laps included. The checkpoint counts when the car passes
  within its reach, or once it's gone on past that point.
- Cars bump each other: three spheres along each, pushed apart and
  bounced.

**How it was measured.** Each change was measured on the Race page
itself: a scratch harness (not in the repository) started a race and
stepped it, undrawn, for up to 900 or 1500 seconds, with its traffic,
police, bridges and props. It counted each opponent's finish and resets,
and where they reset.

- Before this version, at Professional, 62 of the 114 opponents of the
  20 circuits finished in 900 s, with 11 resets each. Seven circuits had
  no finishers.
- Now, 100 finish in 900 s, and all 114 in 1500 s (as do all 113 at
  Amateur), with about 6 resets each.
- All 147 opponents of the 24 checkpoint races finish, with about 2
  resets each.
- The long ones are the long races: "Gimme Some Royalties" and "Soho
  Mojo" are four laps of about 3.7 km (970 s); "Midtown Mayhem" has slow
  Audi TTs (933 s).
- "Underground" still resets about 24 times an opponent, in the Tube's
  tunnels and in a pile-up at a tunnel mouth at the start.

**Repeatable.** The same harness found the same race ending two ways
from one run to the next. Valgrind showed why: a body's bounds were
measured from uninitialised values (`bodyBounds` took the first vertex's
Y and Z as the minimum of themselves and whatever was in memory).
London's parked cars, sized by it, sometimes came out huge, blocking a
road. Now the same race runs the same way every time (eight runs alike),
and Valgrind finds nothing in a London or San Francisco race.

## Sources and checks

- **Where the facts come from:** the race files ([race.md](../formats/race.md)) and aimaps ([aimap.md](../formats/aimap.md)); public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.game`; every opponent of every circuit and checkpoint race finishing, in a sweep; races played by hand.
