# Racing in the Angel Game Engine

How Reluva plays Midtown Madness 2's races: the Race page
(`race_viewer.cpp`), the opponents (`opponent.cpp`) and the road graph
they route along (`road_graph.cpp`). The race files are described in
[race.md](../formats/age/race.md). As with driving
([age-driving.md](age-driving.md)), the game's own rules are in its
encrypted program; what's here is Reluva's reading of its files, to be
compared with the game.

## The Race page

The game's menus ([age-menus.md](age-menus.md)): its splash picture,
then the main menu, the races and the Crash Course, and the garage to
choose a car, its colour and its gearbox. Where their files aren't
there, a menu of Reluva's own (city, event, race, level, car). The
race's time of day and weather set the lighting. A three-second
countdown, then:

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
  or wrecked, the race is over.

## Crash Course

Each city's 13 lessons (`race/<city>/crash<n>data.csv`,
[race.md](../formats/age/race.md)), named in `mmlang.dll`
([mmlang.md](../formats/age/mmlang.md)): San Francisco's "Golden Gate
Stunt Driving School" (Frequent Flyer, Hit or Miss, Turn It Up!, Midterm
1, ... Final) and London's "East End Cab Company School of Driving"
(London Leap, Cutting Corners, ...). A lesson is driven through its
checkpoints (its own file's), in its row's time limit, from the first
point; its time of day and weather are `mmcrashdata.csv`'s row; its
traffic is its own row's AmbDensity; the car to follow (or drive among)
is its aimap's opponent.

What each lesson asks isn't written down beside it: the event number
picks a group of `mmlang.dll`'s messages (ids 192-245), and Reluva reads
the rules from those:

| Event | Lessons | Messages | Reluva's rule |
|---|---|---|---|
| 0 | Frequent Flyer, London Leap, River Dancing | "Take the jump to hit all checkpoints", "Good jumping!" | every checkpoint in time |
| 2 | Straight, With Chaser, Follow That Car!, Midterm 3 (SF), Final (London) | "Follow that car!", "You Won!", "Car escaped!" | stay within 120 m of the car (its aimap opponent) until the time's up, or until it's at its end with you near; farther, "Car escaped!" |
| 3 | The Heat Is On, Artful Dodging | "Practice evading your pursuers!", "Lose your pursuers before you finish!", "You survived the gauntlet!" | all of the aimap's police chase from the start; the end counts once none is chasing; busted fails |
| 4 | Turn It Up!, Cutting Corners, Midterm 1, Final (SF) | "Maintain %.0f through the checkpoints", "You have not maintained the minimum speed!" | every checkpoint at the row's first extra column (50 or 40 mph) or faster |
| 5 | Keep It Clean, Round You'll Go | "Don't even touch another car!", "You scraped the paint!" | touching a car (the traffic's or the police's) fails |
| 7, 9 | Hit or Miss, Brake Dancing, About Face, The Weaver's Tale, The Knowledge, ... | "Ready... Set... Go!", "Time's up!" | every checkpoint in time |
| 8 | Bonnie & Collide, Midterm 2 (SF) | "Drive as cleanly as you can!", "Wait...5 second penalty" | each knock (an impulse over 1,500, a second apart at most once) adds 5 seconds to the time |

Being busted or wrecked fails any lesson. The checkpoints' fifth column,
called "poly count" as in the circuits, varies from gate to gate here
(4-16) and is read as the gate's radius. The other extra columns (1 in
Brake Dancing's and The Knowledge's second, Bonnie & Collide's and the
follow lessons' third) aren't used. A passed lesson offers the next
(Enter), any lesson its restart (R).

## Music

The game's DirectMusic music ([directmusic.md](../formats/directmusic.md)),
played by `music.cpp` on Reluva's DirectMusic player and DLS synthesizer.
A race picks one of the five race tunes of
`aud/dmusic/csv_files/singlerace.csv` (Enemy, Bullet, Duck, London,
PimpHand), the Drive page one of the four cruising tunes of
`singleroam.csv` (SunRoof, TacoLoco, LeTigre, BackOff); which one is
Reluva's choice, by the race (or city and car). Its segments follow the
play:

| State | Segment |
|---|---|
| started | Start |
| stopped (under 1.5 m/s) for 4 s | Idle, or Idle Cops if chased |
| chased by the police, moving | Cops |
| moving again (over 5 m/s) after stopping or a chase | Return |
| the race over | Results |

Each change waits for the next measure, as DirectMusic's do. The speeds
and times are Reluva's; the game's own rules for switching aren't known.
The tunes' pause segment and the big-air motif have no use yet (the pages
have no pause, and jumps no music).

## Replays

The game's instant replay, and replays kept: Reluva's own files
(`engines/age/replay.h`).

**What a replay is.** It's the race run again, not a film of it. Every
race (and lesson) is recorded as it's driven:

- what it was started with: the city, the race, the level, the car and
  its colour, and the options that change how the car drives or what's
  in the city (auto reverse, steering sensitivity, pedestrians shown);
- each frame's time step and the keys held.

Played back, the race is started as it was and stepped with those same
times and keys. Everything else follows from the race's seeds and the
player's driving: the opponents, the traffic, the pedestrians, the
police, the props knocked down. So it all comes back as it was, for a
few bytes a frame (a minute and a half is about 9 KB).

**Checking it.** Every 60 frames the recording keeps where the player's
car was. A replay whose car isn't within 5 cm of the place says it "has
gone its own way". That would happen if Reluva's driving or city changed
since the replay was saved. A Blitz driven, replayed at once, saved, and
loaded again from the main menu ran its 105 seconds to the same win at
the same time, with no difference at any of its marks.

**Watching.** After a race, **I** starts the instant replay of it. The
main menu's INSTANT REPLAY (`main_rpl`) opens the game's **Replay**
dialog (`jpg/rpl_bk`, "Choose a replay file"), which lists the replays
saved, with LOAD (`RPL_LOAD`), EDIT (`rpl_edit`: renamed in the game's
"Rename Replay" dialog, `jpg/REDIT_BK`), DELETE (`RPL_DEL`) and CANCEL.

**The replay's bar** has the game's REPLAY MENU words (`mmlang.dll`
483-490):

| Button | Key | What it does |
|---|---|---|
| Play / Stop | Space | runs or stops it |
| Step | `.` | one frame on |
| Rew | Backspace | back to the start |
| Cam | the Change Camera key | through the car's own cameras, and a roadside camera |
| Save | | the replay saved, under a name ("Save as") |
| Quit | Esc | for the race just driven, first the game's "Do you want to save this replay?" |

Left and Right move along the bar, and Enter presses.

The roadside camera stands 4 m up, 45 m ahead of the car and 10 m to one
side (the other side each time), and watches it pass. Once the car is
70 m away it moves on ahead.

**Where they're kept.** Saved replays go in the save folder's `replays/`,
as `<name>.rpl`:

- "RLVRPLY", a version byte;
- the start, as above;
- the frames (each a float time step, the keys, a gear change, a flag);
- the marks.

Multiplayer games aren't recorded: the other players' cars come over
the network, not from the race's seeds.

## The record and unlocking

Reluva keeps what each driver has done in a record of their own in the
game's save folder. A record holds:

- each race's best place and best time at each level (a Blitz finished
  in time counts as first);
- the lessons passed;
- the last race run (the main menu's Quick Race starts it again).

The drivers are listed in `drivers.txt` (each one's record file, level
and name). The first driver keeps `record.txt`, the record from before
there were drivers; each new one gets a `record-<n>.txt`. These files
are Reluva's own; the game's driver files (`.plr`) aren't read or
written. The drivers are chosen, created and deleted on the main menu
([age-menus.md](age-menus.md#drivers-and-records)).

- **Races** open three at a time (the `.cinfo`'s UnlockGroup), each
  group once every race before it is placed at its MustPlace (3rd) or
  better at that level (a Professional result counts for Amateur too).
  That's Reluva's reading of the two keys.
- **Lessons** open one after another, each once the one before is
  passed.
- **Cars and paint jobs**: the game's pictures say what opens each
  (`jpg/<car>_lck.jpg` for a car, `_lck<n>` for paint job n, with `_p`
  for a Professional's), and Reluva follows them: "To unlock this
  vehicle, place 1st, 2nd, or 3rd in six San Francisco Checkpoint
  Races" (Amateur), "... finish first in six ..." (Professional),
  "... pass London Crash Course Midterm Exam Two", "... successfully
  complete the San Francisco Crash Course" (its final lesson). Eight
  cars have them (the Audi TT, the new Mini Cooper, the Light Tactical
  Vehicle, the DB7, the New Beetle Dune, the Panoz GTR-1, the fire truck
  and the New Beetle RSi) and twelve paint jobs (one each of the Audi
  TT, the Mustang Fastback, the London cab, the new and the classic Mini,
  the double-decker, the Dune, the GTR-1 and the fire truck, three of
  the RSi). Every other car and paint job is open. The `.info` files'
  UnlockScore and UnlockFlags (set on six cars) aren't
  used: the pictures cover more.
- **U** in the menus opens everything, for looking round.

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
