# Race files

A city's races: their names in `tune/<city>.cinfo`, everything else in
`race/<city>/` in `mm2core.ar`. Read by `src/engines/age/race.cpp`. Worked
out from the files (they're CSV and `Key=Value` text) and by driving them
on the Race page.

## tune/<city>.cinfo

```
LocalizedName=San Francisco
MapName=sf
RaceDir=sf
BlitzCount=10
CircuitCount=10
CheckpointCount=12
BlitzNames=Ignorance Is Blitz|Hold On Tight|...
CircuitNames=Take It Easy|Hang Time|...
CheckpointNames=Racing 101|Deck The Hall|...
MustPlace=3
UnlockGroup=3
```

Names are separated by `|`. MustPlace and UnlockGroup are read as
unlocking's: races open UnlockGroup at a time, each group once those
before it are placed at MustPlace or better
([age-racing.md](../../engines/age-racing.md)). The Crash Course's
lessons have no names here; they're in `mmlang.dll`
([mmlang.md](mmlang.md)).

## Settings: mmblitzdata.csv, mmcircuitdata.csv, mmracedata.csv

A heading line, then one row a race (as many as the .cinfo's count:
checkpoint races are the `race` files): a description (`none`), then ten
columns twice, amateur then professional:

| Column | Meaning (from the heading) | Reluva |
|---|---|---|
| CarType | | not used |
| TimeofDay | 0-3 | with Weather, picks the lighting: TimeofDay x 4 + Weather (morning, noon, evening, night; clear, cloudy, foggy, rainy) |
| Weather | 0-3 | |
| Opponents | how many | that many of the race's opponent paths |
| Cops | | how many of the race's aimap police take part ([aimap.md](aimap.md)) |
| Ambient | traffic density | times 60 cars ([age-traffic.md](../../engines/age-traffic.md)) |
| Peds | pedestrian density | times 300 people, at most 60 |
| NumLaps | | a circuit's laps |
| TimeLimit | seconds | a Blitz's time limit |
| Difficulty | | not used |

`mmcrashdata.csv` (Crash Course, 13 rows: `lesson1`, `lesson2`,
`lesson3`, `midtrm1`, ... `final13`) has the same columns; Reluva takes
each lesson's time of day and weather from it.

## Checkpoints: <blitz|circuit|race><n>waypoints.csv

```
x,y,z,a,radius,frame rate,state changes,texture changes,msg
-1501.457153,36.363750,-285.736023,92.340782,8.000000,0,0,0,
```

The first line is the start; each after it a checkpoint. `a` is a heading
in degrees: 0 faces -Z, 90 faces +X (Blitz 0 starts at 92 and its next
checkpoint is to the east; Circuit 0 starts at -90 and heads west).
`radius` is the gate's half-width in metres in Blitz and Checkpoint
races; circuits' files call that column "poly count" (11 throughout) and
Reluva gives their gates 15 m. The last three columns are 0 throughout.

## Opponents: <file><n>-<a|p>-<i>.opp

One file per opponent per difficulty (`a` amateur, `p` professional):

```
x,y,z,brake,forward offset,side offset,target speed,speed start,side start
-1700.000732,43.460361,-69.360390,91.249046,0,0,0,0,0
-1630.754150,49.914387,-66.647179,0.000000,0,0,0,0,0
-1795.783325,34.850105,-70.781670,0.000000,0,0,0,0,0
...
```

- The first line is where the opponent starts. Its fourth value is
  sometimes the heading the car leaves in and sometimes not: compared
  with the direction from the start to the path, it's off by anything
  from 0 to 180 degrees across the 600 files. Reluva faces the car at its
  path instead.
- The rest are points 70-220 m apart, at the street corners of the way
  the opponent drives. In 540 of the 600 files the first of them lies on
  the far side of the start from the second; it isn't driven to (Reluva
  leaves it out; what it's for isn't known).
- The other columns are 0 throughout.

San Francisco has 288 such files (156 checkpoint race, 119 circuit, 9
Blitz, a few for other modes), London 324.

Which car each opponent drives, and its skill, are in the race's
aimap ([aimap.md](aimap.md)).

## Crash Course: crash<n>data.csv

Each lesson (`crash0`-`crash12` in each city) has a row of its own:

```
Filename,Event,Checkpoints,TimeLimit,AmbDensity,extra,extra,extra,extra,etra,
copchase,3,1,180,0.2,0,0,0,0,0,0
```

- **Filename**: its checkpoints, `race/<city>/<Filename>.csv`
  (`longjump.csv`, `copchase.csv`, `exam1_1.csv`,
  `corner0waypoints.csv`...), as the races' waypoint files: the first
  line the start. Their fifth column, headed "poly count" as in the
  circuits, varies from gate to gate (4-16) and Reluva reads it as the
  gate's radius; the jump lessons' files head it "radius".
- **Event**: what the lesson asks (0, 2, 3, 4, 5, 7, 8, 9), read from
  `mmlang.dll`'s messages ([age-racing.md](../../engines/age-racing.md)).
- **Checkpoints**: 1 throughout. **TimeLimit** in seconds (0: none).
  **AmbDensity**: its traffic.
- The extra columns: the first is the minimum speed in mph of the event
  4 lessons (50 in San Francisco, 40 in London). The third is 1 exactly
  in the lessons whose opponent is the point of the lesson: the car to
  follow (every event 2 lesson) and Bonnie & Collide's and Midterm 2's
  (event 8); London's lessons whose aimap has an opponent only for
  company (`crash3`, `7`, `11`) have 0. The second is 1 for Brake Dancing
  (`oneeighty`: back and forth between the same two places, the same
  checkpoints over again) and The Knowledge (`map`: checkpoints across
  London); the fourth for San Francisco's `crash10` (not at
  Professional) and `crash11`. What those two do isn't known.

The lesson's `crash<n>.aimap` names its opponents' paths
(`vpbullet Follow-1.opp`, `vpford race0-a-6.opp`...): the car to follow,
or to drive among. Its `copchase` aimap lists 14 police and
`CopChaseDistance` 150.

## Cops & Robbers: multicopwaypoints.csv

In the waypoint files' form (x, y, z, a heading, then columns that are 0
throughout): 44 places in San Francisco, 46 in London, spread across the
city at the roadside, each heading along its road. Reluva plays
multiplayer Cops & Robbers between them: the gold's home and the bases
([age-multiplayer.md](../../engines/age-multiplayer.md)). Which the game
uses for what isn't in the file.

San Francisco's `cir1_strtpnts` to `cir9_strtpnts` (no extension: one
to eight rows each, the same columns with one more 0) look like start
grids, but don't match the circuits' starts by number. Not used.

## Cruise: roam.aimap

`race/<city>/roam.aimap` (and `.aimap_p`): the aimap form
([aimap.md](aimap.md)) with no race beside it: police (19 in San
Francisco, 20 at Professional; 20 in London), no opponents, and in
London the traffic's density and types. Reluva reads it for the race
menu's Cruise ([age-racing.md](../../engines/age-racing.md#cruise-and-the-stunt)).

## The stunt: accel0waypoints.csv

San Francisco's `accel0waypoints.csv`: five points in the waypoint
files' form, in a straight line from (-1284, 40, 854) down to (-1, -1,
-4) by the bay, the fifth column 9-15 (a radius, as in the lessons).
`mmlang.dll`'s messages 199-204 name it "Stunt Mode: Acceleration";
Reluva runs it as Cruise's second race there.

Not read: `stunt0waypoints.csv` (one point), `stunt0.aimap`,
`jump0waypoints.csv`-`jump2waypoints.csv`, `jump0.aimap` and
`ramp.pathset`.
