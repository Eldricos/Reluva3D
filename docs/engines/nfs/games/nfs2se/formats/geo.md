# Need for Speed II SE: car models (`.geo`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/geo.cpp` · **Checked on:** all 53 models on the US CD · **Confidence:** confirmed unless marked

## Overview

The models of the cars, the traffic and the other vehicles and things
that race or stand on the tracks (a tank, a log, a T. rex...): 53 `.geo`
files in `GameData/Sim/CarData/cardata.viv`. Each is 32 parts in a fixed
order; their pictures are in the car's own `.qfs`
(`GameData/CarModel/pc/<name>.qfs`), by name.

The unit looks like a metre: the F355's body is 2.0 wide and 4.4 long
(the real car 1.98 m and 4.25 m), its wheels 1.7 apart *(inferred)*.

## Layout

### Header (140 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | 2 in 40, 1 in 7, 0 in 6; not worked out |
| 4 | 32 × u32 | rising by 0x40 (in 52 of the 53): memory addresses, not read |
| 132 | 8 | 0 in 38 of the 53; not worked out |

### Parts

32, one after another, each:

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | vertex count |
| 4 | u32 | polygon count |
| 8 | 3 × 4 | the part's position, 16.16 |
| 20 | 4 × u16 | not worked out |
| 28 | 3 × u64 | 0, 1 and 1, in all 1,696 parts |
| 52 | 6 each | the vertices: three s16, 256ths of the unit, from the position |
| | 6 | after an odd number of vertices |
| | 12 each | the polygons |

A polygon, always a quad: a u32 of flags, four vertex indices, and its
picture's 4-character name in the car's `.qfs`.

- **The parts.** The 16 racing cars have parts 0 to 19, the detailed
  model; every model has part 21, and the others (traffic, the other
  vehicles and things) only parts among 20 to 25. In the F355, 12 to 19
  are its four wheels in pairs (their positions ±0.85 across and 1.19
  forward, 1.48 back); 21 is the whole car at medium detail, 24 and 25 at
  low. OpenNFS names the same order: 0-19 high, 20-22 medium, 23-25 low,
  26-31 reserved *(published)*.
- **Flags.** The left-hand parts' polygons have 0x4 set where their
  mirrored right-hand ones haven't (parts 5 and 6, 7 and 8); OpenNFS
  turns such polygons' facing *(published)*. Other bits not worked out.

## In Reluva

Read as `CarModel` (`src/engines/nfs/nfs2se/formats/geo.cpp`);
`reluva-tool nfs survey` reads all 53 in `cardata.viv`, and
`reluva-tool nfs geo-obj` writes one as an OBJ.

The data viewer's Cars page (`src/engines/nfs/nfs2se/pages/car_page.cpp`)
draws each model a level at a time, its pictures from the car's `.qfs`
by name, laid on the quads as the Tracks page lays them
([trk.md](trk.md), In Reluva), both faces, lit from one side. The racing
cars' bodies come out the palette's bright green: likely where the game
puts the chosen paint, not worked out *(guessed)*.

## Sources and checks

- **Where the facts come from:** the files; OpenNFS (MIT, `LibOpenNFS`'s
  NFS2 reader, read only), the only public description, for the layout;
  no prose write-up was found.
- **How it was checked:** survey scripts over the 53 (November 2026):
  every part's counts and polygons' indices, and the padding, which
  OpenNFS gives as 6 bytes after an odd count: every model then ends
  exactly with its 32nd part, and no other padding does that; the reader
  on all 53; the unit tests (`nfs.unit.nfs2se`).

## Open questions

- The parts' four u16, the polygons' other flag bits, the header's u32.
