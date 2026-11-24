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

## How it drives

- **Where:** at the road's middle, from each point's distances to its
  edges, about 10 + 0.6 v metres ahead (up to 48): the controls reach
  the car a few frames after they're written, through the ring. The points themselves
  jog sideways where the edges change (`TR00`'s 1190, 4.5 m), and a few
  forward vectors are far off (the same point's, 50°): both made the
  first versions brake hard on straights, so the road's way is taken
  from its middle's places.
- **How fast:** each bend's speed by its curve over about 15 m,
  `sqrt(grip / curve)`, and the braking (9 m/s²) there's room for before
  it; at most 102 m/s, the McLaren F1's 228 mph. A grip of 3 × 14 m/s²
  was quickest on `TR00` (4× no faster).
- **Stuck:** slow or turned round for 1.5 s, it backs off in reverse,
  steering the other way, for 1.5 s.

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

## Left open

- Its time lost: at `TR00`'s chicane before the line it still scrapes
  the left wall, down to 27 m/s; other cars aren't seen, only bumped.
- The other tracks, a whole tournament, and the knockout's later rounds.
