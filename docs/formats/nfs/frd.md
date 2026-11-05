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
  Tracks page dims them so with key I (`dimLight`).

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
    `0x4dedf0` (`0x4bc39b`) does without the splashes (`SPL%d`) at Off;
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
from the run's middle one and walking sideways (`0x41e990`). The byte
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
hidden or out of play; the page has no such cars. Not compared against
a night frame: of Hometown's two moving objects the water wheel stands
off the road at the mill and the other (the last group, 141 keys) flies
over the start, out of a car's headlights.

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
there. The human's car is left out, the camera standing for it, unless
`RELUVA_NFS_TRACK_CARS_HUMAN` is set. With `RELUVA_NFS_TRACK_CAMERA`
from the same sky draw the page shows the game's frame's cars;
`RELUVA_NFS_TRACK_STEP=<g_step>` (`0x7d3684`) holds the moving objects
at that step's keys.

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
racer with no tickets) the chase is given up: patrol, siren off. The
reversing, roadblocks, strips, stops and calls for help aren't
followed.

**The computer cars' driving** (nfs3.exe, November 2026; `aiStep`).
Each step (64 a second) an AI racer runs `car_driver_ai` (`0x409c00`,
state 1) and `car_ai_model` (`0x408600`). The page runs the parts that
pick its speed and lane for each of the three cars in turn, and moves
them along the slices at the speed that leaves:

- **Its lanes** (`0x40e350`): +0x638 the lane at its side offset
  (`0x40e2b0`: 7 + round(offset / the right lanes' width), 6 + ... on
  the left, with the x87's rounding to the nearest, 0-13), +0x63C and
  +0x640 those at offset − and + (its width across the road − 0.5).
- **The speed wanted** (`0x409330`): the speed table's byte for its
  slice pair × the speed scale (+0x7BC: the driver's factor `0x408dc0`
  × the track's `0x40ee80` × the catch-up `0x4090a0`, at most 1: the
  page takes the camera's car as the race's human, at race distance 0,
  and each racer's distance as the slices from the camera's slice along
  its way),
  under the top speed cap × the track's factor. The page's two Corvettes
  have Lead Foot's (skill 2) and, ahead of him, Mulligan's (skill 0, a
  surging driver), from the drivers' table (`0x55226c`).
- **Flashing** (`0x404840`): first the flash is undone when its 64 steps
  are up (`0x40deb0`: high beams back to low, low back to off); then,
  with a car ahead in its lane within 2 slices (`0x56ef3c`, `0x56ef48`
  from `0x40cfc0`) and the speed wanted more than 8.94 m/s
  (`0x536194`) over its speed, past the race's start (`0x40f050`), the
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
  other way). Halved for a car only partly in that lane. Left out, as
  the page has nothing they act on (read November 2026):
  - `0x40d720`: eight 0x50-byte records at `0x7dd850` (in use when
    the first word isn't 0; a slice at +0x34, a lane at +0x38): a lane
    holding one 1 to 5 slices ahead in the car's direction gets −50 in
    row 5 (`0x56eecc`),
    and if it is the car's own lane its +0x7B0 is scaled by
    `0x536724`. What fills the records (roadblocks or wrecks, most
    likely) wasn't followed;
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
- **The aim** (`0x40daf0`): the chosen lane's middle, (L − 7 + ½) right
  lanes' widths or (6 − L + ½) left ones'; for the lane just past the
  road's lanes, the car's half width past the edge; within the walls
  less the half width; then the nudge. +0x7C8 moves 0.2 a step towards
  it (`0x408870`), the car's +0x7B8 following. A car aimed at a lane's
  middle 1.5 widths out reckons itself a lane further on (1.5 rounds to
  2), so the lanes' votes are taken from there; next to a lane without
  a strip it is nudged back.
- **Its speed** (`0x4066c0`): towards the speed wanted, up by the
  acceleration table of its `carp.txt` (P +0x2EC, one value a m/s at
  round(speed − 0.5), × 0.9 with ai Normal past race distance 100 and
  × 0.9 in the weather under 45 m/s; nothing over the top speed cap ×
  its scale), down by the maximum braking deceleration × 1.25 for an AI
  racer (× 2 for a police car instead; `0x4063e0`). **The brake** (+0x505,
  and +0x509 for the glows) is 255 only when it is more than 5 m/s too
  fast (`0x53624c`); under that it slows without its brake lights. The
  speed tables drop at bends, so the cars brake into them.

- **The side offset as driven** (+0x7B8, `aiMove`): the game's car is
  steered at its aim (+0x7C8) by the controllers above and its lanes
  (`0x40e350`) are taken at +0x7B8, the car's own offset, not at the aim.
  The page drove each car exactly on its aim until November 2026, and a
  car aimed at a lane's middle 1.5 widths out then sat in the lane past
  it for good. The page now moves +0x7B8 towards the aim as a spring and
  damper (100 /s², 14 /s, at most 70 × (1 + 0.0015 × speed) m/s²),
  fitted to `-watchmem` of car 1's +0x7B0-+0x7CC over 376 steps of a
  Hometown race (`t6-watch`): 1.1 m rms, about as good from 0.6 to 1 of
  critical damping; the rest is the road's bends and the car's heading,
  which the page doesn't model.

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
