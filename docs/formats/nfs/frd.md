# Need for Speed III: `.frd` track geometry

Status: **implemented** (`src/engines/nfs/frd.cpp`). All 9 retail tracks
read completely, with every byte accounted for (`reluva-tool frd-check`),
and draw correctly on the Tracks page.

## How it was worked out

October 2026, with survey scripts over the nine tracks:

- **Starting point.** The overall order (track blocks, then polygons,
  then objects, then textures) and several field names follow the NFS
  modding community's descriptions of NFS III's FRD, as T3ED used them.
- **Track blocks.** Each field was found on the data:
  - the block count is one less than the blocks: block 0's neighbour list
    names block 119 of tr00's "119";
  - the record sizes come from a least-squares fit of each block's size
    over its eight counts, on all 1,583 blocks with a following block:
    8, 8, 12, 20, 20, 16 and 16 bytes, exactly.
- **Polygon section.**
  - An empty list has no repeated count: block 1's first list is `0`,
    followed by the next list's count.
  - There are four object layers: block 0's trailing `0` and `10, 3` fit
    only as its layers 1-3.
  - Checks on every block of every track:
    - each list's count matches its repeat;
    - each layer's total matches its objects' polygons;
    - list 4's count equals the block's `nPolygons`, in all 1,592 blocks.
- **Placed objects.** Type 3's key count is the short at +32: 17 keys of a
  spinning object on Hometown, after which the usual vertices and
  polygons follow.
- **Texture table.** The bytes left after the objects divide exactly by
  47 on all nine tracks.
- **By eye.** The tracks drawn on the Tracks page:
  - roads, lane lines, fences, poles, trees and buildings are where they
    belong;
  - billboard text reads the right way round (with z mirrored, as
    models are, [fce.md](fce.md)), once the page culls back faces and
    takes placed objects' vertices in the game's order (Textures,
    below).

All values are little-endian. Floats are 32-bit IEEE.

## Layout

| Part | Contents |
|---|---|
| Header | 28 bytes: `35 01 EF BE`, then three Windows FILETIMEs (1998); u32 number of blocks − 1 |
| Track blocks | one per block, below |
| Polygons | per block: seven lists and four object layers |
| Placed objects | 4 × blocks + 1 groups |
| Textures | u32 count, then 47-byte entries, to the end of the file |

### Track block

| Offset | Size | Meaning |
|---|---|---|
| 0 | 3 floats | centre |
| 12 | 4 × 3 floats | the block's outline on the ground |
| 60 | 6 × u32 | vertices; high-res, low-res and medium-res counts; a duplicate of the vertex count; object vertices |
| 84 | 12 × n | vertices |
| | 4 × n | colours, `0xAARRGGBB` (baked lighting: white, or blue-grey in shade; alpha 255 in all but 98 of 414,641) |
| | 300 × (s16, s16) | neighbours: the blocks drawn from this one (−1: none), with a road detail always −1 so far (Which blocks are drawn) |
| | 8 × u32 | start position, then the counts of the next seven record lists |
| | 8 each | positions |
| | 8 each | per road polygon (as many as list 4) |
| | 12 each | virtual road vectors |
| | 20 each | references to placed objects |
| | 20 each | polygon objects |
| | 16 each | sounds |
| | 16 each | lights |

The vertices come in order:

1. the objects' vertices (the object count);
2. the road's (the high-res count);
3. the rest, which only the lane list uses.

The records after the counts are kept as stored. Lights and sounds are
also decoded (October 2026, from a survey of all nine tracks):

- **Lights** (16 bytes): a position, 3 × s32 in 16.16 fixed point, then
  a u32 type. The positions are on the tracks where lights would be:
  `trk000`'s type 1 lights sit on its town centre's street lamps. The
  types run 0 to 15 and differ by track (`trk000` 1 and 7, `trk004`
  13, `trk007` 3 and 13...). Every type is drawn with `glw0` and
  `glw3` of `Render/pc/sfx.fsh` (below); the type (& 15) picks a colour, a size and a
  blink from a table in nfs3.exe (`0x55e3dc`, 12 bytes each, used by
  `0x491190`). A blinking light is hidden while
  ((t + phase) >> shift) & 1, t the tick count (64 a second):

  | Type | Colour (ARGB) | Size | Blink: shift, phase |
  |---|---|---|---|
  | 0, 14, 15 | `FFDEEAEB` | 5 | |
  | 1 | `B9FFFFFF` | 4.5 | |
  | 2 | `FFFFFFD2` | 5 | |
  | 3 | `FF80E5F0` | 4.5 | |
  | 4 | `FFD9C45E` | 5 | |
  | 5 | `FFDF1616` | 5 | 6, 0 |
  | 6 | `FFDF1616` | 5 | 5, 27 |
  | 7 | `FFFF0000` | 3.125 | 6, 0 |
  | 8 | `FFA3B1BE` | 3.75 | |
  | 9 | `FFDF1616` | 3.125 | |
  | 10 | `BADF1616` | 2.5 | |
  | 11 | `FF1E95E3` | 2.5 | |
  | 12 | `FF1E95E3` | 3.125 | 6, 0 |
  | 13 | `FFE0E027` | 3.75 | |

  **The two quads** (`0x491190`, November 2026). The light's centre is
  put into the camera's view (+0x38 position, +0x44 rows) and drawn
  only when its depth z is from 1 to 256 (`0x49126b`: the float's bits
  compared as integers) and its centre is inside the screen's window
  (outcode 0); otherwise neither quad is drawn. Both face the camera,
  turned by ¼ turn × (x / z + 1) (`0x53bcb8`) as the cars' glows are
  (fce.md), and go to the depth-sorted list at depth z:
  - `glw0` (`0x8b47bc`) at a fixed size on the screen: its corners
    0.025 (`0x53bcb0`) × size × the projection's scales (320 and
    −330.33 in a 640 × 480 race) from the centre, not divided by z; its
    colour (alpha too) × k / 256, k = min((256 − round(z)) × 256,
    0xFFFF) >> 8 (`0x491364`), so it fades out towards 256;
  - `glw3` (`0x8b4840`, from `0x4918ca`) in perspective: its corners
    1.05 / z (`0x53bca8`) × 0.25 (`0x53bcc0`) × size × the scales from
    the centre, 0.2625 × size units, in the full colour.

  In `nfs-host` (a night race on Hometown with the player's car poked
  onto road slice 520, the town centre, `t4-town`) the lamps show the
  `glw0` star at the same few pixels whatever their distance and the
  small `glw3` cross over it; the Tracks page from the game's camera
  dumped there draws them at the same places and sizes.

  Two options change them. With the Direct3D renderer (`d3da`) only,
  the effects intensity slider (`SFX_INTENSITY`, a float at `0x6fbc38`,
  1.0 by default, `0x472e5c`; 0.5 on an "STB Velocity 128" card,
  `0x472ea0`) dims them when below 1: k = min(f × 65536, 0xFFFF) >> 8,
  then each byte of the colour, alpha too, becomes byte × k >> 8
  (`0x491223`); size and blinking stay. The same factor dims the
  headlight pass and other effects. And no lights are drawn when the
  Special Effects option at `0x6fbc30` is 2, Off (`0x4911b7`). The
  Tracks page dims them so with key I (`dimLight`), only in its Direct3D
  look (Shift+Y), as the game does.

  The slider is on the Advanced Graphics screen (`advgrph.mnu`) and the
  race's pause menu graphics (`psgraph.mnu`), `codelink=SFX_INTENSITY`;
  without `d3da` it's flagged off (`0x448738`). It runs from 0 ("none") to 1
  ("full"): the arrow keys move it by its step and clamp it to [0, 1]
  (`0x46a8d7`-`0x46a962`). The step is the menu's `snap` when that is
  above 0 and at most 1, else `r0_step` (`0x46a873`, `0x46a70d`), so
  0.1 here. **Dragging doesn't snap** (October 2026): the mouse sets
  the value to the knob's position over the track's length in pixels
  (`0x469590`), and every setter passes it through `0x4693c0`, which
  rounds to a multiple of `snap` (half up, `0x53ae50` = 0.5) only when
  `snap` isn't 0. With `snap=0.000000` it's returned as it is, so a drag
  gives any of the track's pixel steps (a 195-pixel bar, `p1_w`), not
  tenths. The menu parser keeps `snap`, `r0_step` and `r0_val` at
  +0x44, +0x48 and +0x54 of its template (`0x604ee0`; the attribute
  table at `0x558324`), the control's same offsets. Its callback
  (`0x448570`, `0x44cf70`) stores the value.

  **Correction (October 2026): `0x6fbc30` is the Special Effects option,
  not the Horizon.** The options table (`0x5563e8`, 0x14-byte entries:
  name, list, two zero words, then the value's address, read by
  `0x442c90`-`0x442cc9`) gives `sfxlevel` (`kMenuGRAPHICSSpecialE001`)
  `0x6fbc30` and `hrzlevel` (`kMenuGRAPHICSHorizon001`) `0x6fbc2c`; the
  address after an entry's name belongs to the next entry. Both lists
  are text 187, 185, 229: 0 High, 1 Low, 2 Off; both options are on the
  Graphics screen (`graphics.mnu`) and in `psgraph.mnu`. So the lights
  above, the particles ([col.md](col.md)) and the rest below go by the
  Special Effects option. Its other readers:
  - `0x491bc0` (from the car drawing, `0x4b8d74`, `0x4b8e8d`): a car
    light's flare on the wet road, skipped when Off or without weather
    (`0x6fd4cc`) ([fce.md](fce.md), Lighting);
  - `0x492980` (per car, `0x4b8efe`): a headlight's beam in fog, with the
    effects intensity, skipped when Off ([fce.md](fce.md), Lighting);
  - `0x4c6f00` (per view, `0x4bc267`, `0x4bc2e5`): skipped when Off. It
    draws up to 64 strips of 0x340 bytes (`0x560110`, count
    `0x7d5758`), laid down per wheel from `0x429cf1` and `0x429ea8`
    through `0x4db940` with the road polygon's material: tyre marks,
    most likely;
  - `0x4cc5c0` (per view): the particles aren't drawn when Off;
    `0x4cbd0c`: at Low every other throttle turn is skipped;
  - `0x4cc600` (per view, `0x4bc3c1`): only at High, `0x4cb750`, a
    screen-space pass of four quads scaled by the effects intensity
    and skipped for camera modes 8 and 14 (not followed further);
  - `0x4dece0` (per view, `0x4bc375`): the number of raindrops
    (`weather.fsh`, `DRP%d`) is halved at Low and quartered at Off;
    `0x4dedf0` (`0x4bc39b`) does without the splashes (`SPL%d`) at Off
    (the whole pass: [col.md](col.md), Rain and snow);
  - `0x47268e`, `0x472e3b`: the defaults (below).

  Its default is set by `0x472d10` from the processor's speed in MHz
  (`0x4f1ef0` / 1,000,000; `0x472d8e`-`0x472de2`) through tables of five
  (`0x472168`-`0x4721ae`); the Special Effects and Horizon tables
  (`0x472190`, `0x472172`) are the same:

  | MHz | Index | Special Effects, Horizon |
  |---|---|---|
  | up to 166 | 0 | 2, Off |
  | 167-200 | 1 | 1, Low |
  | 201-233 | 2 | 1, Low |
  | 234-266 | 3 | 0, High |
  | over 266, or no timer | 4 | 0, High |

  The others by index: `viewlevel` (`0x6fbc28`) 3, 3, 2, 1, 1;
  `cardetail` (`0x6fbc1c`) 2, 2, 1, 0, 0; `projtex` (`0x6fbc3c`) 0, 0, 0,
  1, 1; `0x6fbc20` 0, 1, 1, 1, 1; `0x6fbc24` and `0x791b28` 0, 0, 1, 1, 1.
  With either software renderer the index is halved first
  (`0x472de4`: `test byte [0x7a3a58], 6`). That byte is 1 << the
  renderer's number in the DLL table at `0x55fd68`: 0 `nulla`, 1
  `softtria`, 2 `softlina`, 3 `voodooa`, 4 `voodoo2a`, 5 `ragea`, 6
  `d3da` (so bit 6, 0x40, is Direct3D, as tested for the slider and the
  dimming), 7 and 8 `powervra` (`-pvr`, `-pvr2`; the 0x180 tests), then
  the Verite pair. So the mask 6 is bits 1 and 2, `softtria` and
  `softlina`; with them `0x472eaa` also turns the z-buffer and projected
  textures off and sets a 320-wide mode.

  Both are saved. `config.dat` is the 0x20F0 bytes of settings at
  `0x6fbb40`, written whole (`0x47285f`) and read back whole
  (`0x472a68`; its first u32 must be 0x20F0). So the Horizon option is
  the u32 at offset 0xEC, Special Effects at 0xF0 and the slider the
  float at 0xF8. `0x472d10` only runs when `config.dat` is missing or
  doesn't match (size or renderer, `0x472ab6`, `0x472adf`, via
  `0x67900c`; `0x473195`), and from `0x4a3dda`. The install's own
  `config.dat` has both at 2 (Off) and the slider 1.0; one saved by
  `nfs-host` (renderer changed, so the defaults rerun on its fast
  clock) has 0. `nfs-host -mhz N` sets the clock the game measures.
- **Sounds** (16 bytes): a position as above, a u16 slot of the track's
  placed-sound bank (`Audio/SFX/tr3dNN.bnk`, or `...n`, `...w`, `...nw`
  by sky variant) and a u16 kind (`0x41d210`): 0 a loop, the first
  within reach playing on one channel; 1 a one-shot, fired at random
  moments on another. The volume falls with the square of the
  distance. The game passes only a block's first sound record, so the
  second of the one block that has two is never heard. The slots fit
  the banks: `trk000`'s go up to 7 of 32 slots, `trk005`'s to 20 of
  21. `trk008` uses slot 12 of a 12-slot bank, so not every one can be
  heard.

The rest (positions, the road polygons' records, the virtual road,
object references and polygon objects) is still kept as stored. Object
references begin with a 16.16 position near their block. The positions
are one record per slice of the track (8 per block; the block's first
slice is its start position): a u16 first polygon and a u8 count, the
run of high-detail road polygons (list 4) across that slice, then a
u8 and −1. The game finds which road polygon a car is on by starting
from the run's middle one and walking sideways (`0x41e990`), into a
0x78-byte track position ([physics.md](physics.md), Other car fields,
+0xC): polygon i shares its edge from vertex 0 to 3 with polygon i−1
and its edge from 1 to 2 with i+1, and its edge 0-1 faces the next
slice (`0x41e6a0` takes the vertices as 1, 0, 3, 2; `0x41e990`,
`0x41f870`, `0x41fd60`). A point beyond the first polygon's or the
last's outer edge is marked with side 1 or 2 (+0x6C), the side bits
the walls use. The run goes from left to right as the track is driven
forward: its first polygon is on the driver's left (along −right of
the slice's `.col` point) at 12,667 of the 9 tracks' 12,706 slices,
and a polygon's corners are 0 front left, 1 front right, 2 back right,
3 back left, so side 1 is the left and side 2 the right (November
2026, from the files and the runs `d2-w1`, `d2-w2`; physics.md, +0xC).

**The 39 other slices** (November 2026, from `tr02.frd` and `tr02.col`
read with the repo's readers and a script outside the repo) are
Atlantica's slices 429-448 and 500-518 (blocks 53-56 and 62-64): two
stretches where the road runs under a roof, a flat ceiling 19.1 units
above the road (7.3 at 500-518), reaching from the wall on the road's
left to 13-30 units right of the `.col` point, and open on the right.
There the run is the cross-section from the roof's right end: the roof
from right to left (3 polygons, facing down), down the left wall (3
upright polygons at 429-448, 1 at 500-518), then the road and its edge
from left to right as at every other slice. It isn't a fork, a second
level or a reversed list: the polygons chain as everywhere (vertices 0
and 3 of each are vertices 1 and 2 of the one before it), and none is
degenerate except one road triangle (slice 503). Facing down, the roof
polygons have corner 0 at the front right. The roofs and walls of other
tracks' tunnels and overhangs (Rocky Pass slice 380, Atlantica 220)
come at the run's end, after the right wall, so the check finds those
runs in order. The roof and wall polygons' road records have surface
14 (record byte 1 & 0xF).

What the road finding does there (read from `0x41e990`; corrected
November 2026, below): its inside tests are on x and z only; a polygon
of surface 14 is never taken as found; and each walk, up from the middle
and down from the one below it, goes on while the sum |dx| + |dz| from
the point to the polygon's vertices 1 and 3 (`+0x54`, `+0x5c`) shrinks,
and past a polygon where it grows only when that polygon is of surface
14 (`0x41eca2`-`0x41ecd2`: `cmp al, 0xe` / `jne 0x41ecf6` stops the
walk up on any other surface; the same at `0x41ef51`-`0x41ef81` for the
walk down). So the middle polygon it starts from, (count − 1)/2, is a
piece of the left wall here, the walks pass over the roof and wall
polygons, and:

- a point on the road is found in the same road polygon as at a slice
  without the roof;
- a point right of the road gets side 2, as everywhere (`0x41ebd7`: not
  inside the last polygon, but on its side of the edge it shares with
  the one before);
- a point left of the road, inside the wall, gets side 1 (`0x41ee86`,
  the down walk reaching the first polygon over the roof's: on a roof
  polygon, mirrored, the edge 1-2 it is tested against has its inner
  side to its left, so side 1 still means the left). With the search
  rewritten in C++ (`roadSearch`, frd.h) over the dumped runs, all 624
  points 0.5-8 units beyond the `.col` left wall in these 39 slices get
  side 1, none side 2, and the 622 right of the road side 2 (2 found on
  the road). The first account here (October 2026) had the stop rule
  the wrong way round in its model of the loops (stopping on surface
  14), and so found 113 of 195 such points with no side.

So the marks keep their meaning. A missing side 1 wouldn't change the
wall: those points are behind the left wall; a car's corner there has
no polygon either way (off the road, `0x430350`); and `0x41fd60` adds
bit 1 from the car's own polygon's edge 1-2 whenever the corner's
polygon isn't the car's. This wasn't checked by driving into those
walls.

**The search beyond its first slice** (November 2026; `roadSearch`,
the two open items of the road finding, settled from the code). When
neither walk finds the point, `0x41efa5`-`0x41f013` moves `+0x10` by
the next step of `0x552e58` (1, −2, 3, −4 ... 11: the slices +1, −1, +2,
−2 ... +5, −5, +6 from the first) and walks that slice's run, up to 11
slices, then puts `+0x10` back (`0x41f018`).

- **The opposite side's mark.** The side is set at most once a search
  (`0x41eba3`, `0x41ee52`: only while `+0x6C` is 0), at whichever slice
  a walk first ends on its run's first or last polygon, and the test
  there is only that the point is on the inner side of that polygon's
  other edge, not that it lies beyond the slice's end. On a bend a point
  beyond one wall of its own slice can be on that side of the other end
  polygon of a slice 3-5 away, and the search marks it with that side.
  Over the nine tracks (points 0.5-8 units beyond each slice's `.col`
  walls, the search carried from point to point outwards as a car's is),
  the points within a unit of a wall that end with the other side's
  mark: Hometown 16 of 3,836, Redrock Ridge 5, Rocky Pass 110 of 5,932,
  Country Woods 75, Aquatica 14, The Summit 406 of 6,892 (Rocky Pass and
  The Summit share stretches; on The Summit 564 come from the first
  slice's own walk down over surface-14 polygons), and none on
  Atlantica, Lost Canyons or Empire City. On Rocky Pass, of the wrong marks within two
  units of a wall, those set 3-5 slices away (52 at +5, 43 at −5, 28 at
  −4, 24 at +4 of the first slice, by walks up; 49 more by walks down). That is what the code does. **It doesn't push a car the wrong way**
  (November 2026, read from `0x4304b0`, `0x41fd60`, `0x41f870`,
  `0x430210`, `0x42ee10` with `gdecomp` and `asm`, and driven). For a
  corner off the road `0x41fd60` ORs the car's and the corner's +0x6C
  with its own bits from the corner against the **car's** polygon (1 or
  2 within 1.5 units of its edge 1-2 or 3-0, 4 or 8 past its ends; only
  the +0x6Cs when the corner's record is the car's). For each bit (4, 8,
  1, 2, `0x42dd48`) `0x41f870` steps from a copy of the car's own track
  position to that neighbour (i − 1, i + 1, the next or last slice; it
  clears the copy's +0x6C); only where there is none, or it is surface
  14, or `0x430350` finds the car below it, is that edge a wall, and its
  normal is made from the car's polygon's edge (`0x430210`), so it
  points into the road. With no wall found the push is from the corner
  towards the car's middle. `car_contact_impulse` (`0x42ee10`) moves the
  car out only by a corner's depth behind that edge (from one of the car
  polygon's corners, car +0x30-+0x5C) and pushes only a corner moving
  into it. A wrong mark (2 at a left wall) therefore adds a normal only
  when the car's polygon has no road on its right, and even then from
  the right edge, which a corner at the left is in front of: no push.
  Over the found points with the car's middle 1.2 m inside the wall
  (`e5-runs/sim/wrongside`), the step the other way from the car's
  polygon is road at all 66 on Rocky Pass and all 220 on The Summit.
  Driven (`e5-wall1`: Rocky Pass, the player's car held at slice 478
  8.9 m left of the road point, 1.15 m inside the left wall, then let go
  at 15 m/s forward and 6 m/s into the wall): from 52.37 to 54.02 s the
  wall test found 26 corners off the road, 25 of them with the mark 2
  (bits 3 and 7) and all with 1; every normal asked was side 1's (25),
  and the 25 impulses pushed the car right, its speed across the road
  from −5.1 to +1.2 m/s, its middle −8.8 to −8.4 m, never past the wall.
- **Reading past the run.** The walk up holds its start to count − 2
  of the slice it is on (`0x41ea31`-`0x41ea38`; −1 for a run of one, so
  it begins one polygon before the run), but the walk down starts from
  the first slice's start less one as it is (`0x41ecf6`, `[ebp-8]` is
  set only at `0x41e9e0`/`0x41e9f7`). After a step to a slice with fewer
  polygons than that, it reads polygons past the slice's run: the next
  slices' polygons of the block's list 4, or, from the block's last
  slices, memory past the list. Over the points above it happens 361
  times on Lost Canyons (164 of them past the block's list), 130 on
  Redrock Ridge, 26 on Empire City, 19 on Atlantica, a few elsewhere
  (`roadSearch` counts them, `pastRun`, and stops unfound where the
  game would read past the list); in a Python model of the loops none of
  the polygons read so finds the point. In a
  race it didn't come up: `e1-lc` (Lost Canyons, 8 cars, set-up pokes
  only, 6,000,000,000 instructions, `-watch 0x41ed08` with the start
  `+0x70` and the slice's record) had 6,882 walks down and none starting
  past its run, nor any on Rocky Pass (`e1-rp`, 847). A side mark was
  set once in the two (side 1, Lost Canyons slice 70).

The byte
at 3 is the music zone, 4-9: the game
sends it to the Pathfinder map as an event when the car nears a slice
where it changes ([mus.md](mus.md), The events the game sends).

The Tracks page shows each light as the game's two quads facing the
camera (`lightQuads`), in its type's colour and size and blinking as the
table above has it, none with the Special Effects option Off (key Q),
and each sound as a cyan diamond. Its view is the game's (`viewScreen`,
hrz.md): x scaled by 320 and y by 330.33, so `glw0`'s corners, 0.025 ×
size × z units from the centre across and up, land on the game's pixels,
3 % taller than wide. Until November 2026 the page's pixels were square
(the view 2 atan 0.75 high instead of 72°), so everything stood 3 %
lower than in the game. It plays the sounds as the game does:
only each block's first, louder by the game's inverse square and panned
by side; the nearest loop within 300 units (the game takes the first of
the blocks seen) and one-shots when the timer picks a frame, counting
the page's frames as the game counts its own (`0x434a73`). A loop plays
looped whatever its bank says. The overlay names the nearest light or sound; `M` hides them all.

### Polygons

A polygon is 14 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 × u16 | vertices, of its block (or its object). A quad; a triangle repeats one. Drawn in this order on a block, as 1, 0, 3, 2 on a placed object (Textures) |
| 8 | u16 | an entry of the texture table |
| 10 | u16 | the animation's first texture entry (as at 8 when animated); read only on animated polygons, below |
| 12 | u8 | flags: `0x10` two-sided, `0x04` animated, `0x01` (none in the files); `0x20` and `0x40` are never tested, below |
| 13 | u8 | the animation: delay in ticks in the top 5 bits, frames in the low 3 (`0xF9`: 31 ticks, 1 frame, still) |

An animated polygon's texture entry is the one at 10, plus
(t / delay) mod frames, t the game's tick count (64 a second;
`0x41b52b`). The track's animated polygons have 28, 84 or 86: 3 or 10
ticks, 4-6 frames.

What reads the polygons (nfs3.exe, October 2026). The three loops that
draw them (`0x41ae90`, `0x41b2f0`, `0x41b640`) copy byte 12 and test
only `0x10`, `0x01` (`0x41b4e6`, `0x41b7c8`) and `0x04` (`0x41b52b`,
`0x41b80a`). No other code was found reading byte 12 (every read of a
polygon's +12, and every loop stepping 14 bytes, was checked), and the
car's road finding (`0x41e990`) uses the 8-byte road records, not the
polygons. So:

- **`0x20` and `0x40`** do nothing in the game. Over the nine tracks
  the roadside lists (1, 3, 5) all have `0x20`; lane markings never
  have either; on the road (list 4), `0x20` is on 5 % and `0x40` on
  8 %, inside the slices' runs like the rest; `0x60` is rare (6 to 115 a
  track). Probably editor marks.
- **The field at 10** is read only behind the `0x04` test (`0x41b563`,
  `0x41b842`). Lane markings (list 6, 27,748 polygons) all have flags
  0, so theirs is never read. The values there (mostly 0-7, also 38,
  42, 43 and some like `0x845`, `0x8004`, `0xFEFE`) look like
  leftovers.

Each block has seven lists. Each list is a u32 count, then (if it isn't
0) the count again and the polygons:

| List | Contents |
|---|---|
| 0, 2, 4 | the road at low, medium and high detail |
| 1, 3, 5 | roadside polygons at the same levels, on three tracks only (`trk000`, `trk004`, `trk008`) |
| 6 | lane markings: all their textures are lane textures |

Then come four object layers. Each layer is a u32 polygon total, then
(if it isn't 0):

- a u32 number of objects;
- for each object, a u32 type. Type 1 is followed by a u32 count and the
  polygons; type 4 has nothing more.

A type 3 entry appears once on each of two tracks, also without data. These
objects use the block's vertices.

### Placed objects

Each group is a u32 count, then the objects. Each object starts with
3 × u32: type, a number, and a heading. The number is an entry of the
block's object references (20 bytes each; `0x41cac6`): the game keeps a
state for each (standing, knocked down, or moving), and the reference's
byte 19 = 2 makes it one a car can hit (`0x41d403`). The heading's low
byte, × 256 / 65536 of a turn, sets the turn about the vertical of an
object knocked over (`0x41d5a0`): −32, ±64 and −128 are 45°, 90° and
180°. Then, by type:

- **Type 4** (still, 2,046 of them):
  - 3 floats, the reference point;
  - a u32.
- **Type 3** (moving, 20 of them):
  - 12 bytes the game doesn't read (room for a reference point, which
    type 4 uses; here leftovers);
  - 8 bytes: a u32 size of the data, then a u16 size again, a u8 3 and
    the object's index among the block's moving ones (0, 1, 2...);
  - at +32, a u16 number of keys;
  - a u16: the key delay, in game ticks (64 a second). Hometown's
    17-key spinner (delay 6) turns once in 1.5 s, and its 141-key loop
    (delay 25) takes 55 s.
  - the keys, 20 bytes each: 3 × s32 position in 16.16 fixed point and
    4 × s16 rotation in 2.14 fixed point, a quaternion by its unit length
    (`0x4000` = 1).

Then, for both types:

- u32 number of vertices;
- the vertices, relative to the reference point (type 4) or to the key
  (type 3);
- their colours;
- u32 number of polygons;
- the polygons.

The keys are world positions. Hometown spans x −758 to 911 and z −779
to 661: its spinning object sits at (−563, 2, 442), and its other object
flies a loop 52 m up. The same loop is in the `.col` file
([col.md](col.md)).

**How the game moves them** (`nfs3.exe`, `0x40f4b0`; worked out from
its code). From the game's tick count t (64 a second, `0x7d3684`):

- the delay d is the key delay if 1 to 400, else 6;
- the key is (t / d) mod (keys − 1): the last key is the first again
  (Hometown's loop: they differ by 1/65536 in one coordinate);
- it moves from that key towards the next by (t mod d) / d, the
  positions straight and the quaternions blended (`0x4b70f0`);
- the quaternion's four values are taken in the file's order, each
  × 4 / 65536, and made a matrix by `0x4b7590`. Run on sample values,
  that gives the matrix Reluva's `keyMatrix` builds (x, y, z, w, the
  game's vectors being rows), so the rotation sense is the game's.

The Tracks page moves them so: 64 ticks a second, keys − 1 keys round,
the same blend (normalised rather than spherical, which differs little
between keys this close).

### Textures (47 bytes)

| Offset | Size | Meaning |
|---|---|---|
| 0 | u16 | width |
| 2 | u16 | height |
| 4 | u32 | room for the picture's handle: the game writes it at load (`0x419b4d`); the files hold leftovers |
| 8 | 8 floats | u, v of polygon corners 0 to 3 |
| 40 | u8 | flags: `0x02` a see-through surface, added in half grey at half alpha and sorted by depth (`0x41b596`, below); `0x04` not lit by the headlights' projected texture (below). Values 0, 4, 6 |
| 41 | 3 | not read (byte 43 often `0xFF`) |
| 44 | u8 | 1: a lane texture |
| 45 | u16 | the picture's number |

**Texture flag `0x02` is drawn added** (nfs3.exe, November 2026). The
block's lists (`0x41b596`) and the placed objects (`0x41b86c`) give such
a polygon's corners the colour `0x552e1c`, set each frame by `0x41b9b0`
to `0x7F7F7F7F` (with the Direct3D renderer, bit `0x40` of `0x7a3a58`,
and an effects intensity under 1, each byte that times the intensity),
and set the queued polygon's flag 1 (byte 6), the flag the headlights'
triangles have. Every polygon drawer `0x434870` can pick (`0x4332b0`,
`0x433530`, `0x433840`, `0x433a50`, `0x433bb0`, `0x4336e0`) sends THRASH
state 0x68 = 1 for a polygon with flag 1 and 0 for the others: blend 1,
source alpha and one ([thrash.md](thrash.md)). So the texel times half
grey is added by its alpha times one half, a quarter of the picture
where it's opaque, and alpha-0 texels not at all. `0x4c7790` sends them
to the depth-sorted list (`0x4c7610`), though added the order doesn't
matter. The flag is the texture entry's own, not the picture's: a
picture's `<additive>` sets bits 2 and 4 of its shape slot's byte 0x28
(`0x4d40da`), the same place and bit in the slot's layout, but the
drawers read the entry (`+0x18` of the queued polygon). Over the nine
tracks the two agree: the flagged entries' pictures are exactly the
`<additive>` ones they use, except picture 159 of `tr040` (flagged,
no text); `tr050`'s 67-69 have the text and no entry. On Hometown it's
two entries: the light shafts through the covered bridge's windows
(picture 21, object layer 3 of blocks 21-27) and picture 105 on a
moving object. The Tracks page draws flagged polygons so (source alpha
and one, vertex colour `0x7F7F7F7F`, without the ambient light); it
drew them added in full (`<additive>`) or blended over before. An
`nfs-host` frame driving through the bridge by day (`r1-on2`, frame
3342 at 0:42.79 on the race clock) shows the shafts as faint grey
wedges from the right-hand windows across the floor, as the page now
draws them from block 21. Compared pixel for pixel (November 2026,
`t5-br`: the car held on slice 207 inside the bridge, frame 2646 with
the camera dumped at that sky draw; `t5-brg`, its THRASH list): the
game sends the 17 shaft quads it draws as picture 21 (16 × 16 grey, mean
46, alpha 255) in `7F7F7F7F`, blend mode 1, fogged at 175 towards
(183, 184, 190), the fog of the region at slice 207 (density 65), as the
page draws them. The wedges fall on the same pixels; walls, floor and
ceiling are 3-6 brighter on the page (the host's 16-bit truncation),
the shafts' brightest overlaps about 11 more.

**Brightness against the host's frames** (November 2026). The bridge's
walls and ceiling again (`t5-br` frame 2646, the page from the same
camera, `d5-runs/out/br-page.png`), region by region: the page is above
the host by 3.2-4.6 in red and blue and 2.1-2.7 in green on the walls
and ceiling, 4.6-6.6 on the floor under the shafts. `nfs-host` draws
these frames itself (`thrash_native.cpp`): textures point-sampled, the
colour × texel and the fog in floats, each pixel truncated to an integer
and then to 5:6:5 (`to565`), and read back for the frame as × 255 / 31
(× 255 / 63 for green). Put through that read-back (`(c >> 3) × 255 / 31`,
`(c >> 2) × 255 / 63`), the page's frame comes within 0.6-1.6 of the
host's on the walls and ceiling and 3.2-3.9 on the floor, where the
shafts' added layers each lose their truncation again (above). The
rest, about one level, is the host's float-to-integer truncation before
the 5:6:5 one and the page's rounding to 8 bits. So the walls' brightness
is the host's 16-bit frame, not the page's drawing: nothing in the
game's code makes walls darker. The `.col` side codes 1-4 (col.md) play
no part in it: their only readers pick the wall hits' particles and
sounds, the push-back and cover.

That the overlaps' difference is the host's 16-bit frame was checked
offline (November 2026, `t6-scripts/shaftcount.py`, `trunc565.py`).
Rasterised from `t5-brg`'s THRASH list, the 17 quads cover 10,635
pixels once, 8,219 twice and 12,866 three times (the grey wedges), and
4-12 times only in the white patch at the far end, which is at 255
both on the page and in the game. `nfs-host`'s native THRASH
(`thrash_native.cpp`) reads each pixel back from 5:6:5 as × 255 / 31
(× 255 / 63 for green), adds the colour × alpha / 255 truncated, and
stores it truncated to 5:6:5 again; the page adds in 8 bits. Run on
floor values of 40-70 and layers of picture 21 × `7F` (15-30, × 127 /
255 each), the page comes out above the host by 2.4 (red, blue) and 1.4
(green) with no layer, 5.2 and 3.5 with one, 8.2 and 5.8 with two and
10.5 and 7.5 with three: the three-layer wedges' 11 is what the
truncation per layer gives, and the walls' 3-6 is about 2 of it and
something else (the textures' filtering or the fog) besides. The page
keeps its 8-bit adds.

**Texture flag `0x04` and the headlights.** `0x41ae90` is called for
each car (the loop at `0x41c6cc` over the cars, `0x5efac8`) and each
list of a block in view: the road and roadside at the block's detail,
three object layers, and the lane markings at high detail. With the
`projtex` option on (`0x6fbc3c`, its name in the options table at
`0x55676c`) and the z-buffer in use (`0x7a3a60`, `0x431b44`), a polygon
whose texture lacks `0x04` is drawn again as two triangles with the
headlight picture (`0x41b24c` → `0x41a360` → `0x41a120`, with a
fixed texture, `0x8b4814`). One whose texture has `0x04`, or any polygon with the
option off (or a light record of type 1), only has the light added to
its vertices' colours (`0x41b0cd`). The car's light reach (10 to 40,
or 20 to 60) is set from the same option at `0x42d253`; that this is
the headlights comes from that and the option's name, not from seeing
it. Over the nine tracks, every lane marking and every roadside polygon has `0x04`, as
do 30 % of object polygons and 5 % of the road's.

**As nfs-host runs it** (November 2026). The hosted game has no projected
picture: its renderer is `voodoo2a.dll` loaded as `softtria`, a software
renderer, for which `0x472eaa` turns the z-buffer (`0x7a3a60`) and
projected textures off, and `e1-tl1`'s THRASH log of frame 2818 (Empire
City by night, `-thrashlog 2818 2818`) sends `setstate(0x4, 0)`, z test
and write off, four times and never on. So every polygon only has its
vertices raised. Read against `d8-t8g1`'s list (the same night frame):
of the 100 vertices of the blocks near the player that the page's lights
raise (the human's high beams and racer 1's low beams, `carHeadlight`
without projection), 98 have the list's colour exactly (the light's grey
replacing the vertex's, `ff474747`, `ffdcdcdc`...) and the other 2 keep
their own red there, byte by byte the higher, as the page draws them;
the 23 left unlit match the list's unlit `ff030306`. The Track page drew
the projected picture by default (Shift+E), which doubled the pool's
brightness against the game's frame: the road in front 112, 102, 91
against the game's 62, 58, 57 (mean of x 300-400, y 250-262 at 640 ×
480), the lit wall on the left 35, 28, 22 against 54, 46, 35. It now
starts with the z-buffer and the projection off, as nfs-host has them
(65, 60, 60 and 58, 48, 39: within the 16-bit frame's truncation);
Shift+E turns both on, as a 3D card's game with the projtex option
would draw.

The picture is the shape at that place in the track's sheet,
`trNN0.qfs` (or `trNN1.qfs`, an alternative set that differs in a few
pictures), counted from 0, whatever its name (November 2026). The loader
`0x41db10` loads the sheet's shapes in order into 44-byte slots
(`0x5dd858`), and `0x419ab0` turns each texture's number into the slot
at that index. The names don't always follow the order: `tr010` has no
`0095`, so from place 95 each name is one more than its place;
`tr070` has `0001`-`0003` and `0025` twice (places 26-29); `tr040` and
`tr060` name theirs `HOR1`, `CEME`, `GOLD`...; and the `1` sets of
Hometown, Atlantica and Empire City skip a name too. For a lane texture
it's `linN` in `GameData/Render/pc/sfx.fsh`, by name.

Found from footage: at Redrock Ridge's start the Tracks page showed a
red tiled square on the Taco Den sign's pole and lightning-patterned
roofs on the motel, where `Redrock_Ridge_Day` (0:12) and an `nfs-host`
race show the Taco Den sign and red tiled roofs: the page had taken
`0135` (the tiles) for number 135, which the game takes as place 135,
`0136`. Summit was the worst: from place 26 nearly every picture was
wrong, which made its forest start look like a town of planks. The page
takes pictures by place since.

**Signs read backwards on the page (found, November 2026).**
`Redrock_Ridge_Day` (0:12) and an `nfs-host` race (no mirror) both
show "CACTUS MOTEL" and "TACO DEN" readable, from set 0, where the
Tracks page showed set 0's signs mirrored and set 1's readable. The
pictures aren't the cause: set 0's read the right way round in the
sheet, and set 1's 14 to 31 differing pictures a track are the same
signs flipped left to right (or turned over), for the mirrored track
(`0x41dbc0` loads `trNN1.qfs` when `0x6fd4c4` is set, else
`trNN0.qfs`). The Taco Den sign is two one-sided quads back to back in
a block's object layer (flags `0x60`, entry 193), each readable from
its own front. The page drew both, with no culling, and the one meant
for the far side was drawn last, mirrored as seen from the near side.

How the game draws a polygon (nfs3.exe):

- **Order.** The block's lists go through `0x41b300`, which queues the
  four vertices in the file's order; the placed objects go through
  `0x41b640`, which queues vertices 1, 0, 3, 2. Everything after goes
  by the queue: the Direct3D path (`0x433840`, the first entry of the
  table at `0x5f0780`, set by `0x434870`) gives queued vertex k the
  texture's corner k (u, v at +8 + 8k), and the back-face test uses the
  queued order. So a placed object's picture is flipped left to right
  against the file's order, and its front is the other side. Checked
  on Hometown's speed signs (placed, one-sided): taken in the game's
  order, each faces the traffic on its side of the road, the sign on
  the right (block 11) the race and the one on the left (block 5) the
  oncoming lane; in the file's order they would face away.
- **Culling.** A polygon is drawn only if its screen corners 0, 1, 2
  (or 1, 2, 3) turn the front way (`0x41b432`-`0x41b493`; the way is
  `0x554e4c`, (view = 1) xor the mirror flag, `0x4349d0`), unless it
  has flag `0x10` or a vertex is clipped. With z mirrored into the
  page's world the front turns the other way, so the page winds each
  polygon backwards and culls back faces (`F` turns it off).

With both, the Tracks page reads set 0's signs as the game does: the
Taco Den and Cactus Motel on Redrock Ridge, the 50 MPH sign and the
speedway billboard on Hometown. Set 1 (`X`) reads backwards there, as
it would in the game on an unmirrored track.

**The mirrored track** (November 2026). The mirror flag (`0x6fd4c4`)
doesn't touch the geometry: the view set-up (`0x4bf260`, from
`0x4dbda0`/`0x4dbde0`) negates the projection's x scale (`0x56009c`), so
the whole 3D view, sky included, is drawn flipped left to right, and the
culling turns the other way with it (`0x4349a0`: the front is
(view = 1) xor the flag). With the flag the loader takes set 1
(`0x41dbc0`), whose signs are drawn reversed, so they read right once
flipped. An `nfs-host` race on Hometown with the flag poked to 1 shows
exactly that: the start's scene mirrored (the curve to the left, the
poles and fence on the right, the corn on the left), the cockpit and
the rest of the 2D overlay not. The Tracks page's `U` does the same:
set 1, the view's x negated, front faces culled instead of back, and
left and right controls swapped to follow the screen. Redrock Ridge's
Taco Den sign then reads the right way round from set 1.

**How the headlights are projected** (nfs3.exe, November 2026). Only
by night (`0x41c670` returns at once by day), for each car in play
within 150 of the block's centre on the ground (`0x536cbc` = 150²):

- **The light** (`0x42d160`, per car each frame). The car's light state
  at `car+0x660` is 0 off, 1 low beams, 2 high; the headlight key steps
  through 0, 1, 2 (`0x40de50`). The light sits at the car's position
  (`+0x98`) plus its up row (`+0xCC`) times `car+0x10c` (its up · the
  road's normal, set by `0x4a2690`: 1 on level road) and a quarter
  (`0x5373b0`) of its half length (`+0x11C`) along `car+0x67c`; it is
  kept at `+0x6AC`. Its frame, `+0x664` (rows right, up, forward), is
  the car's orientation (`+0xC0`, copied by the same function) turned
  about its right by 1/16 of a turn (low) or 1/32 (high) (`0x4ea9e0`,
  sine and cosine in turns), forward going down. `+0x67c` is that
  frame's forward row: nothing else writes it (the only instruction
  naming it is the `lea` at `0x42d1fc`), and the function takes the
  place before it rewrites the frame, so the place uses the tilted
  forward of the step before (of the last time the lights were on,
  or nothing at first). Low beams fade from 10 to 40 in `0xB0B0B0B0`, high from 20 to 60
  in `0xE0E0E0E0`; with `projtex` off or no z-buffer, 20 or 40 to 40 or
  60 in white (`0x42d253`-`0x42d31b`). `0x41a800` scales both distances
  by 1, less for each further car the pass has lit (down to 0.25), the
  end at least a unit past the start.
- **Each vertex** (`0x41a550`): x, y, z in the light's frame; the
  picture's u = (x / z + 1) / 2, v = (y / z + 1) / 2 (1 / z taken as 1
  below z = 1), so a square 90-degree frustum. Outcodes: 1, 2 left and
  right of it, 4, 8 below and above, 0x10 nearer than 1 (or behind),
  0x20 past the end. Within the end its colour is the light's, each
  byte (alpha too) times k / 256 (`0x41a3e0`): k = (1 − (|x / z| + |y / z|)
  / 2) times 1 up to the fade's start and falling to 0 at its end, each
  ratio held to 0..1. Behind the car a light has no colour (`+0x74` is
  0 for cars).
- **Each polygon** of the block's road and roadside at its detail, object
  layers 0 to 2 and, at high detail, the lane markings (`0x41ae90`; the
  placed objects have their own pass, below), with its first corner within the reach and
  facing the view as for drawing: if `projtex` is on with the z-buffer
  and its texture lacks `0x04`, two triangles (corners 0, 1, 2 and 0, 2,
  3) of `glw2` from `sfx.fsh` (128 × 128, two lamps and a halo; loaded
  into `0x8b4814` by `0x4d5370`), each left out if its three corners are
  outside the same side, one is past the end, or none has alpha 8 or
  more (`0x41a360`, `0x41a120`). They are queued with flag 1, blend 1:
  source alpha and one, so the picture times the colour is added by the
  colour's alpha. Otherwise each of its corners fully inside the light
  (no outcode) gets its colour raised to the light's, byte by byte
  (`0x41b0cd`), which every polygon sharing that vertex then shows.

The Tracks page lights a car at the camera, heading where it looks, its
light placed as the game's from the car's position: the camera taken as
the car's, upright, as long as the Corvette (half length 2.40644), so
1 above it and 0.6 ahead along the tilted forward (`headlightPlace`). By
night, high beams at first as a human's car starts (`startingBeams`), `E`
stepping through off, low and high as the game's key, and Shift+E turning the projection (`projtex`) off and on.
The projected triangles are added with `glw2` as above; a raised colour
is drawn as its difference from the polygon's own (baked and ambient)
colour, added over the polygon's picture. The functions are
`carHeadlight`, `headlightAt` and `headlightTriangle` in `frd.h`.

The `config.dat` that `nfs-host` writes has `projtex` 0 (the u32 at
0xFC), so its night frames show only the raised colours: road and
grass uniformly brighter out to the light's end, which stops at whole
polygons. The night clips show the projected picture: a white pool with
the lamps' lobes and a sharp far edge ahead of the car
(`Hometown_Night` 0:40, `Summit_Night` 2:17), which is what the page
draws by default.

**The placed objects** (`0x41c310`, November 2026). Each placed
object of a group drawn (`0x41ca40`: type 4 at its reference point,
type 3 at its key, others at the origin) is lit by night, except in the
mirror (view 1): for each car in the race's list in play, not hidden,
within 150 of the object's place on the ground (`0x536cb4`, a double
22,500), its roof light with the siren on and then its headlights with
them on, each skipped as for a block (`0x41bc50`; `0x41bf30` or
`0x41bc90` with the outline of the block the group belongs to) and at
full strength (`0x41a800` with 1, not reduced for the cars already lit),
raise each vertex fully inside the light (no outcode) to the light's
colour, byte by byte, as the block pass raises a flag-`0x04` polygon's
corners. That is whatever the object's textures and the `projtex`
option: placed objects never get the projected picture. Hometown's lamp
posts in the town centre are such objects (8 vertices each, two crossed
two-sided quads of picture 101, block 65's first group): in `nfs-host`'s
night race (`t4-town`, frame 2720) they stand bright green in the
player's high beams, where the page drew them nearly black. The page
now lights its still objects so, and draws a raised colour over a
two-sided polygon without culling it (it culled the far side of every
two-sided polygon's raise, so a crossed quad seen from its back got
none); with `projtex` off (Shift+E) as `nfs-host` has it, the posts,
fences and shop fronts there match the frame.

Moving objects are lit too (`0x41ca40`, November 2026): for a type-3
object the loop takes its key's matrix and position (`0x40f4b0` at
`g_step`), moves and projects its vertices (`0x41a9b0`) and then calls
`0x41c310` with that matrix and position, the same as for a still
object: the 150 test from the key's position, each vertex put through
the matrix (`0x4e0430`) before `0x41a550` lights it, the block's outline
the group's block's. The type-3 branch only runs with the loop's last
argument set; with it clear a moving object is skipped altogether. The
page lights its moving objects so, at their place this frame. `0x41c310`
also skips a car with +0x88 set (no forces, `0x4068a0`) as well as one
hidden or out of play; the page has no such cars.

Compared against a night frame (November 2026, `d5-wheel`: Hometown by
night, the player's car held from sky draw 300 on slice 600, 4 m right
of the road's point, turned to face the water wheel at the mill
(−563, 442), 43 m off; frame 2700 with the camera, cars, set-up and
`g_step` (1603) dumped at sky draw 500; `d5-wheelg`, the same run's
THRASH list). The wheel's 30 vertices placed by the page's key matrix
and projected with the game's camera land on the list's within 0.24
pixels at step 1606 (the frame is drawn three steps after the sky draw's
dump; `RELUVA_NFS_TRACK_STEP=1606`), so the matrix is the game's. Two
differences, both fixed:
- **The lit object was the mirror image.** The page keeps a moving
  object's vertices unmirrored in its mesh (z as stored, the key's matrix
  mirroring them into the world) but put them through the matrix with z
  negated for the light, so the light was worked out at the object's
  mirror image and its raised colours drawn there: a bright copy of the
  wheel beside the real one, which stayed dark. It now lights them where
  they are drawn. The game's list has the wheel's wood and paddles at
  0xC3-0xE4 grey (`0x41c310`'s raise), what the page's light gives at
  those places within 1-10.
- **The raised colours weren't fogged.** At night nearly all the lit
  scenery's brightness is the raise, which the page adds as a pass of its
  own; the game raises the corners' colours and fogs the polygon with
  them (Hometown's night fog there: value 200, density 60, black, as
  the list's batches have it). The page drew the pass unfogged: 25-35 %
  too bright at 43 m (the mill's wall 74, 68, 58 against the game's 56,
  53, 44). The raise is now fogged towards black (it is added over the
  fogged polygon) and the projected picture, a polygon of its own that
  Glide mixes towards the fog colour before adding, towards the fog's
  colour. After both: the mill's wall 60, 56, 48 and 64, 58, 50 against
  56, 53, 44 and 62, 57, 47; the trough's front 106, 99, 90 against 103,
  96, 86; the grass 71, 57, 40 against 68, 56, 36, the 2-4 of the host's
  16-bit frame (below, Brightness against the host's frames).

**Other cars' lights** (`0x41c670`, November 2026). The pass goes
through the race's cars in their list's order (`0x5efac8`, count
`0x5efd9c`) for each block: a car in play, not hidden, within 150 of the
block's centre. A police car with its siren on (+0x200 bit 0x20,
+0x734) first lights the block with its roof light, then any car with
its lights on with its headlights. Each light is skipped when it is
behind the camera by more than its reach (`0x41bc50`) or misses the
block (`0x41bf30`, for the roof light `0x41bc90`, `lightMissesBlock`):
the block's four outline corners (+0x84, `Track::Block::corners`) are
put into the light's frame (+0x688, +0x6B8, the light's world-to-light
transform made by `0x42d160`), projected with the screen's constants
as if the light were the camera (`0x56009c`-`0x5600a8`: 320, −330.33,
320, 240 in a 640 × 480 race; window `0x7d34f8`-`0x7d350c`), and the
block is left out only when every corner is wider than it is deep
(|x| × the camera's +0x54, held to 1, and 1 when 0 as the dumps have
it, beyond z, or |z| for the roof light) and all four share an
outcode. A headlight is lit with intensity 1 − 0.75 × (cars already
lit for this block / all the race's cars) (`headlightIntensity`).

**The roof light** (`0x42d020`, `sirenLight`) is a second light record
at +0x734: a unit above the car along its up row, its frame the car's
turned about the up by (step mod 32) / 32 of a turn; near plane 3.5,
fading from 15 to nothing at 30, `0xFF8080FF` ahead and `0xFFFF8080`
(+0x74 of the record) behind: `0x41a550` takes a point behind as one
ahead, negated, in that colour. Its pass (`0x41ab40`) raises the
corner colours of every polygon of the road, roadside and object
layers 0-2 (not the lane markings), whatever the texture, and is not
counted for the intensity. A corner in several lights is raised to the
highest, byte by byte.

**The lights a car starts with** (`0x42bcb0`, `startingBeams`): off;
low beams in the weather; by night high beams for a human's car
(+0x200 bit 4) and low beams for the others. The computer steps them up
for 64 steps when held up (`0x40de50`, back by `0x40deb0`). An
`nfs-host` night race on Hometown (`-dumpmem` of the cars at
`0x5e1120`) has the human on 2 and all seven computer racers on 1,
their lights at +0x6AC 0.88 above +0x98 and their fades 20 to 40 (40 to
60 for the human) in white, `projtex` being off.

**The roadside brightness at night is the player's own high beams.**
Hometown's night frames light the road, grass and fences to the bend.
The same race with `oppnumber` (`0x6fd4d0`) 0 and traffic off
(`0x6fd4e8` 1) shows it just as much, alone on the grid; poking the
player's +0x660 to 0 each frame (`-pokeevery at=0x47a880:0x5e1780:0`)
leaves road and roadside dark. Other cars only add their own pools.

The Tracks page's `J` puts three cars on the virtual road ahead of the
camera, from its nearest slice: two Corvettes going its way 15 and 35
units on, in the first and second lanes on their right, and a police
Corvette (`Vcop`) coming 25 on in the first lane on its right, its
siren on. Each is placed as the game places a car:

- in the middle of its lane as the computer aims (`0x40daf0`): lanes
  are numbered 0-13, 7 and up right of the road's point, a lane the
  slice's lane width / 2 wide, lane L ≥ 7 centred (L − 7 + ½) widths
  right, L ≤ 6 (6 − L + ½) widths left; kept within the walls less the
  car's half width;
- its box on the road: there is no suspension, and `car_clearance`
  (`0x4a0440`) is 0 when the box's lowest corner touches the road's
  plane, so the position is the half height (+0x118, the model's
  `0x2C`, set by `0x4a0d20` from the model) along the road's normal; the
  model is drawn 0.145 higher in y (`0x540120`, `0x4bab70`); its rows
  the road's;
- driven as the computer drives (below; `speedsF.bin`'s speeds, and
  `speedsR.bin`'s going the other way), its side offset driven towards
  the computer's aim (below);
- with the lights a computer car starts with, its headlights placed by
  its own half length (+0x11C, the model's), and its headlight part
  (12) drawn only with them on, as `0x4bab70` does.

They light each other and the track as above (and are lit by the
camera's car), their colours, reflections, glows, the rain's flares,
the fog's beams and the roof light's sweep as in [fce.md](fce.md),
Lighting.

`RELUVA_NFS_TRACK_CARS=<dump>` places them instead from a dump of the
game's car records (`-dumpmem ...:0x5e1120:0x4d60:file`, 0x9AC bytes a
car; `parseCarDump`): each car in play and not hidden (+0x89, +0x200 bit
0x40) at its +0x98 with its rows (+0xC0), lights (+0x660), brake
(+0x509), roof light (+0x734 with +0x200 bit 0x20) and beam scroll
(+0x944), a police car as `Vcop` and any other as the Corvette, held
there. With `RELUVA_NFS_TRACK_CARS_SETUP=<dump>` (`-dumpmem
...:0x6fd52c:0x360:file`, the race's set-up records, 0x6C bytes a car;
November 2026) each car gets its own model: the car record's +0x220
points at its set-up record, whose first byte, the car's key, is its
`carp.txt` serial number (net.md: 0x13 `Corv`, 2 `f355`, 0xF `Cout`),
and its paint is the model's primary colour nearest the
record's +0x8A8-+0x8B0 (hue, saturation, value; fce.md, The skin's
paint), the reflection taking +0x8B0. The human's car is left out, the camera standing for it, unless
`RELUVA_NFS_TRACK_CARS_HUMAN` is set. With `RELUVA_NFS_TRACK_CAMERA`
from the same sky draw the page shows the game's frame's cars;
`RELUVA_NFS_TRACK_STEP=<g_step>` (`0x7d3684`) holds the moving objects
at that step's keys.

The night comparison again with the game's own models (November 2026:
`t6-night`'s 150th sky draw, camera, cars and the set-up dump of the same
deterministic run, `d5-night-setup.bin`, whose end state hashes the same,
30c7ac9b95d5cee3, as `d6-tlog`'s): the two nearest cars, about 10 pixels
long in the game's 640 × 480, are a light car and a black one with red
tail lights on both. The brightest quarter of each car's pixels: the
light car 59, 50, 51 in the game against 50, 50, 56 on the page, the
black one 42, 28, 24 against 42, 39, 35; the game's are a little redder
(its tail lights take more of so few pixels, and its 5:6:5 colour rounds
blue and red to 8 levels). Nothing was found to fix at that size; a
nearer car in a night frame would settle it.

**Night cars at size** (November 2026, `d7-runs`). Two deterministic
Hometown night races (`nfs-host -deterministic 50000`, 5.1 billion
instructions) with the player held on a slice and two computer cars
held just ahead of him from the 300th sky draw (`-pokeevery` of their
positions and rows, `facepoke.py`), dumped at the 500th (camera, cars,
set-up) and their THRASH list at frame 2700 (`-gpudump`; a second run
the same for the frames): `d7-night1` on slices 230-234 (an F355 12 m
ahead, 85 pixels wide, a Diablo 24 m ahead coming the other way) and
`d7-lamp2` on slices 554-557 by the town's red light (type 7, slice
558). The list gives each car's lit colour exactly: its vertices' colours
are `carShade`'s base (a third, a third, a half) and top, so the colour is
three times the darkest vertex's red and green and twice its blue
(`0x49d8f0`). Found and fixed:
- **The camera's car's headlights** were put at the camera (in the
  cockpit, or behind the car) with the Corvette's length; the game's are
  the human's car's own (`0x42d160`: its position, rows and half length).
  With a dump the page now takes the human's car from it for that light.
  The F355: the game 156-158, 168-170, 182-183, the page 163, 176, 189
  before, 157, 170, 183 after; the Diablo 75-77, 87-89, 100-101 against
  74, 87, 100.
- **The track's lights on near cars** (`0x4b9550`, `carLitByTrackLights`).
  The nearest detail's drawing (`0x4bae00`, detail level 0 of `0x4bb5d0`,
  `carDetailLevel`: the mean depth of the car's box's corners under 30,
  × 0.75 by night, and only with the car detail option `0x6fbc1c` 0, as
  nfs-host's `config.dat` has it; levels 1-3 under 60, 120 and 300, the
  table at `0x55fdf4`) adds after `0x4b97b0` the track's lights near the
  car: for each slice from the car's − 3 to + 3, each light of that
  slice's block (so a block's lights count once for each of those slices
  in it) whose colour `0x491120` gives (the light table's, none while
  blinking off or with the Special Effects option Off), within 16 by
  `0x496550`'s distance (on 16.16 integers: the largest of |dx|, |dy|
  plus a quarter of the other, then the same with |dz|), adds its colour
  × (1 − d / 16) (`0x10000 − (d >> 4)`, each channel `>> 16`); the
  sums over their count are added, at most 255. The page left this out.
  By the red light (`d7-lamp2`) the game's cars were 174-176, 174-176,
  180-181 and 132-134, 135-137, 142-143; the page now gives 174, 175, 181
  and 134, 137, 142 with the light on (166, 175, 181 and 132, 144, 149
  while it blinks off): that is the red the game's night cars had more.
- **The glows** were filtered; they are point-sampled now (thrash.md,
  Texture filtering), their corners from texel centre to texel centre as
  `0x492370` has them (u, v 1/64 to 63/64 of `glw3`'s 32).

The tail lights' glows then match: the F355's left one's red pixels
centred 1.7 pixels higher on the page (the page's cars are placed from
the dump, taken at the sky draw before the frame's poke; from the raw
dump they were 6 pixels higher), their mean 93, 24, 32 against 92, 22,
27; the box of the F355's back 57, 41, 49 against 52, 39, 45, the
Diablo's 61, 57, 68 against 54, 56, 68. The reflection's colours were
already the list's (`7e63637e`, `88626288`, `845f5f84`).

**The car's shadow** (`0x4b81a0`, November 2026; `carShadowQuad`,
`drawShadows`). Each car's drawing at every detail calls it
(`0x4baa50`, `0x4bab70`, `0x4bae00`, `0x4bb110`, `0x4bb200`); it does
nothing for a car off the ground (+0x184 not 0) or without a road
contact (+0x20 0). Read from the code:
- `0x4b7f80` takes the box's four bottom corners, (∓ +0x114, −+0x118,
  ± +0x11C) in the order front left, front right, back right, back
  left, into the track by the car's rows (+0xC0) and place (+0x98), and
  puts each on the road: the height `0x4968a0` gives on the car's road
  frame (+0x124), or the road `trackpos_build` finds there when that is
  up to 0.25 (`0x4000` in 16.16) above it.
- By day, out of the weather and out of the mirror (view 1), each
  corner is pushed along its two edges, to the corners before and after
  it (`0x4b7a30`: 3 and 1, 0 and 2, 1 and 3, 2 and 0): e the unit
  vector from the neighbour to the corner (`0x4e0000`, `0x4e01c0`), the
  corner moved by e × −(e · L) × +0x118 when e · L < 0 (`0x4e0210`,
  `0x4e0050`, `0x4dffb0`), L the horizon's light direction (`0x55d2a8`,
  fce.md, Lighting): the shadow stretched away from the light by the
  car's half height. By night, in the weather or the mirror it lies flat
  under the box.
- All four raised 0.125 (`0x53ffc0`), projected, and sent as one quad
  (`0x4bbe90`, sorted by the car's depth, `0x4bc0b0`) with the picture
  `0x8b4898`, which `0x4d5370` loads from `sfx.fsh`'s `shad` (32 × 32,
  32-bit: colour 5, 8, 23 and an alpha of 0xFF inside a soft edge of
  0x00-0xE4), each corner's colour 0x6FFFFFFF; a quad whose screen
  winding is the wrong way (seen from below; reversed for a car on its
  roof, +0xD0 < 0) is not sent.

In a race's THRASH list (`d8-day1`: Hometown by day, the player and two
cars held as `d7-night1`'s, `-gpudump 2700`) each car's shadow is a
batch of two triangles of texture `shad`, blend on in mode 0 (source
alpha, inverse source alpha), the corners `6fffffff` with u, v from 1/64
to 63/64 in the corner order above. The page now draws it so, over the
road and its markings without writing the depth; its corners put
through the game's camera (`d8-runs/proj.py`) land within 0.2 pixels of
the list's for both cars (the F355's (206.2, 284.0), (267.7, 283.5),
(237.4, 297.0), (147.6, 297.5) against (206.2, 284.0), (267.7, 283.5),
(237.4, 297.1), (147.6, 297.7)), cast to the left and back as the
game's. By night (`d7-night1`'s list) the flat quads' x match to 0.1
pixel and their y are 1.4 pixels off, as that run's cars are
(above). The page's cars never leave the ground, and the page has no
mirror view.

**Its brightness** (November 2026, `e6-runs`: Empire City by day, Hot
Pursuit, racer 1 held 18 m ahead and an officer poked beside it as
`d8-t8g0`'s, frame 2816, the page from the 400th sky draw's dumps;
`t8o`, and `t8d` without the officer, whose list 2818 was written). The
road round the officer's red light was 1-2.6 brighter on the page, but
the strip under the held car's shadow (440-500, 258-263) 8.0, 9.1, 10.4
in red, green and blue. The road itself is drawn alike: of the 346
corners of the road texture's polygons taken back to the track with the
dumped camera (`e6-runs/vc`), 341 have the `.frd`'s colour exactly (the
other 5 are clipped corners); the road pictures are `tr080.qfs`'s within
the 5:5:5 widening (0.3-0.5 a channel); fog 475 in `1c0a05` both. The
shadow's picture is not: `shad` is a 32-bit shape with a soft alpha, and
the game hands it to the card at 4:4:4:4 (`0x4d37a0` → `0x4d27e0`, each
channel's top four bits, thrash.md, Textures as loaded), so its colour
(5, 8, 23) arrives as (0, 0, 17) and its alpha a step lower; the list's
texture is that, texel for texel. Taken so by the page (`decodeForGlide`,
also for `glw0`-`glw4` and the particles' pictures), the strip is 6.4,
6.9, 8.5 over the host's frame, 4.5-5.5 over a host that rounds its
blend (thrash.md, Dithering), and 2-2.5 in `t8d`'s. The strip is five
rows tall between the car and the road, and there the difference
changes sign from row to row (the page's window, 800 rows, scaled to
480), so part of what is left is the comparison's.
The road beyond it is 0.4-2.6 over (0.1-1.6 over the rounding host).
Part of the strip's excess in `t8o` was the smoke drawn over it with its
picture mirrored (col.md, Its spread over the car): with the picture's
corners as the game sets them the strip is +3.9, +4.3, +5.9 (`e7-runs`).

**The police car's driving** (`aiCopStep`, November 2026). The page's
police car drives as a computer cop (`car_driver_cop`, `0x4032e0`;
[physics.md](physics.md), The computer cops' driver), not by the racers'
rules. Its driver acts when the gate lets it, (step / 2 + its place in
the list) mod 4 = 0 on the 32 Hz task, every 8 steps; between, only the
aim and the speed move (`0x408870`, `0x4066c0`, as for a racer). With
its siren on it starts in state 7 (a run with the siren), else 1
(patrol), both `cop_ai_drive` (`0x403270`): `lane_votes_start` (its
lanes, keeping right, the cars round it), the speed wanted the track
section's speed (`0x40ed40`, per-track slice ranges) × 2 or × 1.19, at
most its top speed (`0x401370`: `carp.txt`'s maximum velocity, field
14, × 1.1 with +0x711 bit 1), then `0x40bff0` (behind a slower car in
its lane its speed less 10 × the share of round(its speed × 0.579 −
0.5), at least 4, slices it is in; × 0.4 for one coming at it),
`lane_votes_ahead` and `lane_choose`. No flashing, no best line, none of
rows 8-10 and no `0x409660` bonus: those are the racers'. Then
`cop_ai_pick_target` (`0x402e10`): a racer within 16 slices over the
section's speed by more than 6.71 m/s (`0x403f80`, `0x536104`) scores
max(cops / 3, 1) less the cops already on it, and the best scoring
over 0 becomes its target: state 5, the chase, the siren on. Chasing,
its speed wanted is `0x402420`'s: the top speed more than 8 slices
behind or when the target is faster, else slices × (top − its speed) ×
0.125 + its speed + 5.81 m/s; past it × 0.5 its speed (× 0.2 when it is
the car behind in the cop's lane, `0x56ef54`); its votes `0x402750`'s
(rows 6 and 13: 70 for the target's lane when it comes the other way, 10
behind it or more than 5 slices on, else 30 beside it and 10 its own);
under 15.65 m/s or on its slice, within 4 slices, it aims at the
target's side offset (+0x711 bit 2, `0x406b60`). A target running the
other way turns it (`0x402910` flips +0x621 bit 0x10) when it isn't 12
slices past it; the page turns it on the spot from standing, where the
game's car turns on the road. Over 50 slices away (`0x5521ac` +4 for a
racer with no tickets) the chase is given up: patrol, siren off.

**The police car's orders, strips, roadblocks, reversing and stops**
(November 2026; `aiCopStep`; physics.md, The computer cops' driver).
The page's police car now runs the driver's other states as the code
has them:
- **The chase** (`cop_ai_chase`, `0x4025c0`): within 16 slices, or with
  the target pulled over, `0x402420`; 16 or more behind it, the top
  speed; 16 or more past it, the section's speed × 1.19, facing the
  target's way. `0x402420`, past a target, under 3 m/s and more than 7
  slices off, **reverses** (`ai_reverse_set(0)`, +0x7D4): −9.83 m/s
  (`0xc11d5ac4`), then slices × 5 (`0x53608c`) while past it and the
  target's speed × (slices × 0.1 + 1) once behind it; reversing, the aim
  is the road's point (`ai_reverse_end` sets +0x7C8 to 0 each model
  call). The chase is given up past `t_ticket_times` (`0x5521ac`, by the
  target's tickets: 50, 50, 70, 70, 90 slices; +4 of the record with
  +0x711 bit 1).
- **The stop** (`cop_ai_stop_check`, `0x402110`): with the target
  pulled over (its record's +0x1C = 2) and stopped (its state 6) within
  16 m (`0x536074`), or the record's +0x28 past; else within 20 m
  (`0x53607c`, 40 with three cops stopping it), the target at 11.18 m/s
  or less (`0x56ea50`), the cop on the ground and upright (> 0.5): held
  192 steps, or for a computer racer 160 when it is more than 60 slices
  ahead of the leading human (its +8 less `0x56ebb8`'s +8) and 224 when
  more than 60 behind (`0x40237a`; `aiStopHold`, the page's camera
  standing for the human), then the record's +0x1C = 2 and
  +0x28 = step + 320. The racer (`car_driver_ai` states 5 and 6): 11
  m/s (`0x408b30`) and +150 for the nearer road edge's lane
  (`0x408bd0`) until in the lane past the road's lanes, then 0; under
  3 m/s, or after 320 steps, stopped (6, `car_stop_hard`: the speed
  halved a call). The cop's stop (6, `cop_stop_begin` `0x401e40`): the
  siren off, the record's +0x28 = step + `t_ticket_times`[tickets]
  +0xC (384 steps, 448 from 5 tickets); past it the ticket (`cop_ticket`
  `0x401ff0`): the record cleared, a ticket more, free of chases for
  +8 (960 steps after the first), `cop_after_ticket` (`0x4040e0`)
  radios, and every cop on the racer or watching it lets it go (no
  chase lost; a watcher's strip taken up) and patrols; the racer's
  +0x6F0 and chases lost are cleared.
- **The officer on foot** (November 2026; `drawOfficers`): as the stop
  begins `cop_officer_place` (`0x401c20`, `0x401b50`) puts him 2 m from
  the racer along its slice's right towards the road's middle, at the
  road's height, upright and turned to the racer's heading
  (`mat_rot_y_turns` of +0x210, not the road's slope); `draw_officer`
  (`0x4b9bc0`) draws him within 100 m, until the ticket (`0x401e20`).
  His model is the track's `Render/pc/copN.fce` with its `.art` pages
  (`0x4b7c60`, loaded with `0x49d450`: cop0 on tracks 0 and 4, cop2 on 2
  and 6, cop3 on 3 and 7, cop4 on 8, cop1 on 1 and 5), raised by its half
  height (+0x2C). Read again in November 2026 (`0x4b9bc0` and the
  functions it calls):
  - **The spot on the road** (`0x401d78`): after `0x4205a0` builds a
    track position for the spot at the racer's slice, the side is kept
    only when its +0x6C is 0 (inside a polygon of the slice's run; the
    run's polygons share edge 1-2 of polygon i with edge 0-3 of i + 1 on
    every track, 10,665 of 10,665 pairs on Hometown, all but 118 of
    the nine tracks' 149,877), the polygon and its record are set and the
    record's surface (byte 1 & 0x3F) is neither 0 nor 14; else he goes to
    the other side, unchecked (`sliceRunSide`, `officerSpotOnRoad`).
  - **His lighting**: as a car's, not two fixed colours. The colour is the
    road's under the racer (`0x4206f0` on the racer's track position, the
    mean of its polygon's four corners); by night, out of the mirror,
    with the other cars' lights on the racer (`0x4b97b0`, `carLitBy`) and
    the track's (`0x4b9550`, below, The track's lights on near cars);
    then `0x49d8f0` (`carShade`: a third, a half and twice the colour)
    and `0x49da30` per vertex against the horizon's light direction
    (`0x49d760` with `0x55d2a8`), when `0x6fbc20` is on (`0x49d750`; on
    from 167 MHz up). `0x49d800(0x4F4F4F4F, 0x7F7F7F7F)` gives the two
    colours of the **reflection** (`0x55e6dc`, `0x55e6e0`, × the effects
    intensity under Direct3D), and the model is drawn with the sky's
    sphere map (`0x563a90` → `0x6ff9a0`, +4 its handle) as the last
    argument of `0x49d5c0` → `0x49de20`, so the triangles without fce
    flag 1 reflect it: only cop2 has any (12 of 439; the other four
    models' triangles all have flag 1). The page had taken the two
    reflection colours for his light and dark.
  - **Track 8's red light**: with the model's dummy count (+0x34) not 0
    (cop4's one dummy at 0.03, 0.80, 0.24), the dummy (+0x38) through his
    frame and place (`0x4e0430`), and d = his forward · the view's
    forward (view +0x2C): when d < 0 (he faces the camera) a glow
    (`0x492370`) in 0xFFFF0000, size −d × 0.1 (`0x5400f8`), at his nearest
    depth (the least view depth `0x49d5c0` returns).
  The page now does all three.
  - **Checked on screen** (November 2026, `d8-runs`). `draw_cars`
    (`0x4bbba0`) calls `draw_officer` for each car drawn (+0x8B4, its
    detail, not −1) with +0x8E4 and +0x8E8 both set, whoever set them;
    a dump's car records hold him (+0x8E4-+0x918), and the page now
    draws a dump's officer where it has him (`DumpedCar::officer`).
    - Hometown by day, a stop by keys alone (`d8-stop`: `r1-ram4`'s
      keys, the player's cop ramming racer 1, H at 64.4 s; Hot Pursuit
      and car 12 poked at `0x474ca0`): the officer at (742.79, 22.14,
      −266.21) from 65.07 s; the dump at the 545th sky draw (65.82 s,
      frame 2741) and that frame's THRASH list (`d8-stopg`). His
      triangles' colours are the racer's: base `ff55556d`, from
      0xFFFFFFDB, the page's colour for both to the bit; the frame and
      the page agree on his place, pose and size (his body's mean
      39, 39, 43 in the game, 40, 40, 50 on the page). The page's
      camera is the dump's (the scene's fixed one by the racer).
    - Track 8 by day and night (`d8-t8g0`, `d8-t8g1`, Hot Pursuit with
      the player a racer; racer 1 held 18 m ahead with `facepoke.py` and
      an officer poked beside it, +0x8E4 = +0x8E8 = 1, +0x910 2 m to
      its left, +0x8EC facing the camera, from the 300th sky draw;
      dumps at the 400th, lists 2818, frames 2816): the list's red
      light is a `glw3` quad in `ffff0000` (blend mode 1), 3 pixels
      across at (427.5-430.4, 229.6-232.6), depth 18.7, by day and by
      night; on the page it falls on the same pixels (red at most 123 by
      day and 98 by night in the game's frames, 99 on the page's by
      night). By day the officer's base is `ff553a39` in the list and
      on the page. By night the list has `ff2d2939` (0x877B72: the
      road's 0x261A11 plus 97 from the player's headlights, the only
      light reaching him, `0x4b97b0` dividing by the lights that
      reach); the page gave 0x7C7067 with its projected headlights on,
      and exactly 0x877B72 with them off (Shift+E), as `nfs-host`'s
      `config.dat` has them, so for a comparison the page is to be set
      so. Nothing on the officer or the red light differs.
- **Orders** (+0x7AC, `cop_ai_order` `0x403830`, taken by
  `cop_ai_order_start` `0x403030` once, and again in patrol at each call
  without a target): kind 0, to the roadside: facing the order's first
  slice (`0x4028e0`), patrol until within its range (`0x40f000`: the
  first slice to first + length), then state 3 (6.5 m/s, +100 for the
  nearer edge's lane, `0x402d90`, and rows 5-7 and 1) and, off the lanes
  (`0x402f70`), 4: stopped, and with +0x711 bit 8 **a strip**
  (`cop_ai_lay_spikes` `0x403d30`) at its slice, lane 7 (6 for an odd
  cop). Kind 4, **a roadblock**: the siren on, 8 (× 2) to the slice,
  9 (6 m/s), 10 at 6.5 m/s or less (radio `0x40bac0`); with no racer
  within 240 m (`0x5360f4`) a quarter turn on its slice (`0x403180`),
  speed 0: 11, across the road, stopped hard (`car_stop_hard` three
  times a call); with one near it keeps to its lanes. Kind 2, patrol
  facing +6's way; kind 3, the run with the siren.
- **The strip** (`spike_lay`, `0x4c9dd0` with `0x4c9bc0`): across the
  lane's middle, 0.75 lane widths either side (`0x18000` × the half's
  lane width / 2), 0.5 above the road (`0x540360`): 12 m on Hometown's
  8 m lanes, as `r2-cop5` measured. Its record holds a quad: the two
  ends on the road at the slice's point and the same two 0.5 higher
  (`0x540360`; `aiStripQuad`). Each step (`car_spike_check` `0x4297d0`,
  `spike_hit_test` `0x4ca160`) a racer in the race, on the ground, within
  a slice of an armed strip is tested wheel by wheel: the quad's four
  corners are taken into the car's frame from the wheel (its record's
  track positions at +0x304 + wheel × 0x78) and given out codes against
  a box of ±0.5 m (`0x540388`, `0x540390`); a wheel whose four codes
  share no side is on it (`aiStripWheels`, the page's wheels the model's
  parts 1-4 on the road). A hit clears the strip (`0x4c9f50`), sets
  +0x99C to the wheels and +0x621 bit 0x40, pulls the racer over
  (`spike_hit_racer` `0x401650`: record +0x1C = 2, the cop's +0x711 bit
  8 off) and, when its +0x6F0 is over 2 m/s, a cop is on it or it has
  lost a chase, the strip's cop takes it if it has no target, the
  record's +0x28 = step + 320 with a cop on it. A car at 40 m/s moves
  0.6 m a step, so a wheel can step over the 1 m box. **+0x6F0** is
  not the speed: it is the most the racer has been over its track
  section's speed while a computer cop looked at it
  (`racer_overspeed_update` `0x4014b0`, a maximum, from
  `cop_ai_pick_target` for each racer within 16 slices of a free cop and
  from `car_driver_cop` for the target each call; a human cop's
  `0x4052e0` for racers within 50 slices), 0 after a ticket
  (`0x401ff0`). Read as an integer against 0x40000000 it is over 2.0.
  The cop
  watching (+0x730) radios once its racer is past the strip by less
  than 30 slices (`0x403df0`, `0x40bca0`: "missed the strip") and clears
  it 50 past; a roadblock's cop gives up when its racer is 5 slices past
  it (`0x403ed0`, radio `0x40bd00` from state 10).
- **Waiting** (`cop_ai_pick_target`): with a strip out or at a
  roadblock, a cop takes a target only once its watched racer has been
  within 4 slices for 256 steps, or after 960.

**The calls for help** (`car_driver_cop` `0x4032e5`; November 2026):
every 320 steps of a chase (the record's +0x24, the first 640 after the
chase began, +0x20) a chasing computer cop with no human cop in the race
calls. The cop that asked last (+0x5C) only clears that. Another asks for
a strip (`cop_ai_call_strip` `0x4039c0`): with none ordered (+0x58) and
the heat, chases lost (+0x14) + cops on it (+0x2C) + tickets + (step −
+0x20) / 2048, over 6, the first time only a radio call (`0x40b910`,
+0x54 bit 2); after that the free cop nearest ahead of the racer, 51
slices to ¾ of the track by slice number (negated on a track run
backwards), gets the strip order. Failing that, a roadblock
(`0x403b20`, human targets only) and then backup (`cop_call_backup`
`0x4043a0`): only while the racer still scores for more cops
(`0x403f80`: max(cops / 3, 1) less the cops on it, so never with five or
fewer cops and one on it); a radio call once (`0x40b720`) 640 steps into
a chase with one cop on it; the nearest free cop on the racer's other
side within twice `t_ticket_times`' slices, and every free cop with
+0x711 bit 1 ahead within 8 times them, on a run with the siren (kind
3, length 8) to halfway between it and the racer (`0x404340`).
`cop_ai_target_score` also counts a racer that has lost a chase or
whose +0x6F0 is over 2, as well as one over the section's speed by 6.71.
A cop that takes a target while watching a racer for a strip or a
roadblock stops watching and takes its strip up (`cop_set_target`
`0x4018b0`), and the last cop to lose a racer not caught by a strip
counts a chase lost (`cop_lose_target` `0x401a80`, never while it is
pulled over). The page runs all of this with a **second police car**
(J puts it 800 m on, patrolling the camera's way; its index 1, its
strips in lane 6); with two cops the racers' heat reaches 6 only after
chases lost or tickets (or about 3 minutes of one chase), and backup is
never sent. Its **key C** gives the free cop (the first without a
target) the orders those calls give, in turn: a strip (kind 0
from its slice − 6, 3 slices, +0x711 bit 8), a roadblock (kind 4, 20
slices on along its way) and a patrol, each watching the racer nearest
it, the chase dropped. A racer close by and speeding is taken as a
target first, as the game's cops do. The radio plays no speech: the info
line shows the police car's state and its last call for 5 s (of
physics.md's table, those the page's cases make: a target taken
`0x40b220`, a strip or roadblock asked for `0x40b910`, `0x40b860`, a
strip laid `0x40b9c0`, a roadblock set `0x40bac0`, missed the strip
`0x40bca0`, ran the roadblock `0x40bd00`, ticketed `0x40b670`); strips
are drawn as dark bars. A run (`d5-runs/sim/copsim`, Hometown, two
racers at 35 m/s from slices 300 and 280, the police car at 360 given
the strip order): it turned back, reached the roadside at slice 357 at
2.3 s and laid the strip in lane 7 at 2.4 s; the first racer ran over it
at 52 m/s at 7.7 s, pulled over (5) and stopped (6) at slice 375 by
11.8 s; the cop began the stop at 12.7 s and gave the ticket at 18.8 s
(384 steps); both drove on. With the roadblock order (kind 4 at slice
380) it was in place at 7.4 s but, the racers within 240 m, never
turned across: the first racer passing at 57 m/s became its target. A
racer held at 10 m/s was held 192 steps within 20 m, pulled over,
stopped and ticketed.

**On the page** (November 2026, Xvfb, Hometown by day, the camera turned
to look against the slices, J then C at once, Shift+C following the
ordered cop; screenshots only for review, not kept): the second police
car, 800 m on, turned back, stopped at the roadside and laid its strip
in lane 6 (the racers' first lane going that way) within about 3 s;
about 20 s on a racer at 51 m/s over the section's speed (+0x6F0) ran
over it at step 1461-1488, was pulled over and stopped; the cop stopped
it with the officer standing at the racer's road side, and gave the
ticket ("1 ticket", radio "suspect ticketed"). In runs where the racer
stayed out of lane 6 it went past ("missed the strip"), and when the
cop's wait for its racer ran out it took it as a target and took the
strip up, as `0x4018b0` does. Two faults of the page's were found and
fixed on the way: a cop's step changed the racers stepped before it only
for that step (a racer it pulled over was let go again), and the strip
watch counted slices the slices' way where the game counts them the
race's (`g_backward`): the page's racers may run either way, so it is
taken by the watched racer's way.

**The computer cars' driving** (nfs3.exe, November 2026; `aiStep`).
An AI racer runs `car_driver_ai` (`0x409c00`, state 1) every 8 steps and
`car_ai_model` (`0x408600`) every other step (below, When the driver
acts). The page runs the parts that
pick its speed and lane for each of the three cars in turn, and moves
them along the slices at the speed that leaves:

- **Its lanes** (`0x40e350`): +0x638 the lane at its side offset
  (`0x40e2b0`: 7 + round(offset / the right lanes' width), 6 + ... on
  the left, with the x87's rounding to the nearest, 0-13), +0x63C and
  +0x640 those at offset − and + (its width across the road − 0.5).
- **The speed wanted** (`0x409330`): the speed table's byte for its
  slice pair, held under the top speed cap × the track's factor, then ×
  the speed scale (+0x7BC: the driver's factor `0x408dc0`
  × the track's `0x40ee80` × the catch-up `0x4090a0`, at most 1: the
  page takes the camera's car as the race's human, at race distance 0,
  and each racer's distance as the slices from the camera's slice along
  its way). Until November 2026 the page held the scaled speed under the
  cap (below, Why car 3 didn't overtake). The page's two Corvettes
  have Lead Foot's (skill 2) and, ahead of him, Mulligan's (skill 0, a
  surging driver), from the drivers' table (`0x55226c`).
- **Flashing** (`0x404840`): first the flash is undone when its 64 steps
  are up (`0x40deb0`: high beams back to low, low back to off); then,
  with a car ahead in its lane within 2 slices (`0x56ef3c`, `0x56ef48`
  from `0x40cfc0`) and the speed wanted more than 8.94 m/s
  (`0x536194`) over its speed, past the race's start (`0x40f050`, below), the
  lights go a step up for 64 steps (`0x40de50`: off to low, low to high,
  high to off). With a human ahead it also sounds its horn
  (`0x411750`). In practice a car flashes when it is held up behind a
  slower one, or while accelerating out of a bend with a car close
  ahead.
- **Keeping behind** (`0x4094d0`): behind a car going its way in its
  lane, not a police car, that is slower than it wants, within 2 slices
  its speed wanted becomes that car's; at 2, it is lowered by half the
  difference.
- **The lane votes**: 14 rows of three counts, for the lane left of it,
  its own and the one right of it (`0x56ee90`), cleared each step; the
  lane with the highest sum is chosen, its own on a tie, left before
  right (`0x40da30`, into `0x56ef74`). The rows: 0, keep right of the
  way it runs (`0x40cee0`, `0x56f230` 1 on every track: +1 for a lane on
  its right, −3 on its left); 1, −4 for a side lane with a car beside it
  within 2 (`0x40d9f0`); 2, +40 back towards the road's lanes where
  they end ahead (`0x40d2e0`; on track 6, slices 295-360, −40 left for
  racers); 3, −30 for the lane just past the road's lanes, −80 if the
  wall leaves less than its half width there, −1000 beyond, then +22 for
  any row-3 count not 0 (`0x40d3d0`, `0x409660`); 4, −1000 for a lane
  with no road strip here (col.md's mask, bit 14 − lane; it also nudges
  the aim a half width away) and −65 for one without a strip on a slice
  before the one its speed reaches (`0x40d1f0`); 6 and 7, cars ahead
  within 10 slices: a slower one going its way −3 to −8 by the speed
  difference less the slices past 5, one coming the other way −35 +
  35 / 8 × the time to meet it (`0x40d800`); 8, a driver of personality
  under 3: −0.5 and −0.1 × how much faster it is and wants to be than a
  car ahead, more when close (`0x409920`); 9 and 10, +5 for a lane with
  no car ahead, −6 (−24 for a police car) for one within 3
  (`0x4097c0`); 11, the best line: the speed table's lane nibble 4 slices
  ahead, worth 6 for a driver of skill 0, else 10, or 12 with personality
  over 2 (`ai_lane_vote_line`, `0x4096b0`; not for a car running the
  other way). Halved for a car only partly in that lane. Rows 9-12 and
  the flashing only past the race's start (below). 5, the spike
  strips (`0x40d720`, November 2026): eight 0x50-byte records at
  `0x7dd850`, one per cop (in use when the first word isn't 0; a slice
  at +0x34, a lane at +0x38), are the strips `spike_lay` (`0x4c9dd0`)
  lays: a lane holding one 1 to 5 slices ahead in the car's direction
  gets −50 in row 5 (`0x56eecc`), and in the car's own lane its +0x7B0
  is × 0.9 (`0x536724`). The page has them since its police car lays
  strips (below). Left out, as the page has nothing they act on (read
  November 2026):
  - the grudge (`0x409840`): a driver of personality over 2 with
    +0x6E4 above 0, +0x7D4 0 and a target car at +0x624 sets bit 0x80 of
    +0x621 when the target is the nearest car ahead in one of its three
    lanes 0 to 4 slices on (`0x56ef38`, `0x56ef44`); no vote of its own;
  - the human behind (`0x4095d0`): a driver of personality over 2, past
    the start (`0x40f050`) and while `0x56ec80` is at most 4, adds 15
    in row 12 (`0x56ef20`) for each lane whose nearest car behind
    (`0x56ef50`, a flag at `0x56ef6e`) is a human's (+0x200 bit 4)
    within 10 slices: it blocks a human. The page's camera is no racer,
    so this never applies there.
- **The race's start** (`0x40f050`, November 2026; `aiAtRaceStart`).
  The flashing (`0x404840`), the human behind (`0x4095d0`), the empty
  lanes and near cars (`0x4097c0`) and the best line
  (`ai_lane_vote_line`) each call `0x40f050(car, 0)` first (`xor edx,
  edx` before the calls at `0x404920`, `0x4095fb`, `0x4096cb`,
  `0x4097c8`) and do nothing while it returns 1: while the car's race
  distance (+8, slices with the laps) plus 14 × its place (+0x1FC, 0 the
  leader) is at most 400 with five or more racers (`0x5efd94`), 200 with
  two to four, 50 with one, each 26 less on a backward track; the
  grudge after a crash (`0x404a00`, from `car_impacts_task`) asks with
  100 more. With eight racers that is the first 302 to 400 slices: in
  `t6-log` (Hometown, eight racers, the dumps' +8 1-205 and +0x1FC 0-7)
  the whole of the window. The page had applied them from the start.
  **Checked** (`d8-votes`, November 2026: `t6-log`'s run again, with
  `-watch 0x40daf0` and the votes `0x56ee90` (14 rows × 3), the nearest
  cars `0x56ef38`, the pick `0x56ef74` and the car's +0, +0x1C,
  +0x638-+0x640, +0x7B0-+0x7B8, +0x7C4 read at each `lane_target_offset`,
  `d8-runs/votes.py`): in 2,542 decisions from 41 to 86 s rows 9, 10, 11
  and 12 are never other than 0; rows 0, 1, 2, 3, 4, 7 and 8 are (2,542,
  289, 74, 1,685, 229, 1,571, 719 times).
- **The aim** (`0x40daf0`): the chosen lane's middle, (L − 7 + ½) right
  lanes' widths or (6 − L + ½) left ones'; for the lane just past the
  road's lanes, the car's half width past the edge; within the walls
  less the half width; then the nudge. +0x7C8 moves 0.2 a step towards
  it (`0x408870`), the car's +0x7B8 following. A car aimed at a lane's
  middle 1.5 widths out reckons itself a lane further on (1.5 rounds to
  2), so the lanes' votes are taken from there; next to a lane without
  a strip it is nudged back.
- **Its speed** (the force model since November 2026, below; before,
  `0x4066c0`'s): towards the speed wanted, up by the
  acceleration table of its `carp.txt` (P +0x2EC, one value a m/s at
  round(speed − 0.5), × 0.9 with ai Normal past race distance 100 (only
  then: the page took it from the start until November 2026) and
  × 0.9 in the weather under 45 m/s; nothing over the top speed cap ×
  its scale), down by the maximum braking deceleration × 1.25 for an AI
  racer (× 2 for a police car instead; `0x4063e0`). **The brake** (+0x505,
  and +0x509 for the glows) is 255 only when it is more than 5 m/s too
  fast (`0x53624c`); under that it slows without its brake lights. The
  speed tables drop at bends, so the cars brake into them.

- **When the driver acts** (November 2026). `car_driver_ai` goes
  through the same gate as the cops' driver (`0x40df10` → `0x401240`,
  `step_phase_skip(8, car +0)`): a 32 Hz task let through when
  (step / 2 + the car's index) mod 4 is 0, so a racer's speed wanted and
  lane are worked out every 8 steps, each car on its own phase
  (`aiDriverActs`). Its model (`car_ai_physics`, `0x4088f0` →
  `car_ai_model` `0x408600`) is a 32 Hz task too, its time step
  `0x56ec58` 1/32 s: the aim moves 0.2 a call (`ai_lane_ramp`), 3.2 m in
  half a second, as the `t6-log` dumps show (car 1's +0x7C8 from 5.0 to
  8.2 to 11.4), and the speed changes by the acceleration × 1/32. The
  page ran both every step, the aim at twice the game's rate.
- **The side offset as measured** (November 2026). +0x7B8 is not the
  car's own place across the road but `0x4a2bc0`'s (from `0x42ce70`
  each step): the car's position less its slice's road point, along the
  right of the road polygon under it (+0x124, `car_road_frame`). On a
  bend the line from one slice's point to the next moves across the
  slice's right (on Hometown's first 200 slices by −0.5 to +0.5 m, 1.7 at
  slice 170), so a car going straight on measures a side offset changing
  across each slice and jumping back at the next, and its controller
  steers by that. A car aimed at a lane's middle sits on the rounding
  boundary of `0x40e2b0` (½ a width), so its +0x638 flips between the two
  lanes with every few centimetres: in the dumps car 6 at +0x7B8 3.86 to
  4.20, lane 7 below 4 and 8 above. The page now keeps the car's own
  place (`lateral`, drawn) and the offset as measured (`side`, +0x7B8:
  `lateral` + how far it is along the slice × the slice's chord,
  `aiChord`), steers the spring by the measured one and takes the lanes
  there. The polygons' own road vectors (the game's +0x124) aren't used:
  the slice's right stands for them.
- **The side offset as driven** (+0x7B8, `aiMove`, `aiSteerStep`): the
  game's car is moved across the road by its controllers and its lanes
  (`0x40e350`) are taken at +0x7B8, the car's own offset, not at the aim.
  The page drove each car exactly on its aim until November 2026, then
  by a fitted spring, and since by the game's model (below, The
  controllers).

**Against the game's racers** (November 2026, `t6-log`: Hometown by
day, the human left on the grid, the cars' records dumped every 0.5 s
from 50 to 78 s; `t6-sim/aisim` runs `aiStep` with the catch-up from the
50 s dump, every car with the Corvette's `carp.txt`, `behave.py`
compares). Over the 28 s the seven racers' speed scales match the
dumps' (0.656-0.738: 0.82 × the driver × 0.9 or 0.8 by place; the page's
places differ from the game's now and then, so car 2 stayed at 0.656
where the game's went up to 0.738), their mean speeds are within 3 m/s
(34.8-38.3 in the game, 36.7-38.1 on the page) and they end a few slices
from where the game's are (1-12). The game's cars weave: +0x7B4 goes between
lanes 7, 8 and the one past the road's edge every second or two, and
+0x638 changes in 17-23 of the 56 half-second samples per car. The
page's changed in 2-6 with the car on its aim, and in 7-21 with the side
offset as driven. Braking: one sample in the game (car 1), none on the
page. Flashing: none in the game's samples, one on the page (car 1,
held up at the start).

Again with the driver's gate, the 32 Hz model and the side offset as
measured (`d5-runs/sim`, `aisim` with each car's phase its index,
November 2026): +0x638 changes in 21-29 of the 56 samples per car (the
game's 17-23); the gate and the 32 Hz model alone gave 5-18, so it is
the measured offset flipping at the lanes' middles that makes the
weaving. Mean speeds 36.5-38.0 m/s (the game's 34.8-38.3), braking in
one sample for five cars (the game's one car), flashing 1-3 times for
two cars (none in the game). The page's cars now aim at lane 7's middle
(4 m) most of the time as the game's do, but cross to lane 8 less often
(3-14 samples aimed at 12 m against the game's 7-40): the rest of the
difference is the game's own steering, which the spring stands for.

**The bend's cut** (`aiBendCut`, November 2026, `d6-runs/sim`; taken
out again). The page had read `0x406e50`'s point ahead as what the car
follows, and pulled its spring to the aim plus the point's offset across
the car's slice (at most 6 m). The controllers (below) show the point
only turns the car, and only the slow and sliding controllers make it go
where it points: replayed against the game's own +0x7B8 (`d7-watch2`,
below) the aim plus the cut is 5.14 m rms off, the aim alone 0.67.

**The controllers** (November 2026; `car_ai_model` `0x408600`,
`ai_ctl_rails` `0x4076b0`, `ai_ctl_slow` `0x407c30`, `ai_ctl_slide`
`0x407fa0`, `ai_integrate` `0x4071a0`, gains from `ai_gains_init`
`0x405e60`; `aiSteerStep`). Each 32 Hz call the aim moves 0.2
(`ai_lane_ramp`), `ai_heading_error` (`0x4070c0`) takes the heading less
the direction to the point (`0x406e50` → `0x406d40`: the slice from
`0x406980`, moved across along the car's road right by the aim), in
turns within ±0.5, and one controller runs:
- `ai_ctl_slow` reversing or going backwards;
- `ai_ctl_slide` with +0x7B8 off the slice's lanes (beyond lanes ×
  width either side), the heading error over 0.04 (`0x5363ac`) × the
  surface's grip (`0x5523d8`, 1 on dry road), under 0.5 m/s
  (`0x5363b4`), or over 2.5 m/s (`0x5363bc`) with the side slip over half
  the speed (`0x5363c4`);
- `ai_ctl_slow` under 13.41 m/s (`0x5363cc`); else `ai_ctl_rails`.

`ai_ctl_rails` pushes the car across **in the road's frame**: the side
speed wanted is −5 × (+0x7B8 − aim) (`0x56ebd4`), in ±32 (`0x56ebd8`) and
± half the ground speed (`0x53634c`), and under 22.35 m/s (`0x56ebf4`)
± 0.129 × the speed (`0x56ebf8`); `0x406a80` gives the car's velocity
along the right of the road two slices on (`slice_ahead(car, 2)`), and
the force along the road polygon's right (+0x124) is (wanted − that) × 4
(`0x56ebe0`), at most 38 (`0x56ebdc`), both × (1 + 0.0015 × speed)
(`0x536354`). Its yaw only turns the car: the yaw rate wanted −2.5 × the
error (`0x56ebe4`) in ±0.8 (`0x56ebe8`; under 22.35 m/s ± 0.012 × the
speed, `0x56ebfc`), the yaw force 5 × the difference (`0x56ebf0`) in ±0.8
(`0x56ebec`). `ai_ctl_slow` and `ai_ctl_slide` work in the car's frame: a
yaw rate wanted from the error (−2.5, at most 1.6 and 0.02 × the ground
speed; −3, at most 3 and 0.04 × it), its force × 10 (× 10 × 0.5 for the
slide) in ±0.8 (±1), and the car's own side speed (+0x4EC) pulled
towards sin(error) × speed at most 16 and 0.01 × the ground speed (× 3,
in ±38), or towards the error × −30 at most 32, the ground speed and 0.2
× it (× 6, in ±40): the car goes where it points. `ai_integrate` adds the
forces × 1/32 s to the world velocity and the yaw rate. The yaw rate is
in turns a second: in radians the road's bends (Hometown's slices
10-20 turn 2-5° a slice) put the heading error past 0.04 at once and the
car slides off; in turns `d7-watch2` uses the three controllers 1,109,
59 and 140 times in 1,308 calls, as `gh1-ai` counted 1,166, 63 and 65.

The page kept this in each slice's frame until `e11`; it now keeps the
world velocity and the body's yaw as the game does (below, **The world
velocity**): the car runs straight on in the world, so it is carried
wide of a bend, which the rails' velocity two slices on pushes back
against, and it stays within the slice's walls (the car against the
track, `0x4304b0`: a stopped or blocked car no longer slides past
them).

**Checked against the game** (`d7-watch2`, November 2026: Hometown by
day as `t6-watch`, `-watchmem` of car 1's +0x1C-+0x7CC, its slice and
+0x7B0-+0x7CC kept; 1,308 samples, one each 32 Hz call, slices 4-250,
41 s; `d7-runs/sim/replay`). Driving `aiSteerStep` with the game's aim
and speed, slice by slice: 0.62 m rms from the game's +0x7B8, against
0.67 m for the spring (100, 14, fitted on `t6-watch`) and 5.14 m for the
spring with the bend's cut. In a lane change from 4 to 12 m (samples
104-150, about 1.5 s) the model stays within 0.4 m of the game's car,
behind it by a sample or two. **Against `t6-log`** (`aisim` from
the 50 s dump, `weave.py`): +0x638 changes in 16-26 of the 56 samples
per car, 140 in all (the game's 17-23, 143; with the cut 13-18, 110), and
the aim is past 8 m in 4-20 samples per car, 66 (the game's 10-41, 162;
with the cut 122): the lanes are changed as often, but the cars sit in
lane 8 less. Mean speeds 36.3-37.8 m/s (the game's 34.8-38.3), braking
in one sample for four cars, flashing 1-2 times for three cars (none in
the game). **With the race's start** (above; `d8-runs/sim/aisim`
giving each car the dump's +8, +0x1FC and the eight racers, its +8
counting the slices it passes): +0x638 changes in 10-21 samples per
car, 120 in all (cars 1-7: 14, 20, 10, 18, 18, 21, 19; the game's 17,
23, 23, 19, 21, 21, 19), the aim past 8 m in 8-47, 189 (47, 31, 14, 8,
31, 13, 45; the game's 30, 10, 31, 25, 15, 10, 41), mean speeds 35.9-37.8
m/s, braking in one sample for four cars, no flashing, as the game's.
Without the best line's +10 and the empty lanes' +5 a car keeps right
(row 0) until the edge's −30 + 22 holds it in lane 8, as the game's.
Over the 1,568 decisions of the window the votes' rows now agree with
the game's 1,570 (`d8-runs/sim/cmpvotes.py`): row 3 in 81 % of them (the
game's 77 %), row 7 in 48 % (59 %), row 8 in 17 % (27 %), row 1 in 26 %
(14 %); the pick left of the car's lane 747 times (707), its own 688
(716), right 133 (147); the lanes 5-10 at the decisions 43, 126, 268,
769, 345, 17 (the game's 45, 141, 320, 813, 251, 0). The rest is how
the cars meet: the game's car 3 overtakes on the left lanes at slices
94-107 (+0x7B8 to −11.7), the page's stays right, and the lane changes
counted are mostly +0x638 flipping at a lane's middle (4 and 12 m are
lane boundaries), which the page's cars sit on a little less often. The
replay of `d7-watch2` is unchanged (0.62 m rms; the controllers rails
1,109, slow 59, slide 140), and the roadside cop (`park`, 120 stops)
still never parks past a wall.

**Why car 3 didn't overtake** (November 2026). In the game (`d8-votes`,
`0x40daf0` watched) car 3 comes up behind car 4 at slices 91-94 at
64.0-64.5 s, the two a slice apart: car 4's speed wanted falls 42.3 →
31.4 (row 8 and the keeping behind), car 3 brakes from 42.4 to 31.4 and
its votes (row 7 for car 4 in its lane, row 3) pick lane 7, then 6, and
it swings out to −11.7 m before coming back at 107. On the page the two
never met: its cars had run faster from the start, car 4 three slices
ahead of the game's by 61.5 s. Two causes, both from the code:
- **The speed wanted** (`0x409330`) is the table's byte held under the
  top speed cap × the track's factor first (`0x4093ab`-`0x4093ca`, into
  +0x7B0), and only then × the scale (`0x409464`). The page took the
  table × the scale and held that under the cap, so a racer on a stretch
  where the table is over the cap (88 at Hometown's start, 69 further
  on; the cap 78.23 × 0.82 = 64.15 for the Corvette) wanted 63.0 instead
  of 45.9. With the order fixed (`aiStep`, and the cars the page places)
  the page's values are the game's +0x7B0 to the 0.1: per car 44.0, 44.0,
  45.9, 44.5 and 46.8, 46.5 and 48.9, 49.5, 46.5 and 48.9 (the f355 and
  Countach, serials 2 and 15, have the cap 81.81: 67.08 × the scale).
  `aisim` now takes each car's own `carp.txt` by the set-up's key, as
  the page does; it had given every car the Corvette's.
- **The acceleration** (`ai_accel` `0x4064f0`) is × 0.9 only with the
  AI level Normal (`0x6fd4f0` 0) and the race distance (+8) over 100
  (`0x406591`); the page took it always. Through the window the dumps'
  +8 run 1-205, so the cars accelerated 10 % too little at the start;
  now the speeds from the 50 s dump follow the game's within 0.8 m/s for
  the first 4 s (car 3: 8.0, 11.3, 14.7, 18.1 against 8.8, 12.1, 15.2,
  18.5 at 0.5 s steps; the dump's first speed is 0.4 low). Past that the
  game's cars accelerate by their force model on the hills (car 4: 3.5
  to 6.9 m/s² against the table's 4.5-6.9), which the page doesn't have.

Re-measured (`e1-runs/sim/aisim`, the same window): +0x638 changes in
14-24 samples per car, 130 in all (cars 1-7: 15, 21, 14, 24, 22, 16, 18;
the game's 17, 23, 23, 19, 21, 21, 19), the aim past 8 m 193 (43, 29,
24, 23, 19, 11, 44; the game's 162). The speed scales (+0x7BC) agree
with the game's to the end of the window but where the places change
(from 68 s: car 2's 0.656 → 0.738 and car 4's × 0.889, the catch-up),
and the cars still don't meet as the game's do: car 3 reaches slice 94
when the game's does (64.5 s), but car 4 is 3 slices ahead of the
game's (its hills), so car 3 isn't held up and doesn't overtake. The
replay of `d7-watch2` stays at 0.62 m rms (rails 1,109, slow 59, slide
140) and `park` at 120 stops, none past a wall.

**The force model** (November 2026, `aiMove`). `0x4066c0` (towards the
speed wanted, not past it) is only `0x4068a0`'s, for a car with +0x88,
set at `0x46681e` only; +0x88 was 0 for all eight cars in every
`t6-log` dump. The racers are pushed instead (read from the code,
`asm`, constants from the exe):
- `ai_ctl_rails` (`0x4076b0`): the error is the speed wanted × the way
  less the way × +0x7C0 (`0x407992`-`0x4079ae`); over 0 the
  acceleration (`0x4064f0`), else minus the braking (`0x4063e0`,
  `fchs` at `0x4079cf`), with no dead band and nothing that stops the
  speed at the speed wanted: it goes past by the acceleration × 1/32
  and is braked back, so it chatters just under it (the `gh1-ai`
  43.6-43.7 for 44.0). Accelerating, with +0x4F4 over |+0x4EC| and over
  44.703 m/s (`0x53635c`), the drive loses 3.45 (`0x536364`) / P's
  lateral grip multiplier (+0x248) for a racer (+0x200 bit 1), else
  |the side force| / |+0x4F4 × 0.5| (`0x407a28`-`0x407b2a`); for the
  Corvette (3.3) 1.05 m/s². The sign is turned for +0x4F4 under 0, and
  the force goes along the road polygon's forward (`0x56ec74`).
- `ai_ctl_slow` (`0x407c30`) and `ai_ctl_slide` (`0x407fa0`): |the speed
  wanted|, at most 16.4109 (`0x53637c`) for the slow one, less +0x4F4;
  over 1 the acceleration, else minus the braking (`0x407ef3`-`0x407f1b`);
  reversing (+0x7D4) the slow one accelerates backwards while +0x4F4 is
  over minus that speed (`0x407f22`-`0x407f49`). Both along the car's
  forward (+0x4E8).
- `ai_integrate` (`0x4071a0`) adds the force × 1/32 s to the velocity;
  `car_dynamics` (`0x4a2830`, `0x4a2a0c`-`0x4a2b19`) adds gravity:
  −0.3065625 (`0x53c8dc`) × the road's forward's rise (+0x1AC) along
  that forward, while the car is upright.

The page now does this (the speed along the way, + the force × 1/32,
less 0.3065625 × the slice's forward's rise the way it runs). Measured
with `e5-runs/sim/aisim` (`t6-log`'s 50 s dump; each car's own
`carp.txt`; November 2026 the sim also seeds each car's speed wanted
(+0x7B0) and its place along its slice from the dump, where it had
started every car at its slice's point with nothing wanted until its
driver's first call, so a car braked on the first tick and was up to a
slice behind; with that alone the old model gives 146 lane changes, 179
samples past 8 m): from 50 to 64 s, before the game's cars meet, the
speeds are within 0.86 m/s rms of the dumps' (1.50 before; the mean
excess +0.32, was +1.07), car 4 at the end of it 2 slices from the
game's (4 before) and car 3 1 behind; over the whole window 3.08 m/s
rms (3.22). +0x638 changes in 149 samples (cars 1-7: 21, 24, 26, 16,
21, 20, 21; the game's 143) and the aim is past 8 m in 167 (11, 32, 28,
25, 16, 11, 44; the game's 162). Car 4 now holds 44.1-44.5 at
Hometown's slices 69-83 as the game's does. Car 3 still doesn't go
past car 4 on the left: in the game the two meet at slices 90-91 at
64 s side by side (+0x7B8 13.65 and 12.19) and both lose about 10 m/s
in half a second (42.4 → 31.4, 41.3 → 33.2), a hit, after which car 4
aims at 4 m and car 3 swings out to −11.7. On the page car 3 reaches
them as the game's does (slice 90 at 64 s) but in lane 7 (+0x7B8 3.8):
from 61.5 s it aimed at 4 m where the game's aimed past the edge (17.18,
then 12), so the two pass side by side; and the page's cars don't hit
each other. The replay of
`d7-watch2` is unchanged (0.62 m rms; rails 1,109, slow 59, slide 140)
and `park` stops 120 times, none past a wall.

**Car 3's lane at 61.5 s, the votes** (November 2026, `e6-runs`:
`d8-votes`'s decisions at `0x40daf0` against the page's, printed from a
scratch copy of `frd.cpp`). At 61.42 s the game's car 3 is at slice 72,
+0x7B8 12.06, so lane 9 (`0x40e2b0`: 12 / 8 = 1.5 rounds to 2), with car
4 beside it (0 slices ahead) in lane 8. Its rows: 0 (1, 1, 1), 1 (−4, 0,
0: a car beside it ahead, `0x40d9f0`), 3 (0, −8, −978: lane 9 is the one
past the road's lanes, lane 10 none), 7 (−6, 0, 0: car 4 slower ahead in
the lane to its left, `0x40d800`); the sums −9, −7, −977 keep lane 9, so
the aim is a half width past the edge (17.18). The page's car 3 at
61.44 s was at slice 70, +0x7B8 11.92, so lane 8, and its car 4 already
in lane 8, 3 slices ahead: rows 0 (1, 1, 1), 3 (0, 0, −8), 7 (0, −6, 0)
pick lane 7, 4 m. So rows 1, 3 and 7 differ, and each from the state,
not a rule: car 3 sits on the boundary 12 m that lane 8's aim (`0x40daf0`,
its middle 12) and the lane test (`0x40e2b0`, 12 rounds to 9) share, so
0.14 m decides its lane, and the page's car 4 had moved across a second
early. Traced back with each car's first decision whose lane agrees but
whose pick doesn't (`firstdiff.py`), the cars part at 51.7-53.4 s, each
time on row 7's halving for a car not centred in the lane
(`0x40d17d`: the word flags at `0x56ef68`, ahead, and `0x56ef6e`,
behind; read with `capstone`), that is on where another car sits across
the road at 4-17 m/s. The page's rule is the game's. Three things in
the model were not, from the code:
- **The point steered at** (`0x406d40`): the vector is from the car's
  own place (+0x98) to the road point moved by the aim along +0x124; the
  page had put the car at its measured offset plus the line's move across
  the slice (`along` × the chord), twice the latter. And on Hometown
  (`0x6fd4ac` 0) with the steered slice in 0x205-0x20F and the aim under
  0 the aim is 10 more to the left (`0x406d5c`-`0x406d85`).
- **The slice steered at** (`0x406980`): the look-ahead count is
  rounded to the nearest (`fistp` at `0x40e1d1` in round-to-nearest:
  `-watch 0x40e1d4` with the speed and the factor read, 239 counts
  agreeing with nearest, none with truncation, `e6-runs/round`), held to
  4-20, behind the car while it reverses; the walk from 4 slices on
  stops at the first slice whose x-z forward is within |0.7| of the
  car's slice's (`0x40ef70`) and returns that slice, not the one before;
  on Hometown slices 0x225-0x235 it is the look-ahead slice as it is.
- **The slow and sliding controllers** (`0x407c30`, `0x407fa0`) compare
  the speed wanted with +0x4F4, the speed along the car's own forward,
  and push along +0x4E8; the page had used and pushed the speed along
  the road. A car turned off its way (after a hit, sliding) so runs
  slower along the road.
Re-measured (`e6-runs`, `aisim` as `e5-runs/sim`'s, also seeding each
car's aim +0x7C8, target +0x7B4, lanes +0x638-+0x640, heading, side
speed and yaw from the 50 s dump, where it had taken the aim and target
as +0x7B8; with the car-to-car hits, `aiCollide`): +0x638 changes in 121
samples (the game's 143; 139 with `e5`'s seeding), the aim past 8 m in 155
(162; 149), speeds 3.13 m/s rms (3.08 before); the page's car 3 still
doesn't go past car 4 on the left (its +0x7B8 at least 2.67 from 64 to
70.6 s; the game's −11.7). The replay of `d7-watch2` is 0.66 m rms (was
0.62; rails 1,113, slow 59, slide 136): the steered slice from the code
moves the error on slices 129-151 from 0.64 to 1.12 m; with the old slice
it is 0.61. `park`: 120 stops, none past a wall.

**Where the cars part: the slice, the side offset and the lanes'
length** (November 2026, `e7-runs`). The parting at 51.7-53.4 s (row
7's halving, `0x40d17d`) is on cars that sit within centimetres of a
lane's edge: the cars leave the grid aiming at 4 m, which `0x40e2b0`
puts in lane 7 at 4.0 and in lane 8 just past it, so whether a car
ahead counts as centred in a lane turns on its +0x7B8 to the millimetre
(car 7 at 50 s: 4.0024; car 5 4.0076). To see where the page's places
part from the game's, `t6-log`'s run was made again with the car
records dumped every 16 ms from 50 to 53.2 s (`e7-runs/early.sh`; the
50 s dump is byte for byte `t6-log`'s; the set-up pokes were left out,
since they write the 0s already there and an `at=` poke makes `nfs-host`
test every instruction against every poke). Three differences in how
the page placed and measured its cars, all from the code:
- **The slice is the track position's.** +0x1C is the slice whose run
  of road polygons the car is in (the track position's search,
  `0x41e990`, from `0x49fe70` each tick), not the road point behind it:
  the point lies inside the run, so a car is often already on the next
  slice up to 2.5 m before its point (`t6-log`: the place less the
  point, along the forward, −2.5 to +3.2 m). The page had the point
  behind; in the 16 ms dumps its slices agreed with the game's in 684 of
  1,400 samples. Its cars now keep that slice (`AiCar::roadSlice`, from
  `aiRoadFrame`'s search, read through `aiSlice`) for everything the
  driving reads by slice: the lanes and the lane test, the cars ahead
  and behind (`0x40cfc0`), the speed table, the look-ahead, the slice
  steered at, the road 2 slices on and the side offset's point; the
  road points before and after the car still place it. Now 1,312 of
  1,400 agree.
- **The side offset is measured along the road polygon's right.**
  +0x7B8 is written by the race task `0x42ce70` (each tick, for each car
  in play, before the lane `0x40e2b0`, +0x7C0 and the wrong-way test) as
  `0x4a2bc0`: the car's place (+0x98) less its slice's road point, along
  +0x124. +0x124 is `car_road_frame`'s (`0x49ffa0` → `trackpos_road_frame`
  `0x420230`): inside a road polygon of the slice's run (+0x14 set,
  +0x6C 0) up and forward are that polygon's virtual road vector and
  right their cross product; only off the run's side the road point's
  own bytes. The page measured along the slice's right. On Hometown's
  first bends the two differ by up to 2.7° and the offsets by up to
  0.9 m. Against `t6-log`'s dumps (each car's place taken a tick of 32
  back, when the side was measured; `e7-runs/rchk`), the polygon's right
  gives +0x7B8 to 1 cm in 336 of 427 samples, the slice's in 58 (the
  rest are dumps taken at another point of the tick). The page now finds
  each car's road polygon each step (`aiRoadFrame`: `roadSearch` from
  the slice it was on, `polygonRoadFrame`) and measures along its right
  (`aiMeasuredSide`, `aiLateralFor`).
- **A lane's length between slices.** The page moves its cars along the
  slices by their speed, a slice's line at a time, and keeps a car's
  `lateral` from one slice to the next (its place the slice's point plus
  that much along the slice's right), so between two slices a car runs
  from one such place to the other. It had taken the centre line's
  length for that whatever the lane, so on a bend a car on the outside
  covered its lane in the time the centre takes and ran ahead of the
  game's (whose cars move in the world), and one on the inside fell
  behind. Now the length is |next point − point + lateral × (next right
  − right)| (`aiSegmentLength`), also in the rate the line moves across
  (`aiSteerStep`).
- Two smaller ones: `ai_ctl_slow`'s side speed wanted is sin(2π e) ×
  +0x4F4 for a heading error e of 0 or more and −sin((1 + e) 2π) × it
  under 0 (`0x407d09`-`0x407d35`, 2π a double at `0x536374`), the same
  size and sign, so it never asks for the left; and the controllers'
  clamps by the ground speed take +0xB8, max(|vx|, |vz|) + min / 4 of the
  world velocity, where the page had the speed.

Against the 16 ms dumps (`aisim` seeded from the 50 s dump, each car
placed from its dumped position): over 50-51.7 s, before any decision
differs, +0x7B8 is within 0.068 m rms of the game's (0.161 with
`e6-runs`' model); over the whole 3.2 s 0.82 (0.68), as two cars then
pick other lanes. Car 4's move to lane 7 and back at 51-53 s now
follows the game's within 0.2-0.5 m (it had lagged by a slice and 0.6
m). The first decisions that differ (`firstdiff.py`) are still at
51.2-52.7 s, row 7's centring of a car near 4.0. Over `t6-log`'s window
(`aisim` as `e6-runs`'s, with `tr00.frd`): +0x638 changes in 92 samples
(cars 1-7: 14, 17, 15, 15, 11, 11, 9; the game's 143; 121 before; the
page's cars on the lanes' edges at 4 and 12 m sit on one side of them
where the game's flip across), the aim past 8 m in 189 (the game's
162; 155 before), speeds 3.18 m/s rms (3.13) and 0.91 from 50 to 64 s
(0.85). Cars 3 and 4 come to Hometown's slice 76 at 62 s within a slice
of the game's, both on the right, but in one lane (+0x7B8 11.8 and
12.3) where the game's run side by side (14.7 and 7.3) to slice 91 at
64 s: the page's car 3 is already 1-3 slices ahead of car 4 there (slices
76-92), and from 64.5 s it moves left to 2-5 m and stays ahead (3-4
slices at slices 96-120). The game's car 3, turned by the hits, swings
out to −11.7 and is ahead of car 4 from slice 97. So car 3 now passes
car 4 at slices 90-107 on the page, but not as the game's does. The
replay of `d7-watch2` is 0.60 m rms (0.66; rails 1,117, slow 59, slide
132); `park` stops 120 times, none past a wall.

**The hits between cars** (November 2026; physics.md, The hit's
arithmetic). The page's computer cars now run `car_collisions_task`'s
box test and impulse each even step (`aiCollide`). With an optional `hits`
list, `aiCollide` reports each car a hit gave a strength, with the
strength and the point (`AiHit`; `CarBox::hitPoint` is the point
`0x42f280` keeps at +0x178), for the impacts task's particles and
sound; what the hit does to the cars is unchanged. Checked against the
game: `t6-log`'s run again with `-watch 0x42f280` (A, B, both cars'
velocities and yaw spins, the point) and the car records dumped every 16
ms from 63.5 to 64.7 s (`e6-runs/hit`). In the whole run the only
car-to-car impulses are 9 between cars 4 (A) and 3 (B), 63.58-64.18 s,
strengths about 1.5-5. Fed the two records of the 63.568 s dump (rows,
centres, half sizes, masses 1226.7, velocities, spins), `carBoxCollide`
finds the point (739.97, 25.50, −303.06), where the game's watch has
(740.0, 25.5, −303.1): car 3's left rear bottom corner inside car 4's
box; it gives car 4 (5.46, 3.30, −42.89) and yaw spin −0.038, car 3
(10.56, 3.27, −43.58) and −0.165, strengths 5.21 and 4.70; the 63.600 s
dump has (5.28, 3.41, −42.89), −0.038 and (10.40, 2.91, −43.67), −0.163,
car 4's +0x164 1 and both +0x65C 1487 (the step 1452 + 7 × 5). After
the hits the game's car 3 slides (+0x65C kept ahead of the step), its
yaw −0.16 to −0.26 turn/s turns it off its way, and its speed along the
road falls 42.4 → 31.4 while |v| stays about 42: it heads across
the road to the left (+0x7B8 −0.9 m every 32 ms), the swing out to −11.7. Car 4 slows as
its speed wanted falls (42.3 → 31.4, keeping behind car 3). Started from
the 63.5 s dump, the page's two cars hit at 63.56 s (strengths 4.5 and
4.9) and lose 4-6 m/s along the road in half a second, but the page's
car 3 then keeps behind car 4 (its slice a slice behind, from where
`aisim` put it along its slice on the outer lane of the bend) and moves
out to the right (+0x7B8 to 16.9). Over `t6-log`'s window the page's
cars hit twice, at 70 and 78 s (cars 3 and 4): they don't meet side by
side at 64 s as the game's do (above).

**Reversing after a slow hit, and the roll a hit gives** (November
2026, `gdecomp` and `asm`). `car_impacts_task` (`0x42b900`) calls
`0x406120` for a car whose hit strength (+0x168) isn't 0 and that has
the other car (+0x16C): under 10 m/s (+0x7C4 against `0x5361ec`, a
double) it calls `ai_reverse_set(car, 2, 0x80)` (`0x405fd0`). Mode 2
ends a reversing car's reversing unless it is timed (+0x620 bit 0x40)
and its time (+0x7D0) isn't up; a car not reversing starts, +1 to
+0x658, timed for 128 steps. Mode 0 starts it (timed when the steps
aren't 0), mode 1 ends it. `ai_reverse_end` (`0x4060d0`, each tick of
`car_ai_model`) ends a timed reversing past its time, else puts the aim
(+0x7C8) at 0. `ai_target_point` (`0x406e50`, the point steered at, with
no target and on the road's lanes): over 10 m/s (`0x5362cc`) +0x658 is
cleared; at most that, more than 8 starts put the car back on the road
(`0x429100`: a slice on by `0x5efdcc`, velocity 0, raised, ghost 64) and
more than 4, in a lane off the road's (+0x638 under 6 − left lanes or
over right lanes + 7), it steers at its own slice's point
(`ai_point_own_slice` `0x406c00`). The page does all of it, the put
back too since `e9` (below; `aiReverseSet`, `aiCollide`, `aiMove`,
`aiSteerStep`, `aiPutBack`). A hit's spin is
a vector: the page took only its part about the car's up; it now keeps
the parts about its forward and right as roll and pitch rates (turns a
second), turns the body by them, and brings them back as the dynamics
does (`aiTilt`): while a hard hit has it in the air (+0x164) every spin
× 0.6 a tick (the landing `0x4a1ce0`, e 0.1) until all three are under
0.6; on the road each rate is set (`0x4a1b18`-`0x4a1cc6`) to the
body's up less the road's up along that axis, held to ±0.5 turn/s
(`0x53c84c`, `0x53c854`), and to 0 once both are within 0.02
(`0x53c844`); the computer's model (`car_ai_model`) doesn't touch them.
The page draws its cars with the roll and pitch. In `t6-log`'s window
no hit comes under 10 m/s, so the sims are unchanged by this.

**The tick's order and the lanes a car spans** (November 2026,
`e9-runs`). Where the cars part at 51-53 s (row 7's halving of a car
not centred in a lane, `0x40d17d`) the decision turns on another car's
+0x638, +0x63C and +0x640 and on when each was written. Read from the
code (`capstone` on the exe) and one run:
- **+0x21C is not the half width.** `car_lanes_update` (`0x40e350`)
  takes the car's left and right lanes at +0x7B8 − +0x21C + 0.5 and
  +0x7B8 + +0x21C − 0.5 (the doubles `0x5367cc`, `0x5367d4`), and +0x21C
  is written only by the race task `0x42ce70` from `0x42cd00`: the half
  width, height and length (+0x114, +0x118, +0x11C) each × |the road's
  right (+0x124) · the body's right, up, forward (+0xC0, +0xCC, +0xD8)|
  (`0x4e01f0` a dot product). So a car turned off its way spans more of
  the road: in the 50 s dump the eight cars' +0x21C are 1.18-1.44 (the
  formula on the same dump gives each to 0.02; the dump's +0x21C is the
  tick before), where the page had taken the half width 1.14-1.18. The
  lane votes (`0x40daf0`, `0x40d3d0`) do use +0x114. The page now keeps
  it (`AiCar::extent`, `aiCarExtent`: the body's axes as the hits have
  them, turned by the roll and pitch); against the 16 ms dumps it
  follows the game's within about 0.05 (car 7 at 51.0-51.4 s 1.56-1.70
  against 1.58-1.73).
- **+0x638 is fresh, +0x63C/+0x640 are the car's own last decision.**
  `0x42ce70` runs for every car in play each tick and writes +0x21C,
  +0x7B8 (`0x4a2bc0`) and +0x638 (`0x40e2b0` at the new +0x7B8,
  `0x42ced1`); +0x63C and +0x640 are written only when the car's own
  driver calls `0x40e350`. The cars ahead and behind (`0x40cfc0`) read
  the other car's three as they stand, so its centre lane is this
  tick's and its edges are from its last decision (up to 8 steps old).
- **The order of a tick.** `order` (`t6-log`'s keys, `-watch` on
  `0x42ce70`, `0x40e350`, `0x40dd60`, `0x409c00`, `0x4a2830`,
  `0x4088f0`, `0x4311d0` and `0x4a2c60`, with `g_step` `0x7d3684`
  read at each): on each even step the race task first (once, all
  cars), then every car's driver in the list's order (those the gate
  lets act vote and update their lanes), then each car's dynamics and
  model (`0x4a2830` then `0x4088f0`, car by car), then the hits
  (`0x4311d0`, every car), and on every step the positions (`0x4a2c60`,
  Sc64) last. So a driver reads every other car's +0x7B8, +0x638 and
  +0x7C0 as they were at the tick's start, before any model of that
  tick; and the model's controllers read +0x7B8 as the race task
  measured it, not the car's place after it moved. The page had run
  each car's driver and then its model before the next car's driver,
  and measured +0x7B8 in the driver. Now the page and the sims run
  `aiRaceTick` (the race task's part), then every car's `aiStep` /
  `aiCopStep` with `AiPhase::Drive`, then each with `AiPhase::Move`
  and its motion, then `aiCollide`.
- The reach of the cars ahead and behind (`0x40cfd0`) is +0x7C4 × 2 ×
  0.1667 rounded towards 0 (`0x4dfd56` sets the chop mode for its
  `frndint`), at least 10, as the page had it; `0x40e2b0` rounds to the
  nearest, even on a tie (`fistp`), so a car at exactly 4.000 with lanes
  8 wide is in lane 7 (car 1 at 51.3 s: 4.000 and +0x638 7 in the dump).

A car aimed at a lane's middle on 8 m lanes sits on `0x40e2b0`'s
boundary either side; over `t6-log`'s window the samples whose aim is on
4 or 12 m have +0x7B8 − aim of the same spread on the page as in the
game (163 samples, mean −0.008, sd 0.181, 81 out and 82 in; the page's
164, −0.011, 0.195, 73 and 87), so no rule tips them one way. But the
game's cars cross the edge between half-second samples more often: of
its 143 lane changes 47 are between two samples within 0.3 m of an edge
(99 samples within 0.1 m), the page's 21 of 86 (84 samples).

Measured (`e9-runs`, `aisim` and `aisim_fine` as `e7-runs`', with the
order above): over the decisions to 56 s (`agree.py` against
`d8-votes`) the lane and the pick agree with the game's in 281 of 338
(242 before), and the first differing pick per car moves from 52.37,
51.60, 52.07, 51.77, 52.73, 51.21 s to 52.37, 51.73, 52.42, 51.90,
52.73, 51.34 (cars 1-6; car 7 at 67.8, was 67.3). Those that still
differ are the model's, not a rule's: car 6 at 51.34 s doesn't see car 7
in the lane to its right because the game's car 7 voted at 51.31 s with
+0x7B8 5.24 and +0x21C 1.69, so its left edge in lane 8 (4.05), where
the page's car 7 is at 5.10 (3.95, lane 7); car 2 at 51.73 and car 4 at
51.90 s turn on car 4's left edge at −4.0 (game −2.75 − 1.65 + 0.5 =
−3.90, lane 6; page −3.01 − 1.59 + 0.5 = −4.10, lane 5). Both lags come
from the page's model across the road where a car crosses a slice on a
bend: the page keeps its velocity in the slice's frame (the road point's
forward and right bytes), and on Hometown's slices 14-16 the right is
up to 2° off square to the forward (forward · right 0.028-0.042), so the
velocity along the car's own right (+0x4EC) jumps at the slice change
(car 4 at 51.04 s: −0.68 to −0.04 m/s; the game's −0.75, −0.78). Turning
the velocity as a true rotation there instead makes +0x7B8 worse
(0.144 m rms over 50-51.7 s against 0.064), so it was left. Over the
first 3.2 s +0x7B8 is now within 0.35 m rms of the game's (0.82 before;
0.064 over the first 1.7 s, 0.068 before).

**The rails controller's force** (`ai_integrate` `0x4071a0`): the
controller's force 0x56ec6c-0x56ec74 goes through the transposed rows
at +0x124, so its side part is along the road polygon's right and its
forward part along the polygon's forward (+0x13C); the car-frame part
+0x4E0 through the body's rows. The page now pushes along
`AiCar::roadRight` and `roadForward` (`aiRoadFrame`), into the world
velocity since `e11` (**The world velocity**, below). The `d7-watch2` replay doesn't
move (0.60 m rms with the slices' frames; given the polygons' frames
as well, 0.72 m both before and after: the replay's places are rough).

**The put back** (`0x429100`, read with `capstone`). `ai_target_point`
(`0x406e50`), for a car with no target (+0x624 0) and +0x621 bit 8
clear (set, it takes `0x406c90`'s point instead; the page has no such
state): when `0x40f110` says it is off the road's
lanes (+0x638 outside 0-13, or neither it nor the lane either side of
it within 6 − left lanes to right lanes + 7), +0x7D8 counts the ticks
its +0x4F4 is under 3 m/s (`0x5362c4`); while +0x7D8's bit 0x40 is set
it steers at its own slice's point (`0x406c00`), and over 1000
(`0x406fc1`) it is put back (`0x406fd3`) and +0x7D8 cleared. On the
lanes +0x7D8 is cleared, and over 10 m/s +0x658; else more than 8
reversings put it back (`0x407023`, +0x658 cleared), more than 4 in the
lane past the road's lanes steer it at its own slice. `0x429100` (not
while a reset is under way, +0x524; not for a racer out of the race; in
a pursuit not while it or its target is pulled over, `0x401500`) takes
the slice +0x1C + 5 (`0x5efdcc`) the race's way and `0x428d60`: the
slice's point if free, else for i = 0 to the larger lane count − 1 the
right lane middle (i + ½) × the right width (if i ≤ right lanes), then
the left one, then the next slice, up to 10; none free, the first
slice's point. A spot is free (`0x428ad0`) when its lane (`0x40e2b0`) is
open (`0x40e220`), no other car in play is within a lane (+0x638) and
a slice (+0x1C) of it, and a road polygon under it has a drivable
surface (`0x4205a0`, `0x4207e0`; not 0x40, 0x80, 0 or 0xE) and an up
over 0.8 (`0x5371b0`). There `0x4a0aa0` lines the car up with the road;
`0x429100` zeroes the velocity and lifts it 1.5 m (`0x5efdd0`, ×
`0x5371d4` 0.5 under a roof), `0x428970` zeroes the spins and forces,
sets +0x164 1 and clears the strip's catch (+0x621 bit 0x40); then
+0x174 = 64 (`car_collisions_task` `0x4311d0` takes it down by one a
tick and enters no car with it set) and +0x524 = 0x20. The page does
this (`aiPutBack`, `aiSpotFree`, `AiCar::offRoadTicks`, `ghostTicks`):
the surface test is left to the lanes' strips, and the way is the car's
(a racer's is the race's). In `t6-log`'s window no car is put back.

Re-measured with all of the above (`e9-runs/measure.sh m4`): +0x638
changes in 86 samples (cars 1-7: 10, 13, 14, 13, 11, 15, 10; the
game's 143; 92 before), the aim past 8 m in 168 (the game's 162; 189
before), speeds 3.92 m/s rms over the window (3.18) but 0.75 from 50 to
64 s (0.91): past 65 s the page's cars 6 and 7 are held up behind the
others where the game's aren't (13 and 16 slices behind at the end).
Cars 3 and 4 part from the game's earlier than before: from 60 s the
page's car 3 runs at 4 m and car 4 at −4 m to slice 90, where the
game's run at 12 m side by side, so the hit at 64 s and car 3's swing
to −11.7 don't come. The `d7-watch2` replay is 0.60 m rms (rails 1,117,
slow 59, slide 132) and the roadside cop (`park`, now in the tick's
order) stops 120 times, none past a wall.

**The world velocity** (November 2026, `e11-runs`, read with
`gdecomp` and `capstone`). The page had kept a racer's velocity as a
speed along the road and a velocity along the slice's right, carried
from slice to slice by rebuilding the vector in one slice's frame and
measuring it in the next. On Hometown's slices 14-16 a slice's right is
up to 2° off square to its forward (forward · right 0.028-0.042), so
that rebuild was not the vector the projections came from, and the
velocity along the car's right jumped at a slice change (car 4 at
51.04 s: −0.68 to −0.04 m/s where the game's runs −0.75, −0.78). The
game keeps no such frame:
- **The velocity is the world's** (+0xA4). `ai_integrate` (`0x4071a0`)
  takes the controller's road-frame force (0x56ec6c-0x56ec74) through
  the transposed rows at +0x124 (the road polygon's right, up, forward)
  and the body-frame force (+0x4E0) through the body's rows (+0xC0),
  and adds both × 1/32 s to +0xA4. `car_move` (`0x4a2c60`, the Sc64
  task, every step, last) adds +0xA4 / 64 (`0x53c8ec`) to the place
  +0x98. `car_dynamics` (`0x4a2830`, before the model) takes +0xB8 from
  +0xA4's x and z and adds gravity (`0x53c8dc`, −0.30656 m/s a tick)
  through the same road rows, each × its own y. `car_ai_physics`
  (`0x4088f0`) takes +0x4EC and +0x4F4 as +0xA4 · the body's right and
  forward, and the race task (`0x42ce70` → `0x42cdd0`) +0x7C0 as +0xA4
  · the road polygon's forward (+0x13C), +0x7C4 its size. Nothing turns
  or rebuilds the velocity at a slice's end; the rows at +0x124 are
  only ever read as they are.
- **The heading is the world's.** `0x42ce70` stores +0x210 as
  `atan2_turns` (`0x4ea9c0`: `fpatan`, × 1/2π) of +0x1A8 and +0x1B0,
  the forward row of the body frame laid on the road's up
  (`0x4a0f30`), × 1024 (`0x537398`), chopped (`0x4dfd56` sets the
  rounding to chop for the `fistp` at `0x42cee9`). `ai_heading_error`
  (`0x4070c0`) takes it as h × 0.00097656 for h over 0, else
  (h + 512) / 1024 + 0.5 (`0x5362d4`, `0x5362dc`, `0x5362e4`), less
  the direction to the point steered at (`0x406e50`'s vector, its x and
  z through `fpatan` × 1/2π, `0x5362ec`, plus 1 under 0), kept within
  ±0.5. The point (`0x406d40`) is the steered slice's point moved by
  the aim along +0x124, the car's road right, not the slice's.
- **The model's order** (`car_ai_model` `0x408600`): `ai_lane_ramp`
  (the aim 0.2 on), `ai_gear_rpm`, `ai_reverse_end`, then the heading
  error and the controller, so the controller steers at the aim as
  just moved. The controller is the slow one for a car reversing or
  with +0x4F4 under 0, the sliding one when +0xB8 is over 2.5
  (`0x5363bc`) and |+0x4EC| over +0x4F4 × 0.5 (`0x5363c4`).

The page now does the same: `AiCar::vel` is the world velocity and
`AiCar::yaw` the body's heading in the world; `speed` (+0x7C0 along
the way), `sideSpeed` and `heading` are views of them (`aiViews`),
taken again when the race task runs (`aiRaceTick`) and after each
model; writing a view (a stop, a cop turned round, a test's set-up)
sets the world state from it. The controllers add their forces to
`vel` along the road polygon's right and forward or the body's right
and forward; gravity goes along the polygon's forward; the heading
error is taken in the world with the heading chopped to 1/1024 of a
turn; and `aiAdvance` moves the place by `vel` / 64 every step after the
hits (the page and the sims call it last in the tick), finding the
slice, `along` and `lateral` again for the new place (`aiLocate`). A
slice's run is its point + lateral × its right + along × the chord, so
on the outside of a bend there is a gap between two runs (the place is
past the one's end and before the next's start): the place stays on
the first run with `along` a little over 1 until it is in the next. The
page had clamped `along` to 1 there, which held a car still on the
outside of Hometown's first bend until the gap closed (car 4 on slice
13 at 50.5 s, 2-4 m behind its own velocity in half a second).

Measured (`e11-runs`, `aisim` and `aisim_fine` as `e9`'s, each car
seeded with its dumped +0xA4 and body forward, `aiAdvance` after
`aiCollide`): over the 16 ms dumps +0x7B8 is within 0.020 m rms of the
game's over 50-51.7 s (0.064 before) and 0.128 m over 50-53.2 s (0.35),
and the slice +0x1C agrees in 726 of 742 samples to 51.7 s (716) and
1,350 of 1,400 to 53.2 s (1,312). Over `t6-log`'s window +0x638 changes
in 116 samples (cars 1-7: 16, 14, 17, 11, 20, 21, 17; the game's 143;
86 before), 44 of them across a 4 or 12 m edge between samples within
0.3 m of it (the game's 47; 21 before); the aim is past 8 m in 182
samples (162; 168). The decisions to 56 s agree in lane and pick with
`d8-votes` in 295 of 338 (281), and the first differing pick per car is
at 53.25, 53.21, 52.29, 52.90, 53.10, 73.96 and 66.67 s (52.37, 51.73,
52.42, 51.90, 52.73, 51.34, 67.8): each is still row 7's halving for a
car ahead whose +0x7B8 sits within 1-2 cm of 4.0 (car 5 at 52.30 s:
the game's 3.991, lane 7; the page's 4.003, lane 8). Speeds: 1.57 m/s
rms over 50-64 s (0.75) and 4.41 over the window (3.92); the cars now
part at 57.5 s (car 5 moves left where the page's goes right, and car 3
then stays at 4 m where the game's goes out to 11-12), so cars 3 and 4
are not side by side at 64 s and their hits don't come; instead the
page's cars 1 and 4 (65.5-67.6 s) and 6 and 7 (68.0-68.8 s) run within
a lane of each other on the same slice and hit 45 times (the game's
cars hit 9 times in the whole window, all cars 3 and 4 at 63.6-64.2
s); on the same slice each takes the other's speed as its speed wanted
(keeping behind it, `0x4094d0`) while the hits and the sliding they
start slow both, so the two fall to 24-25 m/s; at the end cars 6 and
7 are 5 and 6 slices behind the game's (13 and 16 before), car 2 9.
The `d7-watch2` replay is 0.40 m rms (0.60; rails 1,229, slow 59, slide
20) and `park` stops 120 times, none past a wall.

**Gravity along the body's own forward** (November 2026, `g1-runs`,
read with `asm` and `gdecomp`). From about 51 s the page's cars 5-7 ran
0.2-0.5 m/s faster than the game's. Per 32 Hz call, from `t6-log`'s 16
ms dumps (`resg.py`: the step in +0x7C4 less the acceleration table's
a / 32): car 7 at 7.3 m/s on slice 23 gains 0.2228 m/s a call in the
game, 0.2267 on the page; the difference is gravity's. `car_dynamics`
(`0x4a2a0c`-`0x4a2b19`) does not put gravity through the road polygon's
rows (+0x124) but through the rows at +0x190, each × its own y
(`0x4e0050`). Those rows are written by `0x4a0f30` (from
`car_road_dots` `0x4a2690`): for a car not a human (+0x200 bit 2 clear)
with +0x10C, the body's up · the road's normal, over 0.9
(`0x53c7f4`), they are the body's own rows (+0xC0, +0xCC, +0xD8) with
the road's normal (+0x130) put in as the up. The up part goes into the
road (the contact, `car_on_road` `0x4a1610`, takes it back); the forward
part is added when the normal's y is at least 0.87 (`0x5efdd8`), the
right part as well under that. So a car is pulled by its own forward's
rise. And the body's tilt is the world's: `car_on_road`
(`0x4a1b18`-`0x4a1cc6`) sets the spin about the world's x (+0xE4) to the
normal's z less the body's up's z and about z (+0xEC) to the body's up's
x less the normal's x, each held to ±0.5 turn/s, both 0 once both are
within 0.02 with the body upright; `car_integrate_turn` (`0x4a2e20`)
turns the rows by the spins about the world's axes. Within 0.02 nothing
lays the body on the road: in the dumps car 7's forward's y (+0xDC)
stays 0.0450 from 50.0 to 50.6 s while the road's forward's y goes
0.045 → 0.034 → 0.024. The page had pulled along the road polygon's
forward and kept the roll and pitch against the slice's up. Now
(`aiModelSteer`, `aiTilt` with the track, `aiAdvance`, `aiTiltedAxes`,
`aiSetTilt`) the forward part goes along the body's forward (its yaw
laid on its tilted up), `aiTilt` takes the world spins from the road
polygon's up, and `aiAdvance` keeps the body's up in the world across
slices (turned with the yaw), taking the roll and pitch again against
the new slice. The sims seed the roll and pitch from the dumped +0xCC.
Against the 16 ms dumps the page's forward's y follows the game's within
about 0.005 (car 7 at 52.0 s 0.0001/0.0046, car 4 0.0470/0.0432) and
car 7's step is 0.2248. The hits had put a spin about the right in as a
pitch the other way from `aiTiltedAxes`' (which pitches the up towards
−forward); they now agree.

Measured (`g1-runs/all.sh`, the sims as `e11`'s): the speeds 3.09 m/s
rms over `t6-log`'s window (4.41) and 1.06 from 50 to 64 s (1.57); over
the 16 ms dumps cars 5-7 within 0.06 m/s to 53.2 s (0.2-0.3 before),
cars 1-4 up to 0.22 over (0.1 under before: they had been held back by
the road's forward's rise where their bodies were level); +0x638
changes in 128 samples (the game's 143; 116 before), 56 of them at a 4
or 12 m edge (47; 44), the aim past 8 m in 172 (162; 182); +0x7B8
within 0.017 m rms over 50-51.7 s and 0.105 over 50-53.2 s (0.020,
0.128); the decisions to 56 s as before (295 of 338, the first
differing picks the same), to 78 s 767 of 1,568. The page's cars no
longer run on one slice within a lane of each other: no hits (45 before;
the game's 9, cars 3 and 4 at 63.6-64.2 s). Cars 3 and 4 now come to
slices 86-91 at 63.5-64 s at 11.8-12.2 m with car 4 a slice ahead, where
the game's run side by side (car 3 at 13.7-16.8 m); car 4 moves across
from −4 m about a second after the game's (2.9 m at 62 s, the game's
7.3), and car 5 parts at 56.7 s on the 4 m edge (the game's 3.98, lane
7; the page's 4.00, lane 8). The `d7-watch2` replay (0.40 m rms) and
`park` (120 stops, none past a wall) are unchanged.

**The road frame where the search finds nothing** (`0x41f018`-
`0x41f078`). Unfound, the track position's search walks +0x10 back to
the slice it started from and clears +0x14 and +0x18, so
`trackpos_road_frame` (`0x420230`) takes that slice's road point's own
vectors (its bytes +0x0D-+0x17) and point, whatever +0x6C is; and inside
a polygon it takes the polygon's road vector only with +0x64 2 (set by
the search when it finds one, `0x41eb4b`, `0x41ee00`; 2 in all 427 of
`t6-log`'s samples). The page had kept the last polygon's frame when
the search found nothing on the run, and had taken the frame of the
point behind the car (`slice`) off the run's side; it now takes the
searched slice's (`aiRoadFrame`). In `t6-log`'s window the search always
finds a polygon, so nothing measured changes. The jump in car 1's
+0x7B8 the `e11` sims had at 63.78 s (22.84 → 14.96 m, the car at 22.8
m on Hometown's slice 89-90) is not this: the polygon found there is
slice 90's run's polygon 11, the bank past the road, whose road vector's
right is (−0.600, 0.794, 0.096), and +0x7B8 is measured along it as the
code does. The car was there because its lane change to the edge's aim
(17.14) crossed 16 m at 6-7 m/s, the sliding controller took it
(`ai_ctl_slide`, off the lanes) and on the bend it ran on outside the
point it steered at; with gravity as above car 1 runs there at 55-59 s
instead (to 21.6 m) where the game's car 1 is at 4-8 m: its pick
already differs at 53.25 s.

**The put back's road test and way** (`0x428ad0`, `0x429100`,
`0x428d60`). After the lane and the cars `0x428ad0` builds a track
position at the slice's point + the offset along its right
(`trackpos_at_point` `0x4205a0` from that slice); it must be in a
polygon (+0x14), whose record's byte 1 (`trackpos_surface` `0x4207e0`)
has neither 0x40 nor 0x80 and a surface (& 0x3F) neither 0 nor 14, and
whose road vector's up y, read as an int (s16 × 2), is at least 0.8
(`0x5371b0`, a double): any up y above 0. `0x429100` goes 5 slices
(`0x5efdcc`) on the race's way, +1 or −1 on a backward track
(`g_backward`), and `0x428d60` walks on the same way, for every car,
racer or cop. The page now does both (`aiSpotOnRoad`, applied by
`aiSpotFree` once the car has searched the track; `AiCar::raceBackward`,
false on the page's tracks). In `t6-log`'s window no car is put back.

**The cars ahead in their lanes, checked** (`0x4094d0`). The keeping
behind is the game's: with a car ahead in the car's lane (`0x56ef3c`),
not a police car, its +0x7C0 of the same sign, |+0x7B0| less its +0x7C4
over 0: within 2 slices (`0x56ef48` under 2) +0x7B0 becomes its +0x7C0,
at 2 lowered by half (`0x536424`) the difference.

**Where +0x7B8 parts by a centimetre** (November 2026, `g1-runs`). The
first differing picks (53.25, 53.21, 52.29, 52.90, 53.10 s) are still
cars on the 4 m edge to the centimetre (car 5 at 52.30 s the game's
3.991, the page's 4.003). Over 50-52.3 s, with the aim at 4 m, the page's
+0x7B8 less the game's has means of −0.006 to +0.003 m for cars 1, 3
and 5 (109-131 samples each): no bias, a few millimetres' wander either
way. Tried and left out: measuring from the body's centre kept in the
world (car_on_road pushes +0x98 only along the road's up, so its move
across the road is never taken out, where the page re-places the car on
the slice line each step): 0.0180 m rms to 51.7 s (0.0172), 0.105 to 53.2
s, the decisions 309 of 338.

**The body's centre and the downforce** (November 2026, `h1-runs`,
read with `gdecomp` and `asm`).
- **Where +0x7B8 is measured from.** `0x42ce70` stores +0x7B8 from
  `0x4a2bc0`, which is (+0x98 − the slice +0x1C's point) · the road
  polygon's right (+0x124). +0x98 is the body's centre: `car_clearance`
  (`0x4a0440`, from `car_dynamics` `0x4a288f`) makes +0x15C = (+0x98 −
  the road point +0x148) · the up +0x130 less |up · body right| × +0x114
  + |up · body up| × +0x118 + |up · body forward| × +0x11C, and
  `car_on_road` (`0x4a1a2a` and before) pushes +0x98 out along +0x130 by
  −clearance when it is 0 or less. In the 16 ms dumps +0x15C is 0 (a few
  at 0.002) and the formula on +0x98 gives it to a millimetre: the
  centre sits with the box's lowest corner on the polygon's plane. The
  right (+0x124) is up × forward (`0x420230`, fixed point), so a move
  along the polygon's up measures nothing; a move along the slice's up
  does, by the slice's up · the polygon's right. The page's place lies on
  the slice's plane (its right and the chord to the next point; the sims
  seed it as the dumped +0x98 brought square onto that plane), so the
  centre is above it along that plane's normal by H, with (place + H ×
  normal − +0x148) · +0x130 = the box's reach, and `aiMeasuredSide` now
  adds H × (normal · the polygon's right) (`aiCentreLift`; `aiRoadFrame`
  keeps the polygon's up and point, `AiCar::roadUp`, `roadPoint`).
  Checked (`h1-runs/sidechk`): from each dumped +0x98 (moved back by the
  velocity / 64 for each step the race task's +0x7B8 lags, the step found
  from the game's own formula on the dumped rows), the page's measure is
  within 0.05 mm rms of the dumped +0x7B8 over 1,391 samples (2.7 mm
  without). This is the code's point, not the lift trial's (the body's up
  × the half height), and it doesn't give the trial's 325 of 338.
- **The downforce** (`car_downforce` `0x4a2260`, first in
  `car_dynamics`): upright (+0x10C over 0.75, `0x53c8a4`) and +0xB8 over
  10 m/s, the velocity along the body's up loses P +0x24C (carp field 31)
  × 2 for a car not the player's (`0x53c8ac`) × the velocity along the
  body's forward, when that is over 0. Then `car_on_road` takes the
  velocity's part along the road's up out (when under 0.5,
  `0x4a1aaf`), so what is left is along the road where the body is
  pitched on it: about −0.0009 m/s a call for cars 1 and 2 (bodies
  nose-down 1-2 degrees on the road), +0.0006 for cars 5-7. The page
  now does both (`AiCar::downforce`, `groundSpeed`, `AiCarData::downforce`);
  the removal is along the road polygon's own up where the game's is the
  road frame turned half way (`0x4b7680`) to the frame at the box's
  front-bottom one tick ahead (in the dumps the velocity is left with
  −0.002 to −0.007 m/s along +0x130).
- **What is left of the speed's drift.** Per accelerating rails call
  (the velocity +0xA4 between dumps, along +0x13C, less the table's a /
  32 at round(+0x7C4 − 0.5) with +0x7C4 the dump after the call, which is
  the value the call read; the dump before lags one race task), to 52.65
  s the game's cars 1-7 have −0.0062, −0.0096, −0.0151, −0.0172,
  −0.0203, −0.0156 and +0.0002 m/s; the page's (`frd_pc`) −0.0021,
  −0.0065, −0.0121, −0.0139, −0.0198, −0.0137 and −0.0015. So cars 1-4
  still gain about 0.003 a call, about 0.2 m/s by 53 s. Neither gravity
  along the road's forward nor along the body's own explains all seven
  (the residual less body gravity and downforce is −0.0010 to −0.0031 a
  call for every car); the table's lookup, its rounding (`fistp` in the
  default mode; `0x4dfd56` restores the control word) and the 0.9 are
  as the page has them, and `ai_integrate`'s damping loop does nothing
  for the rails. An `nfs-host -count` of `0x4a2871` with `-native` gave
  296 runs to 50 s, so per-instruction dumps at a call's sub-steps need a
  run without `-native`; not done.

Measured (`h1-runs/all.sh f1`): +0x638 changes in 132 samples (the
game's 143; 128 before), 55 at an edge (47; 56), the aim past 8 m in
168 (162; 172); speeds 3.54 m/s rms over the window (3.09) and 1.60 over
50-64 s (1.06): past 56 s the cars run in other places, car 3 at 40.5
m/s and 3-4 slices behind the game's at 61.5-64.5 s (45.2 before);
+0x7B8 within 0.0177 m rms over 50-51.7 s (0.0172) and 0.106 over
50-53.2 s (0.105); the decisions to 56 s 296 of 338 (295), to 78 s 750
of 1,568 (767), the first differing picks 53.25, 53.21, 52.29, 52.90,
53.10, 65.08 and 66.92 s; no hits (the game's 9). Over the 16 ms dumps
cars 1, 2 and 4 are 0.15, 0.11 and 0.10 m/s fast at 52.6 s (0.21, 0.17,
0.16), car 6 0.26 (0.19). The `d7-watch2` replay (0.40 m) and `park`
(120 stops, none past a wall) are unchanged.

**The roadside cop** (`d7-runs/sim/park`,
the police car ordered to lay a strip from every 8th slice of Hometown,
120 stops): with the spring and the cut it parked past a wall 81 times,
by up to 5 m (it was pulled on to lane 5 − L's aim at the wall less its
half width, the cut on top, and kept moving across once stopped); now
never, a median 3.3 m inside the road's edge (at most 4.8 past it, at
least 4.6 from the wall): `car_off_lanes` (`0x402f70`) stops it as soon
as +0x7B8 rounds into the lane past the road's lanes (past the outer
lane's middle), and stopped (`car_stop_hard`) it no longer moves across.
The roadside order itself is the game's: kind 0, `cop_ai_vote_roadside`
(`0x402d90`: +100 for the nearer edge's lane while within lanes 6 − L to
7 + R, its own beyond), its aim clamped to the walls less its half width
(`0x40daf0`).

In a scratch run of 30,000 steps (almost 8 minutes) on each of tracks
0-4 with the page's three cars, they braked into the bends on every
track (41-173 of the 1,875 samples), changed lanes along the best line
and round each other, and flashed their lights on `trk002` and
`trk003` (a car accelerating out of a bend close behind another, or
meeting the police car in its lane), not on the other three.

**The headlights in footage** (November 2026). In the night clips the
pool ahead of the car is a flat band with a fairly sharp far edge on
the road (`Hometown_Night` 0:40, `Summit_Night` 2:17, 2:53), while snow
banks, rock faces and tunnel walls beside it only brighten broadly and
softly, with no such edge (`Summit_Night` 2:53, 7:20; `Hometown_Night`
1:02). That fits the code: roadside polygons all have `0x04` and only
get the light in their vertex colours, while road polygons get the
projected picture. At the clips' size it can't be told whether the lane
markings inside the pool get it. The cars' tail lights glow large and
red behind each car, and a police car's light blue (`Hometown_Night`
1:02). The track lights glow by day too (a street lamp in
`Summit_Day` 4:05), as the page draws them.

Every polygon's texture is in the table, and every picture they use is
in its sheet. Some entries name pictures past the sheet's end, but no
polygon uses those.

## Which blocks are drawn

From nfs3.exe (November 2026). For each view the game goes through
the neighbour list of the camera's block (`0x41cdc0`) and draws a
listed block if its centre is within the view distance on the ground
(x and z) and passes a test against the view (`0x41bf30`, not
followed). The road detail is the neighbour's second value, or, as
that is always −1, by the same distance (`0x41aa60`): high (list 4, and the lane markings)
within 110, medium (list 2) within 150, low (list 0) beyond. The
roadside list after the road's goes with it.

The distances are set per view (`0x41df10`; for the main view of a
one-player race 500, 110 and 150) and then scaled by `0x41d620` by the
view distance option (`viewlevel`, `0x6fbc28`) and the time of day
(16.16 tables at `0x41a8bc` and `0x41a8b0`):

| View level | By day | By night |
|---|---|---|
| 0, 1 | × 1 (500) | × 0.75 (375) |
| 2 | × 0.55 (275) | × 0.41 (205) |
| 3 | × 0.4 (200) | × 0.3 (150) |

At level 0 every block in reach is at high detail. Split screen
(`0x6fd3b0` = 1) starts from 440, 60 and 92 instead.

Then, for each block drawn: layers 0-2 always, layer 3 only by day
without weather (`0x6fd4c8`, `0x6fd4cc`); the block's four groups of
placed objects (`0x5718d4` + block × `0x5c0` + 8 × group, loaded in
file order by `0x419c20`), the fourth again only by day without
weather. The last group of placed objects, after the blocks', goes to
`0x5dd070` and is drawn last for every view. A polygon whose first
corner (as queued, so vertex 1 of a placed object's) is more than the
view's `+0x30` ahead along the view is dropped (`0x41b3bf`; also in
the placed objects' loop, `0x41b6e2`, and the headlight pass,
`0x41af7a`). The view is a 0x54-byte record at `0x5dd0e0`
(`0x41d310` points `0x5dd830` at it); `+0x30` is the float of its
`+0x24`, which `0x41df10` sets to 500 (400 split screen) and
`0x41d620` leaves alone: only `+0x28`, `+0x38` and `+0x3c` (the
blocks' reach and the detail distances, read as floats at `+0x34`,
`+0x40` and `+0x44`) are scaled by the view level and the night. So
the cut-off is 500 by night too, where the blocks' reach is 375. Hometown's spinner
(at (−563, 2, 442)) stands about 500 ahead of road slices 489-491, but
`nfs-host` frames from there (`t5-hd`, the car poked onto slices 489
and 491) show the town's buildings in front of it, as the page does.

Compared on a moving object (November 2026, `t6-cut`): Hometown's
last group has an object (30 vertices, 12 across) flying a loop over
the start, 141 keys 25 steps apart. With the human's car poked onto
slice 886 (looking south down the road), at steps 700-800 (`0x7d3684`,
dumped with the camera) it passes 440 to 506 ahead, 118 to the right
and 24 up, over open fields. In the game's frames it is a dark blob of
3-4 pixels at 440 ahead, 2 at 467, 1-2 at 476 and gone from about 486;
on the page with the same camera and `RELUVA_NFS_TRACK_STEP` 7-8 of its
pixels at 470-480, 1-3 at 486-490 and none from 492, its far corners
having gone first. Both lose it as its first corners pass 500.

The Tracks page draws the same set of blocks from the block nearest
the camera, with view level 1 (`K` draws every block instead, and
1, 2, 3 fix the road's detail; 4 goes back to the game's choice). It
drops far polygons as the game does (`polygonInReach`): each frame, a
batch whose polygons' first corners may straddle 500 ahead is drawn
from indices of only those within it (not with `K`).

**Moving objects** get the same test (November 2026): in the placed
objects' groups (`0x41ca40`) a type-3 object is placed at its key
(`0x40f4b0`), its vertices moved and projected by `0x41a9b0`, and then
queued by the same loop as the static ones (`0x41b640`), which drops a
polygon whose queued first corner (the file's vertex 1), as placed, is
more than the view's `+0x30` ahead. The page cuts them the same way,
each frame from their corners as placed.

## Tools

- `reluva-tool frd-check <file.frd>...` reads tracks and counts what's in
  them.
- `reluva-tool frd-obj <file.frd> <out.obj> [detail]` writes the road and
  objects as OBJ, mirrored as Reluva draws them: each face's corners in
  the game's order (a placed object's 1, 0, 3, 2) with the texture's
  corner k on corner k, wound so that OBJ's front is the game's.
