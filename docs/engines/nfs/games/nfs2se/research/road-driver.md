# Need for Speed II SE: a driver for the hosted game (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026: the PLAN's last open item of Phase 6,
a tournament or knockout driven well enough to place, which needs
something to steer. `reluva-tool nfs host -roaddrive` drives the player's
car along the virtual road (`src/engines/nfs/nfs2se/host/road_driver.cpp`),
as NFS III's `-drive CAR:road` does its own.

## What it takes from `nfs2sen.exe`

Worked out from the code and checked on the hosted game, each by a run
that printed or changed it:

- **The controls:** the local player's, sampled from the keys into a
  record at 0x4ddb10 (s8 steering, negative left; u8 gas; u8 brake; u8
  buttons, 0x02 shift up and 0x01 down: the automatic box steps through
  reverse, neutral and drive), go each frame into a ring of every
  player's (0x448720), on a link over it too
  ([network-play.md](network-play.md)). Each car's input function
  (`car+0x370`, 0x45b3a0) takes its player's back from the ring into
  0x4ddb18 (0x45e8a0), and 0x445440 into the car (`+0x2e0`, `+0x2d4`,
  `+0x2d5`). The driver puts a native function at the call of 0x448720
  (0x4403a9): it writes the record, then calls the game's own. No
  instruction trace is needed: the race runs at the interpreter's full
  speed. (A first version wrote 0x4ddb18 after the car's input
  function: alone it drove as well, but its controls never reached the
  other copy on a link.)
- **The cars:** pointers at 0x4dcbe8 by slot, the player's slot at
  0x4dd2ec, the race's cars at 0x4dcf64. A car's place `+0x9c`/`+0xa4`
  (16.16 metres), its right and forward rows `+0xc4` and `+0xdc`, its
  speed `+0x2b8` (m/s), its road point `+0x14`, gear `+0x2da`, laps
  `+0x1f8`, progress `+0x220`, finished `+0x280` (2).
- **The road:** the `.col`'s points, at `[0x4dc9d8]`, their count at
  `0x4dc9d0` ([../formats/col.md](../formats/col.md), The virtual road).
- **The race:** its laps at 0x4dd2dc (poked to 1 for short tests), its
  standings at 0x4ffdec (16 bytes a car: slot, progress, time, human),
  sorted a moment after a car crosses the line.
- **The computer cars' speeds:** a table at `[0x4c2b08]`, a u16 each two
  road points, set up with the track (0x42d530, 0x42d590); its low byte
  is the speed the computer cars aim for there, in m/s, read at 0x4258a5
  with the point halved, and capped by their car's top speed (a table at
  0x4c32e0). Beside it, at `[0x4c2b0c]`, a byte a point: the slot across
  the road they keep to, 0 for none *(inferred)*.

The computer cars themselves are no help to ours: they are moved along
the road at their speed, with no controls, and don't skid, so their
code has nothing to steer a car with; and it draws from the game's one
random number generator (0x4c3de4), so calling it for our car would
change the game's sequence on one copy only, against the replay and
the other copies on a link *(inferred)*. Their speeds, only read, are
safe.

## How it drives

- **Where:** at the road's middle, from each point's distances to its
  edges, about 10 + 0.6 v metres ahead (up to 48), but no farther than
  where the road has turned 30° (round Pacific Spirit's hairpin at its
  point 281 it aimed across the inside, into the wall): the controls reach
  the car a few frames after they're written, through the ring. The points themselves
  jog sideways where the edges change (`TR00`'s 1190, 4.5 m), and a few
  forward vectors are far off (the same point's, 50°): both made the
  first versions brake hard on straights, so the road's way is taken
  from its middle's places.
- **How fast:** the computer cars' speed at the road's point, the
  slowest of the next five (its controls reach the car a few frames
  late), at most 102 m/s, the McLaren F1's 228 mph. The game knows its
  bends better than our guess did: each bend's speed by its curve over
  about 15 m, `sqrt(grip / curve)`, and the braking (9 m/s²) there's
  room for before it, now used only where there's no table. Taking the
  lower of the two changed nothing on `TR00`: the guess, at the grip of
  3 × 14 m/s² that was quickest, was never the lower.
- **Slowed on full lock:** steering full lock and still more than 0.45
  rad off its aim for 0.3 s above 15 m/s, it slows 3 m/s under its speed
  until it turns. The computer cars, which can't skid, take a few bends
  faster than a car can: Mystic Peaks' at its point 110 at 63 m/s, where
  a grip of 3 × 14 m/s² allows 50, and it ran wide into the wall.
  Slowed at once, it lost time wherever its aim jumped; capped by its
  own estimate of the bends instead (on the edges' middle), it stopped
  less on Mystic Peaks but was a fifth slower on the Outback, the
  estimate seeing bends that aren't there.
- **Where the road splits** round something (its lane mask's drivable
  slots with a gap between: [col.md](../formats/col.md), Where a car can
  drive), at the middle of the slots on the right, as the game's own
  drivers keep to their side (0x43a9a0). `TR02`'s rock pillars stand on
  its road's middle; aiming there, both cars stopped against them. A car
  already well inside the slots on the left keeps to them: put there by
  a pass, it pressed against the Outback's median (its points 1662 to
  1690) for 17 s, steering for the other side.
- **Off what stands in the road:** where a slot stops being drivable in
  the next 12 points, the line moves to the middle of the nearest slot
  that stays drivable. Mystic Peaks' rock columns stand in the first
  slot right of its points 1797 and 1806, on the line the road's edges
  gave; it stopped against them twice a lap, and still caught them when
  moved only to the slot's edge and half a car and a little more.
- **Round other cars:** one up to 40 m ahead, within 2.5 m of its line,
  and slower (or within 15 m), it moves its whole line 3 m to the side
  of it nearer its own, at 4 m/s across, and holds it 1.5 s after the
  car is no longer ahead, so as not to turn back into it alongside;
  with no room on either side, it follows at the car's speed. Its line
  is kept to the road's lanes (bytes 31 to 33, less 1.2 m), not its
  edges: those take in verges and car parks. Aimed 3 m past the car at
  one point, as far as the edges, it swerved at 64 m/s into Pacific
  Spirit's car park at its point 275, into the signposts standing in it,
  and was there half a minute a lap.
- **Its road point** is the game's (`car+0x14`), but not over a jump of
  more than 30 points to one it's more than 25 m from: off the road
  where it passes near itself, the game's moves between the two parts
  as the car does (Pacific Spirit's points 278 and 372, some 70 m
  apart), and aimed at each by turns, it backed off and on for half a
  minute of a knockout's round.
- **Where it was stuck before** in the race, from the first back off
  there on, it comes in 15% slower than it did (and 15% slower again
  each time it's stuck there after), with the braking there's room for
  before it: on Mystic Peaks the bends at its points 120 and 250, where
  it hit the wall every lap, a stop a race or so instead. And it moves
  its line 4 m to the other side of the road's line from where it was
  stuck (2 m more each time after), from 40 points before the place to
  10 after: what it hits there may not be in the road's data at all.
  Pacific Spirit's signposts at its point 278 stand where the road
  forks round its car park, on the virtual road's middle, its slots all
  drivable; laps there of 140 to 145 s, 2 s a race backing off instead
  of 18. (Keeping passes to a lane from the road's line didn't help
  there, and cost Mystic Peaks 10 s a lap: the car wasn't passing.)
- **Stuck:** slow (under 5 m/s), turned round, or no farther round the road for 3 s
  (against a wall its wheels spin, and its speed doesn't stay low), for
  1.5 s more (not timed while it backs off), it backs off in reverse for
  2.5 s at full lock the other way, then aims to one side: 3 m, then the
  other side, then 6 m and 9 m, each held a second longer, until it's
  30 points farther on. A shift is a press, not a hold: held,
  the gear stopped at neutral, short of reverse, so the button goes
  every other frame. A road sign on the inside of a `TR02` bend held
  both cars until all of these were in.

## Races run

All on Proving Grounds (`TR00`) in the McLaren F1, on a clock of
instructions (`-deterministic 300000`):

| Race | Driver | Result |
|---|---|---|
| Single, 1 lap, 8 cars | first version: angle rule, at the points | 8th, 1:37.40 (the winner 1:02.37) |
| Single, 1 lap | edges' middle, curves by forward vectors | 8th, 1:30.37 |
| Single, 1 lap | grip 3 | 6th |
| Single, 1 lap | the road's way from its middle | **2nd, 1:04.25** (the winner 0:59.87), 228 mph |
| Knockout, round 1, 2 laps, 9 cars | the same | **7th, 2:04.53; advances** (the knockout standings: Klutch, 9th, eliminated) |

The knockout was set up by the keys alone: Enter (the title movie),
Enter (*game setup*), Down, Right, Right (*race type*: knockout), four
Downs and Enter (*DONE*), two Downs and Enter (*RACE*); after the race,
Enter three times through the summary and lap times to the standings.

Later, through the ring, the one-lap race again 2nd (92.1 s); and two
copies on a serial link, both driven, to the race's end
([network-play.md](network-play.md)).

After the driver learnt to get free (late November 2026), the same
knockout round, now the game-files test's `drive` step
([TESTING.md](../TESTING.md)): laps of about 65 s, 9th of 9, the last
place eliminated; the knockout's cars quicker than the driver. Aiming as
far as a 45° turn of the road instead of 30° made it 8th, but the race
slower.

A single race of 4 laps on each track in the McLaren (late November
2026): before the driver took the computer cars' speeds, after, and
then with the passing, the slots kept clear and the sides of a split
road above. Laps after the first, which has the start; places at the
end, or at the last lap run.

| Track | Before | Place | Their speeds | Place | Now | Place |
|---|---|---|---|---|---|---|
| Proving Grounds | about 65 s | 5th to 6th | 53 s | **1st** | 53 s | **1st** |
| Outback | 148 s | 8th | 105 to 124 s | 5th | 94 to 96 s | **1st** |
| Last Resort | 133 s | 1st to 2nd | 120 s | **1st** | 120 s | **1st** |
| North Country | 170 to 174 s | 6th | 137 s | **1st** | 133 to 135 s | **1st** |
| Pacific Spirit | 209 to 273 s | 8th | 166 to 198 s | 4th after 3 laps | 135 to 169 s | 3rd |
| Mediterraneo | 134 to 136 s | 4th | 114 to 126 s | **1st** | 118 to 120 s | 4th |
| Mystic Peaks | 178 to 188 s | 4th to 5th | 179 to 190 s | 6th | 173 to 180 s | **1st** |

The knockout's first round, the `drive` step: 9th of 9 before, 4th
with the computer cars' speeds (a lap of 57 s), now 3rd (53 s).

A whole knockout (late November 2026), the driver through every round,
the keys between rounds sent as the screens came (the summary's name,
*continue* twice, *next race*), 2 laps a round:

| Round | Track | Place |
|---|---|---|
| 1 | Proving Grounds | 3rd of 9 |
| 2 | Outback | 3rd of 8 (7th in an earlier run) |
| 3 | Last Resort | 4th of 7 (6th) |
| 4 | North Country | 5th of 6 |
| 5 | Pacific Spirit | 5th of 5, eliminated |

On Pacific Spirit its second lap was as in a single race (143 s), the
first lost to backing off walls among the other cars (21 s in reverse).
The game then went back to its main menu. In the earlier run the
program ended itself as round 5 loaded (exit code 1, no message, after
its loading picture), and in two of three runs more, the keys sent at
other moments. From a memory dump at its end, its last call was the
exit at 0x45d0a0: before a race the program sets a counter going on its
timer (0x4402f0, one more each two of the timer's ticks, 128 a second,
by 0x440340), loads the race, and quits if the counter is still 0
(0x45cee0); the dump had 0. The load took 15 ms of the instruction
clock, about two ticks, as the files read took none: on a machine of the
time its reads alone take a second or more. The host now counts a
file's bytes read (and mapped) at 2,000 a millisecond, a hard disk of
the time (`GameHost::Options::diskBytesPerMs`), on the instruction
clock for NFS II SE: the load then takes about 1.5 s, the counter 14
when checked, and 15 or 16 at each round's in two knockouts after.
Both of those were out in the third round (7th of 7 at Last Resort),
so a fifth round hasn't been loaded with it.

Slowed on full lock (late November 2026), laps of 53 s on Proving
Grounds, 92 to 95 s on the Outback (2nd), 138 to 145 s on
Pacific Spirit (steadier), and 175 to 191 s on Mystic Peaks (1st).

## Left open

- Its time lost getting free of walls on Mystic Peaks: a stop or two a
  race still round its points 120 and 250 (laps of 174 to 179 s, 1st);
  round 250 the road's left edge moves from 6 to 17 units out and back,
  its middle with it, and the line.
- The knockout, against the game's quickest cars: out in the second
  round in the latest run (8th of 8 on the Outback, its second lap 110 s
  against 93 s in a single race), after a crash 4.6 m into a pass at
  90 to 100 m/s (point 1462), and slow through the split road's
  stretches (points 1663 to 1770, 37 to 52 m/s); in the two before, the
  third.
- The computer cars' slots across the road (`[0x4c2b0c]`): zero at
  every point of the tables the single races loaded, so not taken.
