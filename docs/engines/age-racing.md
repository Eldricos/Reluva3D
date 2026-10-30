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
- The HUD, as the game's screens show it ([age-footage.md](age-footage.md)):
  the clock top middle; top left, lines 24 pixels apart from y 23, the
  game's words (strings 254-261) in green and their values in white:
  **Place** (against opponents), **Check** (this lap's checkpoints, or
  the race's), **Lap** in a circuit, then each lap's time ("1.
  0:45:21"), the lap under way last. After each checkpoint the time so
  far (this lap's in a circuit) shows under the car for a few seconds,
  with "Lap time" over it at a lap's end; "Final lap!" shows there too.
  "Ready...", "Set...", "Go!" and the lessons' messages are over the
  road, at y 110. The speed, gear, revs and damage meter are bottom left
  ([age-driving.md](age-driving.md)), the map bottom right. The game's
  text is Gill Sans MT (its string table names the fonts and sizes);
  Reluva uses its own font.
- The results list the racers by where they got to, with their times.
- **Traffic, pedestrians and police** are as the race's settings say
  (its Ambient, Peds and Cops columns, and its aimap's exceptions and
  police): see [age-traffic.md](age-traffic.md). Busted by the police,
  or wrecked, the race is over.

## Cruise and the stunt

The race menu's **Cruise** (`cruise`, the first of its events) is the
city with no race: no checkpoints, no clock, no countdown. "Explore the
city, become one with your vehicle, and practice up for some serious
racing, without the pressure of the clock", as the game's help puts it.

- Its conditions are the menu's (Change Race Options), always open to
  change. They start from morning, sunny, the traffic its `roam.aimap`'s
  `[Density]` gives (London's 0.1; San Francisco's has none, so the
  same: [age-traffic.md](age-traffic.md#how-many)), half the
  pedestrians and 4 police.
- Its police patrol from `race/<city>/roam.aimap` (`.aimap_p` at
  Professional, with one more in San Francisco): 19 and 20 of them in
  San Francisco and London, at places across the city. That's Reluva's
  reading of the name ("roam"); the game's own cruise isn't otherwise
  described by its files.
- It starts at one of the city's Cops & Robbers places
  (`multicopwaypoints.csv`, as a multiplayer cruise does), picked by the
  car, so its replay starts in the same place.
- Busted or wrecked, the car is put back where it started, mended.
- It plays one of the cruising tunes, as the Drive page does.
- Esc pauses; Quit to Race Menu ends it. Nothing is recorded but the
  last race run (Quick Race).

**Stunt Mode: Acceleration.** San Francisco has one more file no race
names, `race/sf/accel0waypoints.csv`, and `mmlang.dll` has a group of
messages no lesson uses (199-204): "Stunt Mode: Acceleration", "Race to
the end of the pier!", "But don't fall in!", "Go!", "Put the pedal to
the metal!", "Great job! You can stop on a dime!". Reluva puts them
together as the game's stunt, offered as Cruise's second race in San
Francisco:

- The file's five points run 1.5 km in a straight line down the hill
  to the bay (Market Street, it looks like), from 40 m up to the water's
  level, with road under all of it. Measured by casting down onto the
  city's collision every 40 m: the ground runs on some 140 m past the
  last point, then stops. That's the pier's end.
- The car starts at the first point facing the second (the file's
  first heading, 39.6 degrees, points 25 degrees off the road, into the
  buildings).
- The messages count it down (200, 201, 202), then "Put the pedal to
  the metal!".
- The points after the first are checkpoints, as in a race. Past the
  last, stopped (under 0.5 m/s) is "Great job! You can stop on a
  dime!", with the time and how far short of the pier's end. Falling in
  (3 m below the lowest point) fails, as do being busted or wrecked.
- Traffic is the cruise's (0.1) and there are no police.

**Stunt Prototype.** `race/sf/stunt0waypoints.csv` (one point, a start)
and `stunt0.aimap` (traffic 0.13, three roads without it, no police, an
opponent `vpford opp-c0.2` whose path isn't there) look like the "Stunt
Prototype" of the game's mode names (`mmlang.dll` 526, beside
"Testing"). Reluva offers it as Cruise's third in San Francisco: a
cruise from that start, with that traffic, without the opponent.

The `jump0`-`jump2` points and `ramp.pathset` aren't used (see the
Crash Course's columns below).

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
| 8 | Bonnie & Collide, Midterm 2 (SF) | "Destroy that car!", "You did it!", "Car reached destination", "Get going! Lose the tail" (616-623, 652) | ram the aimap's car until it's over or stopped 2 s (it isn't put back on its path) before it reaches its end or the time's up; no arrow till then. With police about (Midterm 2's six), then lose them to the checkpoint, as The Heat Is On |

Being busted or wrecked fails any lesson. The checkpoints' fifth column,
called "poly count" as in the circuits, varies from gate to gate here
(4-16) and is read as the gate's radius.

Event 8 had been read from the messages 210-216, which `mmlang.dll`
names "Bonnie & Collide" too ("Drive as cleanly as you can!", "Wait...5
second penalty"); but the lesson's picture says "Buckle up! It's time to
ram another car into submission!", the walkthrough footage shows the
pickup rammed until "You did it!" and Midterm 2 as "Destroy that car!"
then "Get going! Lose the tail", and 616-623 are that lesson's words. No
lesson uses 210-216.

The other extra columns: the third (1 in the follow and destroy lessons,
every lesson with an aimap car) marks the car, which the event already
says. The second (1 in Brake Dancing, "master the Forward 180", and The
Knowledge, "Race from landmark to landmark as you practice your map
skills") takes the compass arrow away: in the walkthrough footage The
Knowledge is driven from start to end without one (the Midterm before
it, with the column 0, has it throughout), the way found by the map.
The fourth (1 in San Francisco's Straight, With Chaser and Midterm 3)
isn't used: nothing in the files or the footage says what it does
(Straight, With Chaser plays as a plain follow; London's Follow That
Car!, with it 0, looks the same, neither showing a clock).

`race/sf/jump0`-`jump2waypoints.csv` are three short routes down San
Francisco's hills and along the bay (5, 6 and 4 points; `jump0.aimap`:
traffic 0.1, no police or opponents) and `ramp.pathset` one path,
"PATH01", of 13 points near `jump2`'s start, naming no model: nothing
names them as races or says what they're for, so they aren't used. A passed lesson offers the next
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
| a circuit's final lap, or only the finish left in a checkpoint race or Blitz | Cops |
| moving again (over 5 m/s) after stopping or a chase | Return |
| the race over | Results |

Each change waits for the next measure, as DirectMusic's do. The speeds
and times for idling are Reluva's. The footage
([age-footage.md](age-footage.md)) shows the rest. The beatless start
of the Start segment runs about 8-15 s. A few seconds after "Final lap!"
the tune speeds up, and the same happens after a checkpoint race's last
checkpoint. That faster groove matches the tempo of the tune's chase
segment, read from the `.sgt` files' tempo tracks: Bullet races at 140
bpm and its Cops segment is at 155, and the 0.78 s pattern heard after
Bridge Bash's last checkpoint is two of its beats. The beat stops in the
seconds after the finish, then Results plays.
Paused, the tune's Pause segment plays. Both lists' last three columns
name the **big-air motif**: GrooverStyle's `BigAir` pattern (a motif,
one measure of two parts) with the `BigAir` band. A second off the
ground (Reluva's measure of big air), it plays once over the tune, from
then, its band's instruments on its own channels.

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
the same time, with no difference at any of its marks. For that,
everything random or timed in the city starts afresh with each race:
the traffic's, pedestrians' and police's random choices from the race's
seeds, and the traffic lights' clock from 0. (The police's and the
lights' didn't at first: a cruise's replay drifted from 13 seconds in,
found by comparing the two runs frame by frame.)

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
| Rew | Backspace | back five seconds (the race run again, unseen, from its start to there) |
| Cam | the Change Camera key | through the car's own cameras, and a roadside camera |
| Save | | the replay saved, under a name ("Save as") |
| Quit | Esc | for the race just driven, first the game's "Do you want to save this replay?" |

Left and Right move along the bar, and Enter presses.

The roadside camera stands 4 m up, 45 m ahead of the car and 10 m to one
side (the other side each time), and watches it pass. Once the car is
70 m away it moves on ahead.

**Where they're kept.** Saved replays go in the save folder's `replays/`,
as `<name>.rpl`:

- "RLVRPLY", a version byte (2);
- the start, as above, and (since version 2) the conditions it was run
  in when they were changed on the race menu (laps, opponents, time,
  weather, police, traffic, pedestrians: [age-menus.md](age-menus.md#changing-a-race));
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
- **Lessons** are all open ("You can select lessons in any order", the
  game's help) but the Final, which opens once the three Midterms
  (lessons 3, 7 and 11) are passed: "You've passed all three Midterms
  and it's time for the Final Exam" (its description).
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
- `race/<city>/<city>_rewards.csv` says the same in other words: the race
  kind (`blitz`, `circuit`, `race` for Checkpoint, `crash`), `half` or
  `all` of them (a lesson's number for `crash`), the car, its paint job
  (0: the car) and the results screen's message. All twenty rows agree
  with the pictures (half is the five Blitz or Circuit races of ten, six
  Checkpoint races of twelve), but for one number: the fire truck's
  paint job is 2 there and `vpsemi_lck4` in the pictures. So a reward is
  matched to its rule by car, city and kind; when a result opens it, its
  message shows on the results screen.
- **U** in the menus opens everything, for looking round.

## Opponents

Each opponent drives its `.opp` path in the car the race's aimap gives it
([aimap.md](../formats/age/aimap.md)), aiming for its skill there as a
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
    [bai.md](../formats/age/bai.md)) instead.
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
