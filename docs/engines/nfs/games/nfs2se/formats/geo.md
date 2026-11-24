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
| 0 | u32 | 2 in 40 (every car and traffic model, and the props `SOU2`, `SOU3`, `WAGO`, `sm45`, `sm46`, `sm48`), 1 in 7, 0 in 6 (props of one part); neither program reads it: the loader starts at 4 (0x459dc3) and nothing else touches it *(inferred)* |
| 4 | 32 × u32 | room for the parts' addresses, which the loader writes (0x459dc3); the files hold their maker's (rising by 0x40 in 52 of the 53) |
| 132 | u32 | room for the model's first entry in the program's table of texture corners (0x461dd4) |
| 136 | u32 | room for its picture bank's handle (0x461da4) |

The words at 132 and 136 are 0 in 38 of the files; in the others,
files whose part addresses start alike hold the same bytes: the maker's
leftovers, overwritten.

### Parts

32, one after another, each:

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | vertex count |
| 4 | u32 | polygon count |
| 8 | 3 × 4 | the part's position, 16.16 |
| 20 | 2 × u32 | room for the addresses of its vertices and polygons, written as the model loads (0x459dd8, 0x459e00); 0 in every part without them |
| 28 | 3 × u64 | 0, 1 and 1, in all 1,696 parts |
| 52 | 6 each | the vertices: three s16, 256ths of the unit, from the position |
| | 6 | after an odd number of vertices |
| | 12 each | the polygons |

A polygon: a byte of flags, three bytes the program fills in as it loads
the model, four vertex indices, and its picture's 4-character name in
the car's `.qfs`. The loader sets byte 1 to 0 and puts the polygon's
first entry in its table of texture corners in bytes 2-3 (0x461f55,
0x461f6a); 25 of the files hold their maker's leftovers there.

- **The parts.** The 16 racing cars have parts 0 to 19, the detailed
  model; every model has part 21, and the others (traffic, the other
  vehicles and things) only parts among 20 to 25. In the F355, 12 to 19
  are its four wheels in pairs (their positions ±0.85 across and 1.19
  forward, 1.48 back); 21 is the whole car at medium detail, 24 and 25 at
  low. OpenNFS names the same order: 0-19 high, 20-22 medium, 23-25 low,
  26-31 reserved *(published)*.
- **Flags**, from the code that sets up and draws the polygons
  (0x461d80, 0x46da60) and counted over the 4,182 polygons:

  | Bit | Meaning | Polygons |
  |---|---|---|
  | 0x01 | a triangle: vertices 0 to 2 drawn (the fourth repeats one of them) | 578 |
  | 0x02 | a quad: set on every polygon without 0x01 | 3,604 |
  | 0x04 | facing the other way: drawn v0, v3, v2, v1 (a triangle v2, v1, v0), the picture's corners reversed with them, so each vertex keeps its corner (0x46dad3, 0x461ee5). The left-hand parts, mirrors of the right's (5 and 6, 7 and 8), and about half the low-detail ones | 1,931 |
  | 0x08 | see-through: the picture's palette entry 255 isn't drawn (the software renderer's span modes 1 and 5, 0x4a1688). Every `m` and `z` picture, and others; the cars' palettes make 255 black | 1,042 |
  | 0x10 | two-sided: never culled (0x46da92, 0x46f836). Every `n` and `x` picture, the tyres' treads (13, 15, 17, 19) | 423 |
  | 0xE0 | a count of pictures taken in turn, `flags >> 5` (0 meaning one): the file's, then each with its number added to the name's second character (0x461e5c). 2 only on part 11 (`l000` and `l100`: the brake lights, off and on), 4 only on the wheels' faces (`m000` to `m300`, still, turning and blurred) | 46, 64 |

  Which picture is shown is the polygon's byte 1, which the program
  rewrites before it draws each car (0x45a220; read at 0x46da64): see
  The frames.
- **Texture corners.** A polygon's vertices 0, 1, 2 and 3 take the
  picture's corners (0, h−1), (w−1, h−1), (w−1, 0) and (0, 0) (0x4736a0,
  0x46f841): with the picture's first row at its top, bottom left,
  bottom right, top right, top left. Seen so: the beer crate's
  lettering reads on the Cars page laid this way, and not the other.
- **The paint.** Every entry of the car's palette with red and blue under
  0x56 and green from 0x80 is the paint's: the program recolours it as it
  sets up the car's palette (0x444f80), its shade from the green
  (0 to 127 above 0x80): black rising to the chosen colour, then toward
  white for the highlights, by a threshold of each colour's. The colours
  are a table in the program (0x4c3634, 16 bytes each), three values
  each (hue, saturation and value, by how they're turned into RGB:
  0x444af8 *(inferred)*) and the threshold. Most cars' palettes have 50
  to 146 such entries (the F355's 103, all pure greens from 128 to 255);
  `CHEV` and `DAYT` have none, `IDGO` and `LIMO` one, and the props
  none: those can't be repainted.
- **The frames** (0x45a220, as each car is drawn; the model's polygons
  are shared by its cars, so each sets them before it's drawn):
  - **The brake lights** (part 11's polygons with frames, listed at
    0x459a80): 1 while the car brakes (its brake byte, which the
    drivers set and the physics reads: 0x42b5db, 0x452820), else 0.
  - **The wheels' faces** (parts 12 and 14 in front, 16 and 18 behind,
    their first polygons: 0x459ad0), by the car's speed and a count the
    car steps once a frame while the race's clock runs (0x45a32a):

    | Speed (m/s) | Frame |
    |---|---|
    | over 20 | 3, blurred |
    | over 7 | 1 and 2 by turns, every count |
    | over 2.5 | every 2 counts |
    | over 0.2 | every 4 counts |
    | else | 0, still |

    Then: an axle whose wheels spin (the engine more than 1,250 rpm
    over them, in gear 4 or below: 0x4556e4) shows 3; one whose wheels
    are locked (a hard brake or the handbrake: 0x454d5f, 0x45343c)
    shows 0 *(inferred)*; and a car that has finished, under 2 m/s,
    shows 0 on both.
- **The wheels' faces moved out** (0x459e0c): as a model loads, the first
  polygon of the rear wheels' parts (16 and 18) moves 5/256 outward
  (0x45a750, 0x45a7b0) in all 15 racing cars (the program's model numbers
  0 to 14, its table at 0x4c3df4), and of the front wheels' (12 and 14)
  in the Ford Indigo's (`IDGO`) only. The faces sit on the tyres' outer
  edges in the files; moved, they aren't lost against them when drawn
  *(inferred)*: on the Cars page, the F355's rear faces unmoved are half
  hidden by the tyres, and moved they show whole. The Indigo's front wheels alone stand clear of its body.
  `MUSC` has no number: the game never loads it *(inferred)*.
- **The colours' file.** `GameData/CarModel/pc/CARHSVT.TXT` holds the
  table's first 24 colours, the same four numbers each, as commented text.
  `DELTA.SJH` beside it, 5,568 bytes, is 29 cars × 24 colours × two
  numbers: changes to each colour's value and threshold for each car
  *(guessed: by its size and a name in the program,
  `Color_ReadCarDeltaVTFile`; its reader wasn't found)*.

## In Reluva

Read as `CarModel` (`src/engines/nfs/nfs2se/formats/geo.cpp`);
`reluva-tool nfs survey` reads all 53 in `cardata.viv`, and
`reluva-tool nfs geo-obj` writes one as an OBJ.

The data viewer's Cars page (`src/engines/nfs/nfs2se/pages/car_page.cpp`)
draws each model a level at a time, its pictures from the car's `.qfs`
by name, their corners as the program lays them, both faces, lit from
one side. P picks a paint from `CARHSVT.TXT`'s colours, or none (the
palette's green), and recolours the pictures as the program does
(`src/engines/nfs/nfs2se/formats/paint.cpp`); `DELTA.SJH` isn't put in.
The wheels' faces and the brake lights show their frames as the game
picks them, for a speed chosen with V and the brake with B; the count
steps 30 times a second, where the game's steps once a frame it draws.
The wheels' faces are moved out as the game's loader moves them.

## Sources and checks

- **Where the facts come from:** the files; OpenNFS (MIT, `LibOpenNFS`'s
  NFS2 reader, read only), the only public description, for the layout;
  no prose write-up was found. The flags, the words the program fills
  in, the corners and the paint from `nfs2sen.exe`'s loader and renderer,
  disassembled and read (November 2026).
- **How it was checked:** survey scripts over the 53 (November 2026):
  every part's counts and polygons' indices, and the padding, which
  OpenNFS gives as 6 bytes after an odd count: every model then ends
  exactly with its 32nd part, and no other padding does that; the reader
  on all 53; the unit tests (`nfs.unit.nfs2se`).

## Open questions

- What the header's first word meant to its maker.
- Why the Indigo's front wheels' faces alone are moved out.
