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

Names are separated by `|`. MustPlace and UnlockGroup aren't used yet
(unlocking, presumably).

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

`mmcrashdata.csv` (Crash Course, 13 rows: `lesson1`...) has the same
columns; its races aren't played yet.

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

## Not read yet

`.pathset` (paths for the ferry, the bridge, sailboats and the like, it
seems), `crash*data.csv` (Crash Course), `copchase.csv`, the `exam*`
files and the other modes' files.
