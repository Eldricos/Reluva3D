# Need for Speed III: `.col` files (virtual road, moving objects)

Status: **implemented** (`src/engines/nfs/col.cpp`), and checked on all 9
retail tracks.

## How it was worked out

October 2026, from hex dumps and survey scripts:

- **Starting point.** The block ids follow the NFS modding community's
  descriptions of NFS III's COL.
- **Blocks.** Every block's size field accounts for exactly the space to
  the next.
- **Structures.** Every structure's size is 8 + 16 × vertices + 6 ×
  polygons.
- **Objects.** Every object's size is 8 + 20 × keys.
- **Virtual road.** Its fields were read off the data:
  - the positions step along the road (Hometown's first points, a few
    metres apart, next to block 0's centre);
  - the forward vector matches the step to the next point;
  - the right vector is perpendicular to it.
- **By eye.** Drawn as barriers on the Tracks page (**G**), the walls
  follow every bend just outside the shoulders, the right one along the
  fence.
- **Mask and side codes.** From the nfs3.exe code that reads bytes 12
  and 14 (found from every use of the points' pointer, `0x5dd948`), and
  counts over all nine tracks' points.

All values are little-endian.

## Layout

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `COLL` |
| 4 | u32 | 11 (version) |
| 8 | u32 | file size |
| 12 | u32 | number of blocks |
| 16 | 4 × n | each block's offset, counted from offset 16 |

Each block starts with:

- u32 size, these 8 bytes included;
- u16 id;
- u16 number of records.

| Id | Records | On |
|---|---|---|
| 2 | 8 bytes each: texture references, not decoded yet | all |
| 8 | 3D structures | 7 tracks |
| 7 | objects | 7 tracks |
| 18 | more objects, same layout as 7 | Atlantica (`trk002`) |
| 15 | the virtual road | all |

### Structures (id 8)

Each structure is:

- u32 size;
- u16 number of vertices;
- u16 number of polygons;
- the vertices, 16 bytes each: 3 floats and a u32;
- the polygons, 6 bytes each: u16 texture, then 4 vertex indices as
  bytes.

### Objects (ids 7, 18)

Each object is:

- u16 size;
- u8 type;
- u8 structure index.

Every retail object is type 3, moving. A type 3 object continues with:

- u16 number of keys;
- u16 delay;
- the keys, 20 bytes each: 3 × s32 position in 16.16 fixed point, then
  4 × s16 quaternion in 2.14 fixed point.

These are the same keyframe paths as the `.frd` file's moving objects
(Hometown's 141-key loop is in both). The game's object sounds use block 7 only, its
n-th type-3 object taking row n of a per-track table (`0x410e30`,
`0x5dd840` set by the loader `0x41dcc0`; id 18 is not read there):
see [bnk.md](bnk.md), The moving objects' sounds. Block 7 holds 1
object on Hometown (the loop), 4 on Redrock Ridge, 1 on Atlantica (with
2 more in block 18), 3 on Lost Canyons, 2 on Aquatica, 2 on Summit and
5 on Empire City. Type 1, a still object with a
16.16 reference point, follows the community's description, untested.

### Virtual road (id 15), 36 bytes per point

There are about eight points per track block (Hometown: 959 for 120
blocks).

| Offset | Size | Meaning |
|---|---|---|
| 0 | 3 × s32 | position, 16.16 fixed point, in the track's coordinates |
| 12 | u16 | a mask of strips across the road (below): bits 0-7 on the right, 8-15 on the left |
| 14 | u8 | two 4-bit codes, one for each side's edge: the low one right, the high one left (below) |
| 15 | u8 | 0; 3 throughout `trk004`. Not read anywhere found |
| 16 | 3 × s8, u8 | normal (÷ 127), then the lanes: the high 4 bits on the left, the low 4 on the right |
| 20 | 3 × s8, u8 | forward, then the left lanes' width |
| 24 | 3 × s8, u8 | right, then the right lanes' width |
| 28 | s32 | the left wall's distance from the point along −right, 16.16 |
| 32 | s32 | the right wall's distance along right, 16.16 |

From nfs3.exe (October 2026; the block loader `0x41e370` keeps a pointer
to the points, used in place):

- **Lanes.** The road's sides are lanes × width × ½ from the point
  (`0x408cbb` tests a car's sideways position against both;
  `0x42006d` finds the right edge). Hometown has 1-2 lanes a side, 15-16
  wide. The Track page draws these edges with the walls (key G); on
  Hometown they lie on the painted edge lines.
- **The mask** is read in three places only, all in the automatic
  camera views (the camera code's dispatcher `0x426450` and its table
  `0x4263f0`; the views are cycled from a 21-entry list at `0x55fe68`,
  each held at least 320 ticks, `0x4bdcc0`). `0x423fe0` asks whether
  the set bits are one run:
  - views 8 and 14 (`0x4250bc`, `0x425c4c`): when the camera's point or
    the car's point has a gap, the camera is put on the road's edge on
    the car's side: the right edge if the car's lane slot (car `+0x638`,
    0-13, 7 and up on the right; `0x40e2b0`) is 7 or more, else the
    left (`0x41ffd0` finds the edge);
  - view 9 (`0x425f9c`): a gap makes the director move on to the next
    view (`0x4bdbd0`).

  Nothing else reads it, so what each bit stands for comes from the
  data (all 12,706 points of the 9 tracks): the bits are strips of road
  surface between the walls, bits 7 and 8 either side of the point.
  Bits 6-9 are set on 98 % of points; the outer bits follow the walls:
  where the right wall is 30 away and the left 12, bits 3-9 are set.
  The strips aren't of one width (the outermost of 2 bits on a side
  goes with walls 6-22 away, 10.7 on average; of 5, 10-59, about 30).
  A gap (9 to 227 points a track) is a strip that isn't road: a
  divided road's median.
- **The side codes** say what each side's edge is. The low 4 bits are
  the right side's: `0x430bf1` takes the low code when the car is on
  the side the right vector points to and the high one on the other,
  and `0x4a03ad` tests the low code against the right wall's distance.
  Over the 25,412 sides: 0 277, 1 1,746, 2 3,697, 3 35, 4 13,644, 5
  6,013. What the game does with them:
  - **0, no wall.** A car's box corners past a wall are pushed back
    (`0x4a0300`-`0x4a042a`), but only on a side whose code isn't 0.
  - **2, covered** (a tunnel or roof). `0x420bf0` says whether a point
    is under cover: the code of the side it is on (the other side's if
    that one is 0) is 2. It has 16 callers, among them:
    - the sound: on entering, the effects bus is turned up to 127,
      echo (`0x4174dd` → `0x418770` → `0x4e9dfc`, "SNDfxmasterlevel"),
      and back to 0 on leaving (`0x417526`);
    - the rain: with weather on (`0x6fd4cc`, the `w` sky variant,
      `0x41439e`), it isn't drawn under cover (`0x491c1b`);
    - a camera distance: a table value (`0x552e54`) instead of 25
      (`0x421bbf`).

    Every track has some; on Hometown, 36 points with 2 on both sides.
  - **Wall hits.** After a wall collision, the car's impact field
    (`+0x170`) is `0x40000` + the code of the side hit, 4 when on
    neither (`0x430c2e`). The code is read only after the bounce has
    been worked out, and the push-back (`0x4a0300`) only asks whether
    it is 0, so the codes don't change friction or bounce. They pick
    the effects, through two tables of 8 in the impact code
    (`0x42ac40`): `0x553740`, the particle kind (codes 1, 2, 4: kind
    3; 3, 5, 7: kind 2; 0, 6: none), and `0x553760`, the sound's
    material (8, 8, 8, 16, 8, 17, 16, 16). Followed in the code
    (October 2026); what a hit does, when the impact (`+0x168`) is
    over 5 (`0x42acea`):
    - **A jolt** (`0x4295a0`): two wobbles of the car's body, periods
      16-23 (random) and 35, sizes from the impact (× 1/45, at most
      1.5) and a value from the car's data (`+0x528` → `+0x294`). They
      are added to car `+0x920` and `+0x924` (`0x42a9bd`), the drawn
      body's pitch and roll (`0x4b93c0`; see physics.md "Checked in
      runs"). Not side-specific: other hit types do it too.
    - **One particle** of the code's kind at the contact point
      (`+0x178`, `0x42aee7` → `0x4cae00`).
    - **The camera director** gets interest 5 for the car (`0x4bd9d0`,
      read by `0x4bda00`), the most of any hit type.
    - **A sound**, queued on the car (`0x42b115` → `0x4284e0`, up to 8
      at `+0x7dc`) as category 5 (`0x553780`), the material from
      `0x553760`, loudness impact × ¼, at most 10 (127 at `0x411ad5`).
      The car sound code (`0x4119df` → `0x416d90`) turns category 5
      and the material into a slot (`0x4149e0`) of `Gen.bnk`
      (`Gen3d.bnk` when `0x5525a4` is set, `0x4141fc`; the bank is
      table entry 2, `0x414236`), played on one voice (`0x417080`,
      voice `0x1f`): material 8, slot 30 or 32 at random, 33 when
      louder than 110; material 16, slot 30; material 17, slot 29.
      Measured on `Gen.bnk`'s decoded sounds (October 2026; not
      listened to, so not named; energy centroid, the frequency below
      which 85% of the energy lies, and the time by which 90% of it
      has played):

      | Slot | Used for | Length | Centroid | 85% below | 90% of energy by |
      |---|---|---|---|---|---|
      | 29 | material 17 (code 5) | 0.44 s | 300 Hz | 230 Hz | 0.11 s |
      | 30 | material 16 (code 3), material 8 | 0.53 s | 80 Hz | 60 Hz | 0.18 s |
      | 32 | material 8, material 18 | 0.82 s | 760 Hz | 1.4 kHz | 0.42 s |
      | 33 | material 8, louder than 110 | 1.18 s | 1.6 kHz | 4.7 kHz | 0.69 s |
      | 34 | material 18 | 0.40 s | 970 Hz | 1.9 kHz | 0.28 s |
      | 35 | material 18 | 0.80 s (44.1 kHz) | 1.9 kHz | 3.5 kHz | 0.47 s |
      | 37 | material 18 | 0.53 s | 960 Hz | 1.9 kHz | 0.29 s |

      So 29 and 30 are short dull thumps (30 almost all under 100 Hz,
      peaking 0.15 s in), 32 and 33 longer and bright (33 the
      brightest and longest, the hard hit), and the burst's four
      (32-37) mid-bright, 0.4-0.8 s. All peak near full scale (RMS −8
      to −10 dBFS). The levels from the slots' PT headers are the
      same (`0x06` = 20).

      `Gen3d.bnk` holds the same sounds in the same slots: it is a
      version-4 bank, whose slot table starts at 20, not 12 (two more
      words: the audio's size and −1; the game's parser, `0x50265b`,
      `0x502686`). So the game needs no allowance. Read as a version-2
      bank its sounds seem two slots up, which is what was seen; see
      [bnk.md](bnk.md).

    Then, for any wall hit (`0x42b11a`), by car `+0xb8` (its speed by
    the look of it):
    - 22 or more, and car `+0x15c` under 3: kind-3 codes get
      a burst of kind-3 particles (`0x4cac20`; 30 of them, fewer below
      speed 40, thrown with the car's velocity ± 3) and a second sound,
      material 18: slot 32, 34, 35 or 37 at random (`0x4149ac`); kind-2
      codes get one more kind-2 particle. With `+0x15c` 3 or more,
      every code gets a kind-2 particle. `+0x15c` is the car's
      clearance, its lowest corner's height over the road, set each
      step by `0x4a0440` (`0x4a289a`; [physics.md](physics.md), Car
      dynamics). So a car hitting a wall 3 or more above the road
      (in the air) makes a puff of dust and no sparks or second
      sound. The test is an integer compare of the float
      (`0x42b152`), right for any sign;
    - under 22: a kind-6 particle when the speed or the impact is over
      15 (`0x42b248`).

    Every one of these happens only on a tick the kind's throttle
    allows, so a slow hit's smoke often doesn't come (Particles, below).
    The single particle is `0x4cae00`: the kind's own, at the contact,
    with no velocity given, spinning either way (`+0x34` × ±1 from the
    random number's bit 0); kind 0 makes nothing.

    Also from the impact code but not side-specific: a heavy hit (over
    30) adds to car `+0x8d4` (`0x42ad15`), damage perhaps; and
    `0x471270` for one tracked car (`0x471dc0`). Not followed.

  So 1, 3, 4 and 5 differ only in their particles and sound:

  | Code | Particles | Sound | Where (counts of sides) | Inferred material |
  |---|---|---|---|---|
  | 1 | sparks | material 8 | 1,746: Rocky Pass 801 (mostly left), Summit 629, Empire City 136, Redrock 123 | metal guard rail |
  | 3 | dust | material 16 (slot 30, the dullest thump) | 35: Summit 26, Aquatica 6, Country Woods 3 | wooden fences (Summit's town; `Walls`, below) |
  | 4 | sparks | material 8 | 13,644: every track, most of the city and canyon ones | rock, concrete, buildings |
  | 5 | dust | material 17 (slot 29) | 6,013: Country Woods (nearly all), Summit, Rocky Pass, Hometown | grass, dirt or snow verges |

  The materials come from the Track page (walls, key G, October 2026):
  Rocky Pass block 88, a guard rail on the left (code 1) and a rock
  face on the right (4); Empire City block 4, a railing on the right
  (1) and a building on the left (4); Hometown block 8, grass banks
  with wooden fences (5); Country Woods block 40, snow banks (5), though
  a brick wall stands there too.

  Code 3 (looked at on the Track page, October 2026) comes in short
  runs of 1-7 slices: Summit slices 1555-1629 (blocks 194-203, both
  sides, walls 18-31 away), Aquatica 836-838 and 917-919 (blocks 104
  and 114, left, 13-15 away) and Country Woods 518-520 (block 64,
  right, 10 away). Aquatica's block 104 is the seaward edge, a low
  bank above the water. With its dust and the dullest of the thumps
  (slot 30, below 100 Hz) it reads as a soft edge, snow or earth banks
  perhaps, but nothing in the pictures sets these few slices apart from
  the code-5 banks around them, so it stays unnamed.

  **Correction (November 2026).** The planks, timber arcade and railing
  first seen at Summit's were the Tracks page's wrong pictures (it took
  the sheet's pictures by name, not by place; [frd.md](frd.md),
  Textures). With the right ones, Summit's code-3 slices are in a
  small brick town with snow on the roofs and kerbs (a trading-post
  sign at block 194), just before the start: left
  1555-1557, right 1562-1564 and 1568-1571 and left 1571-1573 by the
  buildings, then left 1600-1601 and 1610-1616 and right 1621, 1625-1626
  and 1629 as the town ends. (These last were first taken for a raised
  snow bank; looked at again with the walls guide showing the codes,
  below, every code-3 run follows a wooden plank or log fence: low log
  rows and crates in front of the stone buildings at 1562-1570, plank
  fences at 1556, 1572, 1600 and 1610-1616, the snow lying in front of
  them.) The rest of the
  town's walls are code 4. `Summit_Day` (4:01-4:05) and `Summit_Night`
  (about 5:00) drive through this town without touching a wall,
  so the footage doesn't show a code-3 hit. Hometown's and Empire
  City's set 0 and Country Woods' were right.

  **Rocky Pass again** (November 2026). Its sheet is off by up to three
  from place 92 too, so its walls were looked at again with the right
  pictures (and the game's back-face culling, frd.md), against the
  codes in `tr03.col`:
  - block 88 (slices 696-719, left 1, right 4): a metal guard rail on
    wooden posts along a drop on the left, a grey rock face on the
    right, as first written. `Rocky_Pass_Day` (2:08) passes a place
    like it: rail on the left above the valley, rock on the right;
  - block 21 (162-176, left 4, right 1): a rock face on the left, a low
    guard rail on the right above a snowy slope;
  - block 49 (371-415, 4 on both sides): a concrete parapet on the left
    and a rock face on the right, so code 4 is concrete too;
  - block 68 (527-573, 5 on both sides): grass and earth verges with
    pines behind them;
  - block 77 (593-650, 2 on both sides): a lit concrete tunnel, as in
    `Rocky_Pass_Day` (1:28).

  So Rocky Pass's code 1 is a guard rail on either side and nothing
  else seen.

  **Code 3 against code 5 in the code** (November 2026). The side code
  has four readers: `0x430bf1` (copied into the impact field, +0x170 =
  0x40000 + code), `0x4a03ad` (is it 0?), `0x420bf0` (is it 2?) and the
  impact code `0x42ac40`, where the code (`+0x170 & 0xff`, `0x42ac5f`)
  indexes only `0x553740` and `0x553760`. The other readers of +0x170,
  the force feedback `0x471270` (it tests only the high half, hit type
  1) and `0x468c7b` (another struct), don't look at the code. Both
  tables give codes 3 and 5 particle kind 2; they differ only in the
  sound's material, 16 against 17 (slot 30 against 29, `0x4149e0`).
  So nothing else tells 3 from 5, and the exe has no material name for
  it (no string names a surface); what code 3 is meant to be isn't in
  the exe.

  **Code 3 against footage: wooden fences** (November 2026). `Walls`
  (the Summit by day, chase view, 1280 × 720; times are clip times,
  race clock 6:04-8:43) runs into the town's walls on purpose. The hits
  were placed by the town's landmarks against the Track page with the
  walls guide (key G) showing the codes, and their sounds found by
  correlating the clip's audio (22.05 kHz, mono) with each decoded
  `Gen.bnk` slot's waveform: a played slot gives a sharp peak of the
  normalized correlation, 0.25-0.63 against a median of 0.02-0.05 (the
  slots overlap each other by at most 0.27, but 37 with 30 by 0.40 and
  with 32 by 0.57, so a 37 beside a 30 or 32 isn't counted).
  - **Code 3** (0:43.66: the car runs straight off a left bend at 48 mph
    into the log row in front of the grey stone building with the arched
    door, right 1562-1570; 1:31.70 and 1:32.85, a plank fence on the
    right; 1:43.77, the plank fence by the white stone building on the
    left, 1572): tan puffs at the fence's foot, the day dust `SP02`, and
    slot 30, a dull sub-bass thump (68 % of its energy under 60 Hz).
  - **Code 5** (0:37.43, a snow bank on the right; 1:18.23 and 1:20.33,
    the bank on the left beyond the town, driving the wrong way; 2:34.41
    and 2:39.59, ploughing into the banks after the line): the same tan
    puffs (with the wheels' white spray, kind 10, when ploughing) and
    slot 29, a pitched thump (70 % of it at 100-170 Hz, peaks 113-159 Hz).
  - **Code 4** (0:11.54 and 1:30.00 at 48 mph, brick fronts): slot 30
    with slot 32 at the same moment (material 8's 30 or 32, then the
    second sound, material 18); slow brick hits (0:12.90, 0:21.76,
    0:25.73) slot 30 and grey wheel and hit smoke, no dust.

  So the code is confirmed: 3 and 5 make the same dust and differ only in
  the sound, 30 against 29, and code 3 is the wooden fences (a dull
  knock where the snow banks give a higher thump). Comparison frames and
  the page's views of each run were kept out of the repo.

This is what the game drives on and keeps cars within. It will matter
for driving, AI and the camera. Reluva reads it, and uses it for the
walls guide and the wall hits' particles (below) so far. The walls guide
(Track page, key G) draws each side red on the left and green on the
right, blue where the side's code is 3 and white where it is 5.

### Particles

The wall hits' sparks, dust and smoke, read from nfs3.exe in October
2026, from making to drawing. Reluva has them in
`src/engines/nfs/particles.cpp` and shows them on the Track page (key P
at the wall nearest the line of sight; Shift+P a fast hit). The ticks
below are the game's, 64 a second (`0x7d3684`); a particle's life is
counted every other one.

**Templates.** A kind's 128 bytes at `0x560b64` + kind × 128 (static
data; 14 kinds, the wall hits' here; the other kinds the cars make are
under Wheel spray, below, and all 13 that aren't empty are in
`particles.cpp`). What the code reads:

| Offset | Read by | Meaning | 2 dust | 3 sparks | 6 smoke |
|---|---|---|---|---|---|
| +0x00 | `0x4cac30` | a burst's count | 1 | 30 | 1 |
| +0x04 | `0x4ca8f4` | life, ticks of 32 a second | 128 | 16 | 256 |
| +0x08 | `0x4ca88f` | throttle: made only when (tick / 2) % (this + 1) is 0 | 1 | 0 | 3 |
| +0x10 | `0x4ca872` | upward speed | 3.1 | 2 | 1.5 |
| +0x14 | `0x4ca878` | speed along the first direction given | 2 | 5 | 0.5 |
| +0x18 | `0x4ca884` | speed along the second | 3.5 | 2 | 0.5 |
| +0x1c | `0x4ca916` | each speed × 1 + (random − ½) × this | 0.5 | 0.5 | 0.5 |
| +0x20, +0x24, +0x28 | `0x4caa24` | a sideways push: its turning, size and growth | 0.05, 0.05, 0.047 | 0.02, 0.05, 0.031 | 0 |
| +0x2c | `0x4cb427` | added to the upward speed each tick (gravity) | 0 | −0.25 | 0 |
| +0x30 | `0x4ca9fa` | spin, turns a tick | 0.5 | 0 | 0.1 |
| +0x34 | `0x4ca88c` | part of the velocity given (a car's) | 0.5 | 0.5 | 0.5 |
| +0x38 | `0x4cb4d0` | drag: speeds, spin and push × this each tick | 0.8 | 1 | 0.95 |
| +0x3c | `0x4cb556` | if set, spin held within ±0.25 | 0 | 0 | 0 |
| +0x40 | `0x4ca9c7` | size: half the picture's diagonal | 0.75 | 0.01 | 0.75 |
| +0x44 | `0x4ca9e9` | growth over the life | 3 | 0 | 2.5 |
| +0x48 | `0x4ca9ca` | size × 1 + random × this | 0.5 | 0 | 0.5 |
| +0x4c | `0x4caa55` | colour at birth, ARGB | `7FDFDFDF` | `FFFFFFFF` | `7FFFFFFF` |
| +0x50 | `0x4cb388` | taken off the colour over the life, byte by byte | `7F000000` | `FF00C6FF` | `7F404040` |
| +0x54 | `0x4caa5f` | random part taken off the colour at birth | 0 | 0 | 0 |
| +0x58 | `0x4cc026` | the polygon's flags word (`+0x6`) | 0 | 1 | 0 |
| +0x5c | `0x4cab0b` | drawn as a streak | 0 | 1 | 0 |
| +0x60 | `0x4cbdd1` | farthest drawn, units (× 1, 1, 0.85, 0.7 by the option at `0x6fbc28`, `0x4ca480`) | 150 | 50 | 150 |
| +0x64, +0x68 | `0x4ca515` | `SLnn` pictures and something near the camera (`0x4ca500`); not these kinds | 0 | 0 | 0 |

**Making one** (`0x4ca850`: kind, position, scale s, directions a and
b, velocity c). It takes a free slot of the 600 at `0x7ddfd0` (128 bytes
each), else the one with the least life (`0x4ca4b0`). Nothing is made
when the Special Effects option (`0x6fbc30`, not the Horizon: see
[frd.md](frd.md), Lights) is Off, or when it is Low on
every other of the throttle's turns ((tick / 2) % (2 × throttle + 2)
is 0, kept at `+0x7d`; the drawing skips those too), or off the
throttle's turn. Then life and its maximum from `+0x04`; the three
speeds each × 1 ± spread/2 (random numbers in the order up, along,
across); velocity = s × along × a + s × across × b + c × `+0x34`, plus
s × up on y; size `+0x40` × (1 + random × `+0x48`); growth `+0x44` × s;
angle a random part of a turn; spin `+0x30` × s; the push from
`+0x20`-`+0x28` at a random angle; the colour; the picture
(`0x7ddd68` + kind × 44). A particle with no velocity at all is freed
at once. The random numbers are `0x4eb230`'s (`0x4e0d88`'s low 16
bits / 65536); Reluva uses its own generator for them.

**The spawners.** `0x4cae00` (one particle, above): s 1, a, b and c 0,
so it only rises. `0x4cac20` (the sparks' burst, given the contact and
the car's speed / 2, `0x42b17a`): 30 sparks (`+0x00`), or
round(30 × speed / 20) when that speed is under 20, so 16-29 for car
speeds 22-39; a = the car's velocity (`+0xa4`) as a unit vector, b =
from the car's position (`+0x98`) to the contact as one, c = the car's
velocity + 6 × random − 3 on each axis, s = 0.75-1 at random. Each spark
then loses up to half its life (random % (life / 2)) and gets 0-2
children (random % 3, `+0x7e`). When a spark with children dies,
`0x4cab60` makes that many more sparks where it was: a its velocity's
direction, c its velocity, s 0.25-1, half the life (8 of 16, so they
start half orange).

**Each tick** (`0x4cb2c0`, in the 64 Hz task list after the cars,
`0x4bc931`), for each live particle: on odd ticks only,
- part = (maximum − life) / maximum;
- the push (if its turning isn't 0): velocity x += sin(push angle) ×
  push size, z += cos(push angle) × push size; its growth and turning
  × drag; push size += growth; push angle += turning (angles in turns,
  `0x4ea930`);
- the spin: angle += spin, then spin × drag;
- size = birth size + growth × part;
- on ticks with bit 1 also set, the colour: each byte = birth byte −
  round(`+0x50` byte × part); so sparks go from white to `FFFF3900`
  (orange) as their alpha goes to 0, dust and smoke fade from half
  opacity;
- velocity x, z × drag, y = (y + gravity) × drag;
- life − 1; freed when the alpha reaches 0; children made at 0.

And every tick, odd or even, position += velocity / 64 (`0x4cb46e`).

**Drawing** (`0x4cbc90`, per view). Each particle in the camera's
space: not drawn behind the camera or past `+0x60`. Its half size on
the screen is size × the projection's scale / depth, rounded to whole
pixels (at depths under 1 it isn't divided). The quad (`0xa0` bytes) carries the particle's colour at its
four corners, its kind's picture and the template's `+0x58` as its
flags; quads are sorted by depth (`0x4c7610`) with the scene's.
- **Sparks** (`+0x5c` set): a streak from where the spark was. The
  game keeps, per view, the last screen point (`+0x58`) and whether it
  was drawn (`+0x78`); drawn last frame, the corners are that point
  twice and the spark's point ± (w, h), w and h its half size held to
  0.5-1 pixel, and the kept point moves halfway to the spark; else a
  dot of w × h from the spark's point, which is kept. So the streak's
  length is the screen movement over about two frames.
- **The rest:** a square turned by the angle: corners at (−A, B),
  (B, A), (A, −B), (−B, −A) from the centre on the screen (x right, y
  down), A = sin(angle) × the half size by the view's y scale (sy,
  −330.33: negative), B = cos(angle) × it by the x scale (sx, 320)
  (`0x4cc235`; the particle's +0x38 and +0x3C hold the cosine and
  sine). The picture's corners (0, 0), (1, 0), (1, 1), (0, 1) go to them
  in that order. Since sy is negative, A has the opposite sign to the
  sine: in `e6-runs/t8d`'s list the smoke's first corners are (+sin,
  +cos) × the half size, to 0.2 pixel for all 8 quads against the dumped
  angles, and the sine's part is 330.33 / 320 longer (36.0 against
  35.1 for a half size of 36.1). Until November 2026 the Track page
  took A as sin × half, which mirrors the picture across its diagonal.
- With renderer flag bit 6 (`0x7a3a58`, Direct3D) set and the effects intensity
  under 1, kinds with `+0x58` bit 0 are dimmed (`0x4cbf2f`): each byte
  of the colour × round(f × 65536) >> 16 (`0x540448`, 65536.0), not
  quite the lights' k = min(round(f × 65536), 0xFFFF) >> 8, byte × k >>
  8 ([frd.md](frd.md), Lights). The screen-space quads of `0x4cb750`
  (below) are dimmed the same way. The Track page does this in its
  Direct3D look (Shift+Y; `dimParticle`).

**Pictures** (`0x4cb590`): `SPnn` of `GameData/Render/pc/sfx.fsh` for
kind nn by day, `SNnn` by night. The first account here had them the
other way round; the code is `cmp dword [0x6fd4c8], 0` / `je 0x4cb5ad`
(`0x4cb64a`), and `0x4cb5ad` pushes `'SP%02d'` (`0x540430`), the
fall-through `'SN%02d'` (`0x540428`); `0x4d4080` loads each into
`0x7ddd68` + kind × 44 (November 2026). The day set is the brighter:
`SP06` (smoke) is grey, its red 33-148 at the texels where `SN06`'s is
9-75, their alphas the same (up to 217); in `e1-t8s0`'s THRASH list
(Empire City by day, frame 2818) the smoke's texture is 33-153 in 4-bit
steps, `SP06` as uploaded at 4:4:4:4. `SN03` (sparks) is 8 × 8, opaque,
yellow; `SN02` (dust) a dark brown puff, 32 × 32 with alpha. The Track
page took `SN` by day until November 2026, so its smoke and dust by day
were the night's, about half as bright.

**In a THRASH list** (`e1-t8s0`, Empire City by day, Hot Pursuit with
racer 1 held 18 m ahead by pokes; the particles' 600 slots dumped at sky
draw 400, the frame the list is of): the 8 live particles (all kind 6,
from the held car's rear wheels, below) are 8 quads of one batch, after
the cars and glows, on the 32 × 32 picture with blend 1 mode 0 (source
alpha over), each corner in the particle's colour (`64F1F1F1`,
`5CEDEDED`, `4CE5E5E5`, `3EDEDEDE` for lives 199, 183, 151, 123), their
corners' u, v 1/64 to 63/64, fogged as the scenery (the batch carries
the frame's fog state, 475). The dump's positions projected with the
dumped camera land on the quads' centres to 0.1 pixel, and their
half sizes (size × 320 / depth) on the quads' extents.

**Its brightness** (November 2026). By day the page's smoke was 9.5-10.7
brighter than `nfs-host`'s frame of the same moment (Empire City frame
2816, the region round the held car's tail), the scenery beside it only
1.5-4. The colour fade, the picture (`SP06`, 4:4:4:4 as uploaded), the
fog (475, both) and the blend (source alpha over, both) agree; the
difference was the host's 16-bit buffer, which truncated each blended
layer where `voodoo2a.dll` has Glide dither 4x4 ([thrash.md](thrash.md),
Dithering). With the dither the smoke is 2.8-4.1 under the page's and
the scenery 0.8-2.4 (`e5-t8d0`). Of the 3-4 left (3.1-4.4 in `e6-runs/t8d`), about 1 was
the picture: the game uploads `SP06` at 4:4:4:4 (`0x4d27e0`), which the
page now does too (2.3-3.6 left); the rest is the host's truncating
blend into 5:6:5, rounded to the nearest within 0-1 of the page's
([thrash.md](thrash.md), Dithering). How the Voodoo rounds isn't known,
so that part stays.

**Its spread over the car** (November 2026, `e7-runs`: `e6-runs/t8o`,
frame 2816, against the page from the same dumps, the held car seen
from its right rear). The page's smoke spread further over the car's
front than the game's. The puffs' places and sizes were not the cause:
the dump's 8 particles project onto the list's quads (`t8d`) to 0.1
pixel and their half sizes to 0.2; no puff moves (velocities under 0.1
m/s, held by the dump) and their depths (18.0-18.2 against the car's
19.05) put them over the car in both. The cause was the picture's
corners (above): `SP06`'s dense part sat on the other side of each puff.
With the corners as the game sets them, over the car's front (434-456,
238-256 of the 640 × 480 frame) the page went from +5.9, +6.1, +5.8 over
the game's to +4.9, +5.0, +4.7, and the strip under the car (440-500,
258-263) from +6.4, +6.9, +8.5 to +3.9, +4.3, +5.9; over the puffs
behind the car (472-498, 236-258) it is +2.8, +3.4, +3.8 (+1.7, +2.5,
+2.9 before). What is left over the front is mostly the car: without
the particles the page's car there is 85, 74, 61 where the game's frame
(with its smoke) has 79, 75, 70, so the car's own shading differs.

**Over the cars** (November 2026). In that frame the smoke's batch comes
after all the car's polygons and covers its back, by day greying it and
by night (`e1-t8s1`, `SN06`) darkening it: nfs-host's renderer runs
without a z-buffer (`0x7a3a60` 0, `setstate(0x4, 0)`: thrash.md), and
then the game draws its sorted items back to front in 2,000 buckets of a
quarter metre. A car goes in whole (`0x49c420` → `0x4c7400`) at its
box's mean view depth (`+0x8B8` − 9.5 + 9.5, `0x7dcfe0` and `0x7dcfdc`
both 9.5 without the z-buffer, `0x4c7a60`), bucket round(4 × depth); a
particle's quad (`0x4cc5c0` → `polys_depth_sort` `0x4c7610` →
`0x4c7480`) at the mean of its corners' depths the same way, after the
cars. So a particle no farther than a car's middle is drawn over all of
it: the held car's smoke at 18.0-18.2 against the car's 18.7. With the
z-buffer (`0x4c7610`'s other branch, 800 buckets, and the car drawn at
once by `0x4349d0`) the car's nearer polygons would hide it. The Track
page, which draws with a depth test, follows nfs-host while its
projection is off (Shift+E): a quad the buckets put over a car it
overlaps on the screen is moved along the lines from the eye to just in
front of the car's nearest corner.

**Tyre smoke** (November 2026; `wheelSkidLevel`, `wheelSmokes`,
`tyreSmoke` in fce.h). `car_spiked_effects` (`0x42a560`, the cars'
64 Hz task) works out each wheel's skid level (`+0x94C` + 4 × wheel:
0 front left, 1 front right, 2 rear right, 3 rear left): 0 with the car
in the air (`+0x164`) or tipped (`+0x10C` under 0.9, `0x537278`); else
the tyres' skid level (physics.md, Tyres) × 7.5 for a front wheel
(`+0x5C0`, `0x537280`) and as it is for a rear one (`+0x5C4`), held to
0-10 (`0x537288`). Then `0x42a480` for the wheel: with the level not 0
and the surface under it (`0x429b40`: the wheel's road polygon's, or
the car's `+0x18C`; & 0xF) marked in the table at `0x553080` (rows of
0x24 bytes by surface, 0x240 by weather `+0x160`; `+0x10` is 1 for dry
surfaces 1, 4, 7, 10, 11 and 12 and 0 for every surface in rain or
snow), a skid mark (`car_wheel_skid_mark` `0x429d20`, not drawn on the
page), and for a rear wheel with the level over 5 (`0x537270`) smoke:
`0x4cb010` with kind 6 at the wheel's road point (its track position,
`+0x300` + 0x78 × wheel, the 16.16 point at `+4`), the template's count
(1) of `0x4ca850` at scale 1 (kind 6's `+0x0C` speed scale is 0), a the
car's velocity (`+0xA4`) made a unit vector (`0x4972f0`, left at 0 when
it is 0), b the car's right row (`+0xC0`) for wheels 1 and 2 and its
negative for 0 and 3, whose particles also spin the other way, c the
velocity × 0.5 (`0x540408`). The same table's `+0x0C` kind is what
`0x42a280` throws from each wheel (Wheel spray, below).
`car_smoke_tick` (`0x42b480`) is the hit's smoke (Car hits, below).

In `e1-t8s0` the kind-6 call (`0x42a543`, `-count` and `-watch`) ran 42
times: twice for the player's car at 49.0 s, at the start, and 40 times
for the held car between 51.9 and 60.0 s, `+0x5C4` 10.0 and the rear
levels 10 at each, so the 8 puffs of its dump come in
pairs a few hundred milliseconds apart; at the dump itself the levels
were 0. The Track page draws a dump's particles where the dump has them
(`RELUVA_NFS_TRACK_PARTICLES`, held as the cars are) and makes the
smoke of a dumped car whose rear levels are over 5, each 64 Hz tick.

**Against footage** (November 2026; clips as in [hrz.md](hrz.md)).
No clip has a clear wall hit near the camera, so the wall hits' own
particles aren't seen. Two nearby events:
- `Summit_Day` 4:45 (285.3 s): the car lands from a crest and scrapes.
  A small yellow blob under its tail, then a fan of thin yellow
  streaks rising and spreading, each tapered to a point at one end,
  up to about 40 pixels long at 1280 × 720 with the chase camera close,
  orange at their ends at the last, gone after about 0.2 s, with grey
  puffs either side lasting longer. So the sparks' picture shows as
  yellow, not white, and the streaks are wedges, as the corners above
  (the kept point twice) make. That this is a kind-3 burst is a guess
  from the look: a scrape isn't a wall hit.
- `Rocky_Pass_Day` 3:39 (219 s): wheels on the grass verge throw dark
  speckled clumps, like `SN05`, not the brown `SN02` puff.

**The second set of clips** (November 2026: `Rain`, Hometown in the
rain; `Snow`, the Summit in snow; `Night`, Hometown then the Summit by
night; 1280 × 720, the game's 640 × 480 at 1.5 × in the middle; times
are clip times, m:ss, with the race clock where it helps):
- `Rain` 1:51-1:53 (race 1:46.8-1:48): the car meets an oncoming
  truck. A large bright pixelated puff at the contact, black smoke
  round the truck for about a second, and fans of thin yellow streaks
  under it for under half a second, as `Summit_Day` 4:45's. A car hit,
  not a wall's: the impacts task's hit type 5 (Car hits, below). At
  1:51.6 the puffs are big, speckled and grey-brown, as `SP02` grows
  for kind 2 (0.75 to 3.75), with the yellow streaks of a burst under
  them.
- `Rain` 1:43-1:51 (race 1:38-1:46), 1:25 (race 1:20): dark speckled
  clumps beside the car where its wheels run on the verge. In the
  weather the surface table (below) gives the verges kind 11, whose
  day picture `SP11` is a dark brown speckled puff: agrees.
- `Snow` 4:27 (race 4:23) and 6:55 (race 6:50): ploughing into snow
  banks, the wheels throw a white speckled spray, the day picture
  `SP10` (kind 10, which the table gives the road surfaces in the
  weather; the banks' polygons are road surfaces to it); at 4:27 a
  tan puff by the left bank as well, like `SP02`, the wall hits' dust
  (codes 3 and 5).
- No clip of this set shows a wall hit with sparks near the camera;
  `Sparks` (below) does.
- **Code 3.** `Snow` drives the Summit's town and the banks after it
  (5:45-7:00, race 5:40-6:56) and hits a parked car (race 6:44) and a
  left bank (race 6:50). Whether that bank is one of the code-3 runs
  (slices 1600-1629) can't be told from the clip, and what a hit there
  shows (`SP02` dust, as code 5's) is what the code already says: the
  two codes differ only in the sound. `Walls` settles it: code 3 is the
  town's wooden fences (Code 3 against footage, above).
- `Night` 1:58 (the Summit by night in the weather, race 0:13): white
  puffs on the bank by the wheels but no snow falling. That's the
  surface table, which goes by the car's weather, not by the road:
  Summit's night weather file sets snow only on slices 400-1290 (Rain
  and snow, below), and the race starts outside them.

**Sparks against footage** (November 2026). `Sparks` (Hometown by
day, chase and in-car views, 1280 × 720, clip times) scrapes the rock
cuttings (code 4 on both sides, slices 184-410) and walls. Its hit sounds
were found as in `Walls` (Code 3 against footage, above): slot 30 or 32
with a second sound at 0:35.08, 0:47-0:52, 1:00.15, 1:09-1:11, 1:20.2,
1:29-1:32 and 1:40-1:44.
- At speed (47-66 mph on the HUD: 0:47.32, 0:49.73, 1:09.40 in-car,
  1:28.95, 1:30.67, 1:32.23, 1:40.53) each scrape throws a fan of thin
  yellow streaks, 20-40 pixels long at 1280 × 720 (13-27 of the game's
  640 × 480), low along the wall behind and beside the car, renewed as
  long as the scrape lasts and gone within about half a second of its
  end; their pixels average about (196, 176, 66), yellow. This is the
  burst (`0x4cac20`, 16-30 kind-3 sparks a hit, with `0x4cab60`'s
  children), the `SP03` picture added. The chase camera moves with the
  car, and the sparks keep half the car's velocity (`+0x34`), so they
  stream back past it.
- The burst needs `+0xB8` at 22 or more (`0x5537b8`, m/s). The HUD
  reads `+0x4F4`, the speed along the road ([fsh.md](fsh.md)), so 47
  mph is 21.0 m/s along the road; `+0xB8` is max(|vx|, |vz|) + ¼ min,
  which counts the sideways slip of a car scraping a wall too, so 47
  mph on the HUD passing 22 isn't a contradiction.
- Slow hits (1:43.99, 6 mph) give grey puffs and no sparks: under 22 the
  code makes a kind-6 particle when the speed or the impact is over 15.
  `Walls`' slow brick hits are the same.
- The other bursts in `0x42ac40` aren't walls': hit type 1 (`+0x170` =
  0x10000 | the surface, set by `car_landing` `0x4a1ce0`) bursts the
  surface table's `+0x14` kind at the car's speed with no threshold, at
  a point under the car; that is `Summit_Day` 4:45's landing scrape
  above. Type 5 (car against car, `0x42f280`) bursts at the impact × 2
  (`0x5372d8`) with a dust particle. Type 3 (`car_contact_impulse`)
  bursts when the speed and the impact together pass a value.

The Track page's sparks (Shift+P, Hometown slice 303, the camera 4 in
from the wall) match: thin yellow streaks, about (221, 204, 74) at first,
orange by the end, gone after about 0.6 s. The page's camera stands
still, so its streaks fan forward with the made-up car's velocity where
the clip's stream back. The page's wall-hit code was checked against the
exe again: the tables `0x553740` (0, 3, 3, 2, 3, 2, 0, 2) and `0x553760`
(8, 8, 8, 16, 8, 17, 16, 16), the branch at `0x42b11a`, and the
constants (`0x5403c0` 20, `0x5403c8` 6, `0x5403d0` −3, `0x5403d8` 0.25
and `0x5403e0` 0.75 for the scale, `0x5403b0` 0.75 and `0x5403b8` 0.25
for the children's, `0x540450` and `0x540458` 0.5, `0x5537b8` 22,
`0x5372e4` 0.5, `0x5372e8` 15, the count `0x560ce4` 30) are the page's.
The streak's corners on the screen (y down: the kept point twice, then
(x + w, y + h) and (x − w, y − h); a dot (x, y), (x + w, y − h),
(x + w, y + h), (x, y + h)) are what `streakCorners` makes. One fix:
a particle quad's texture corners come from its picture's record
(`+0x08`-`+0x24`), which `0x4d3f50` sets half a texel in from each edge
(1 / (2 × width), `0x549438` = 2.0), and the polygon drawers use them
for every quad without flag bit 1, streaks included; the page gave
streaks the whole picture and every other quad 1/64, right only for 32 ×
32 pictures. It now takes each picture's own half texel.

**The surface table** (`0x553080`, rows of 0x24 bytes by surface,
0x240 by the car's weather `+0x160`; read November 2026 with the wheel
code `0x42a280`, called for each wheel from `car_spiked_effects`):
`+0x00` the skid level at or under which the wheel throws, `+0x04`
and `+0x08` a speed and a range that scale the level, `+0x0C` the kind
thrown (by `0x4cb010` at the wheel's road point), `+0x10` whether tyre
smoke is made (Tyre smoke, above). Dry: kind 2 (dust) on surfaces 5 and
13, 4 on 2, 5 on 3, 10 on 6, 12 on 4 and 11 (only when skidding), 1 on
9 and 15, none on the rest. In weathers 1 and 2: 10 on the road
surfaces 1, 4, 6, 7, 10, 11, 12, 11 on 2, 3, 5 and 13, 1 still on 9
and 15. Kinds 10 and 11 (templates at `0x560b64`): 10 lives 32 ticks,
size 0.75 growing by 4, colour `7FFFFFFF` fading out; 11 lives 64,
size 0.7 shrinking by 0.5, `FFFFFFFF` fading out. The whole table, by
row (level, speed, range, kind, tyre smoke, landing kind `+0x14`), read
November 2026:

| Surface | Dry | Rain or snow |
|---|---|---|
| 0, 14 | 0, 0, 0, none | the same |
| 1, 7, 10, 12 | 0, 0, 0, none, smoke, 3 | 0, 5, 20, kind 10, 3 |
| 2 | 0, 5, 20, kind 4, 2 | 0, 5, 20, kind 11, 11 |
| 3 | 0, 0, 0, kind 5, 2 | 0, 0, 0, kind 11, 11 |
| 4, 11 | 10, 5, 20, kind 12, smoke, 3 | 0, 5, 20, kind 10, 3 |
| 5, 13 | 0, 3, 20, kind 2, 2 | 0, 0, 0, kind 11, 11 |
| 6 | 0, 5, 20, kind 10, 0 | the same |
| 8 | 0, 0, 0, none, 1 | the same |
| 9, 15 | 0, 8, 20, kind 1, 1 | the same |

`+0x18` is a float (0.01-0.12) and `+0x1C`, `+0x20` the skid sounds'
materials (`+0x990` + wheel, `0x42a3ef`, `0x42a35b`), not followed.

**Wheel spray** (`0x42a280`, November 2026). Each 64 Hz step, for each
wheel of a car on the ground and upright, after the tyre smoke
(`car_spiked_effects` calls `0x42a480`, then `0x42a280`): the row for
the surface under the wheel (`0x429b40`) and the car's weather
`+0x160`; nothing for kind 0. With the wheel's skid level (`+0x94C`)
over the row's `+0x00` the kind is thrown; else, when the row has a
speed and a range and the car's `+0xB8` is over the speed, it is thrown
and the skid level set to (speed − row speed) / range × 5, held to 0-5
(`0x537268`), for the skid sounds. So in the wet every road surface
sprays kind 10 above 5 m/s whether the car skids or not, and the
verges 2 and 13's kind 11 only while skidding (2 also for speed).
`0x4cb010` (kind, wheel, the wheel's road point): the kind's count
(`+0x00`) of `0x4ca850`, at scale |`+0xB8`| / the kind's `+0x0C` (30
for 1, 2, 4, 5, 11; 50 for 10; 15 for 12), plus 0.5 for a rear wheel of
a car with `+0x594` set (not kind 12), held to 1, or 1 when `+0x0C` is
0; along the velocity made a unit vector, across ± the right row,
inheriting half the velocity; a left wheel's particles spin the other
way. Kind 10 isn't made at all with a software renderer (`0x7a3a58` &
6). The wheel's road point is `0x4299a0`'s (from the impacts task): the
car's place a 32nd of a second on (velocity × 0.03125, `0x537228`) plus
(± 0.75 × the half width, − the half height, ± 0.5 × the half length)
(`0x537218`, `0x537220`) along the car's rows, wheel 0 front left, 1
front right, 2 rear right, 3 rear left. The car's weather
(`car_road_contact_task` `0x4a08c0`, Sc32) is the race's kind
(`0x4dcdd0`: `0x8ca22c` when `g_weather`) and 0 when the car is under
cover (`0x420bf0`).

Kinds 10 and 11 (and 1 and 4) also have `+0x64` and `+0x68`: drawn
within 3 of the camera in the main view (`0x4cbec3`), at High Special
Effects and not in the car (`0x79c4a0`), such a particle is turned into
that many screen-space splats (`0x4ca500`, 200 slots a view at
`0x7f0bd0`, life `+0x68` less a random part, drawn by `0x4cb750`) and
freed: mud or water on the lens.

**Splats on the lens** (`0x4ca500`, `0x4cb220`, `0x4cb750`; read
November 2026 with `gdecomp` and the templates read from the exe). The
draw pass (`0x4cbc90`) calls `0x4ca500` for a particle whose depth's
bits are under 3.0's (`0x4cbec3`) in a view whose first word is 0, with
the particle's place along the camera's rows (x, y, z). The kind's
fields, `+0x64`-`+0x7C` of its template (`0x560bc8` + kind × 0x80):
count, life, the quad's flags, half side, jitter, colour, chance. Only
four kinds have them:

| Kind | Count | Life | Flags | Half side | Jitter | Colour | Chance |
|---|---|---|---|---|---|---|---|
| 1 | 10 | 96 | 1 (added) | 0.08 | 0.5 | `7FFFFFFF` | always |
| 4 | 1 | 76800 | 1 | 0.1 | 0 | `8FFFFFFF` | 0.001 |
| 10 | 5 | 128 | 1 | 0.2 | 0.75 | `FFECECFF` | always |
| 11 | 2 | 1024 | 0 | 0.2 | 0.5 | `7FFFFFFF` | always |

With a count and a life, not in the car (`0x79c4a0`[view]) and the
byte `0x6fbc30` under 1, the particle is taken (freed by the caller)
whether or not any splat is made, except when the 200 slots of the view
(`0x7f0bd0` + view × 0x3200, 0x40 bytes) are full. When the chance is 0,
or a random (`0x4eb230`) is at most it, each of the count: the place
(x / z, y / z) (z 2⁻¹⁶ if 0) moved by random × 2 × jitter − jitter
(`0x540398`) each way; no splat where a live one is within half its
half side (`0x5403a0`) both ways, except rain (10) on mud (11), whose
life is halved instead (every rain splat made over it halves it again);
life the template's less `rand_next` % (life / 2); half side the
template's less random × half of it; corners (x − s, y + c), (x + c,
y + s), (x + s, y − c), (x − c, y − s) for s, c the sine and cosine of a
random turn × the half side; colour and flags the template's; for kind
4 also a sound (`0x415010`, called at `0x4ca833` for each kind-4 splat
made): `sound_start` (`0x4182b0`) with the race's bank entry 2
(`0x57113c`, byte 24 of the table at `0x571124`: `Gen3d.bnk`, or
`Gen.bnk` without 3D sound), slot 0xF, volume 0x7F and azimuth 0 (in
split screen, `0x6fd3b0` = 1, 0x3FFF for view 0 and 0xC000 for view 1).
Slot 15 of both banks is a click of 728 samples at 44100 Hz (`bnk-info`). On the odd steps the
particles' task (`0x4cb2c0`) ages them (`0x4cb220`): alpha = the
template's alpha × life / the template's life (whole numbers), then
life − 1, freed at alpha 0. `0x4cc600` draws them (`0x4cb750`), unless
the view's camera mode is 8, 0xE or 0x12: quads at x × `0x56009c` +
`0x5600a4`, y × `0x5600a0` + `0x5600a8` (the particles' projection),
1/z `0x3F7FFF00`, sfx.fsh's `SLnn` (loaded by `0x4cb590` for each kind
with a count), the colour dimmed under Direct3D below intensity 1 for
flag bit 0.

So (64 steps a second, aged every other one) rain spray (10) close to
the camera leaves five fading splats for 2-4 s, mud (11) two that stay
for 16-32 s unless the rain washes them, kind 1 ten small ones for
1.5-3 s, and kind 4 (the dry verge 2's spray) one time in a thousand a
splat that lasts 20-40 minutes.

**On the Track page** (`SplatScreen`, `prepareSplats`): the particles
within 3 ahead go onto the lens as above (the page's camera is never
in a car; a dump's held particles don't), aged on the odd ticks and
drawn after the weather without depth test or fog (the pass's place
and states are inferred), each corner on the line through its pixel.
Each kind-4 splat made plays `Gen3d.bnk`'s (`Gen.bnk`'s) slot 15 at
full volume, ahead (`SplatScreen::takeSounds`). Following a police car in Hometown's
weather (Shift+C), its kind-10 spray fills the 200 slots and faint
rings sit over the view (`h2/splat1-1.png`, before: `g2/spray-2.png`).

The templates of the other kinds the cars make (read November 2026;
count, life, throttle; colour fading by; reach): 1 (1, 64, 0;
`80FFFFFF` by `20000000`; 90), 4 (5, 64, 0; `FFFFFFFF`, no fade; 100),
5 (1, 64, 0; `E4FFFFFF`; 90), 7 (1, 8, 0; `FFFFFFFF` by `FF007FFF`,
added; 150), 8 (1, 56, 2; `FFFFFFFF` by `FF007F7F`, added; 120), 9 (1,
64, 2; `FFFFFFFF` by `FF000000`; 200), 10 (1, 32, 1; `7FFFFFFF` by
`7F000000`; 120), 11 (1, 64, 0; `FFFFFFFF` by `FF000000`; 100), 12 (2,
128, 2; `FFFFFFFF` less a random part of `00007FFF` at birth; 90), 13
(3, 128, 0; 90). Kind 12's `+0x3C` (`0x4cb556`): with the angle over
0.25 the spin becomes −|spin|, under −0.25 +|spin|. The random part
(`+0x54`, `0x4caa8a`) is taken off byte by byte, alpha first, each
round(random × the byte).

**On the Track page** (`wheelSpray`, `wheelParticles`): every car,
every surface; a dump's cars as the dump has them, the computer's cars
with the wheel points above, the surface of the road polygon under
each point, the weather as `0x4a08c0` sets it, and the skid levels
their controller leaves (below): in the wet every car faster than 5 m/s
trails kind 10 from all four wheels, and a skidding wheel on the verges
throws kind 11 (`Rain` 1:25). In the page's weather on Hometown behind a
police car (Shift+C): white speckled trails from its wheels
(`g2/spray-2.png`).

**The computer's cars' skid levels** (November 2026, `field car 0x5c0`
and `0x5c4`). `car_spiked_effects` reads +0x5C0 (front) and +0x5C4
(rear); for a computer car the controllers write them (`aiSkid` in
frd.cpp, called by `aiMove`):
- `ai_ctl_rails` (`0x4076b0`, `0x407b5c`-`0x407bbc`): with a the
  velocity across the road 2 slices on (`0x406a80`, the same the
  controller steers by), over 7 m/s (`0x56ebd0`, set to 7.0 by
  `ai_gains_init` `0x405e60`): front (|a| − 7) × 0.15 (`0x53636c`), rear
  (|a| − 7) × 0.5 (`0x53634c`) while the forward force is a brake, else
  0; both 0 at 7 or under.
- `ai_ctl_slide` (`0x407fa0`, `0x408467`-`0x4084ba`): both |+0x4EC| (the
  velocity along the car's right) − 7, or 0. At the race's start
  (`+0x200` bit 0x8, a computer racer, and race distance under 20, on a random pick by the
  car's slot) the rear is set to 10: a wheelspin (below).
- `ai_ctl_slow` (`0x407c30`) leaves them; `car_ai_physics` (`0x4088f0`)
  sets both to 0 in the air or tipped (`+0x10C` under 0.9, `0x5363d4`,
  `0x40893d`-`0x408959`), and then runs no model, so no controller sets
  them again that tick;
- `ai_finished_coast` (`0x407380`, below) after the finish; the resets
  (`0x40cd80`, `0x42bcb0`) also write them, not followed.

**The start's wheelspin** (`ai_ctl_slide` `0x408244`-`0x4082e0`,
`0x4084b6`; read November 2026 with `gdecomp` and `asm`). Only in the
sliding controller, which a car under 0.5 m/s along its way gets
(`car_ai_model` `0x408600`, `0x5363b4`), and only when it accelerates
(more than 1 m/s under the speed wanted): for an AI racer (`+0x200` &
8) with race distance (`+8`) under 0x14 whose acceleration from
`ai_accel` (`0x4064f0`) equals its table's first value (`+0x2EC`[0]:
the index int(|v| − 0.5) is 0, and none of `ai_accel`'s factors
applied, so dry, not past distance 100 with `ai` Normal), the race's
random number is drawn: `0x554f14` = `0x554f10` × `0x554f18`,
`0x554f10` = its low 16 bits, and the pick ((`0x554f14` & 0xFFFF00) >>
8) × 14 >> 16, 0-13; when it is the car's slot (`+0x1F0`) the car gets
0.5 more side force (and yaw from it, `0x53638c`) and, after the skid
levels are worked out, the rear level 10 (`0x4084bc`). `0x4371d0`
seeds it (from `race_tasks_setup` `0x4c46be` and `0x4c47b3`) with
`0x6fd3b4` (GetTickCount at start-up, net.md): p the seed and u 3 × it,
each made odd; `0x554f14` = p × u × p⁷ with every product but the last
kept to 16 bits, `0x554f10` its low 16 bits, `0x554f18` = p. The same
numbers are drawn by the cops' code (`0x402a00`, `0x403870`,
`0x404840`, `0x405d40`, `0x40c2f0`). So for the few ticks a racer
stands at the start, each tick one racer in fourteen at most (the one
whose slot comes up) smokes its rear tyres (level 10 is over 5:
`wheelSmokes`), dry only.

**A car out of the race** (`ai_finished_coast` `0x407380`, `asm`
November 2026): from `car_ai_model` for a racer (`+0x200` & 1) with
`+0x2C4` 3 (finished) or 4 (busted), on the ground and upright
(`+0x164` 0, `+0x10C` at least 0.9f, `0x40739a`): both levels 0, no
controls, the brake 255; the speed along its forward (`+0x4F4`) × 0.98
(`0x536314`), × 0.95 (`0x53630c`) with `+0xB8` under 22.3515
(`0x536304`); along its right (`+0x4EC`) × 0.85 (`0x53631c`); the yaw
rate (`+0x4FC`) × 0.5 (`0x536324`). Then the rear level |forward| × 0.1
(`0x53633c`) unless the forward is between −10 and 5 (`0x536334`,
`0x53632c`, `0x407473`-`0x407481`), when the forward is halved instead;
plus |yaw| when that is over 0.2 (`0x536344`); and when |right| is
over |forward| both levels |right| and the right × 0.2 (`0x4075a4`).
The velocity and spin go back into the world through the body's rows.

**In Reluva** (`frd.cpp`): `aiMove` zeroes both levels in the air or
tipped (the body's up . the road's normal under 0.9, as `aiTilt` takes
them) and calls `aiSkid` only when not tipped; the sliding controller's
acceleration goes through `aiStartSpin` (`AiStartRandom`, the page's
own stream, seeded once; the car's `AiCar::slot`, −1 unset: never
picked) and sets the rear to 10; `aiFinishedCoast` is the coast's
arithmetic on its own (`AiCoast`). The page's computer cars start
moving, past race distance 20 (`AiCar::distance`), and none finishes
or is out of the race, so neither shows there.

So the game's computer cars do skid: the front level (× 7.5 in
`car_spiked_effects`) reaches 10 on a bend taken at speed, and the rear
only while braking. In a page run on Hometown's weather with a roadblock
(1 minute), 3440 wheel steps had a level over 0, nearly all on rails on
surface 1 (the front wheels).

**Verge clumps on the Track page** (checked November 2026, `i2-runs`):
in Hometown's weather a computer car skidding with a wheel on a verge
throws kind 11 as `0x42a280` says, and nothing was wrong to fix. In a
150 s run with two cops sent (C twice), 76 wheel steps had a skid level
over 0 on surfaces 2 or 3 (cars sliding after hits, the sliding
controller's level at 10). They are rare on their own: the cars keep to
their lanes, and three more runs of 4-7 minutes had none, so for the
picture a car whose wheel was on surface 3 at over 10 m/s was pushed
12 m/s sideways (a temporary hook, not kept): the sliding controller
took over (level 10 from the side speed), and kind 11 particles went
live from the next tick (2, 4, 6... a tick; white speckled trails over
the grass behind it, `i2/push2-e1-2.png`, `i2/verge-clumps-zoom.png`).
On surface 2 the wet row throws kind 11 for speed as well (over 5 m/s),
which is where most of the page's clumps come from (up to about 400
live).

**Car hits** (`car_impacts_task` `0x42b900`, November 2026). The cars'
task (Sc64) after the hits (`car_collisions_task` `0x4311d0`, Sc32):
for a car in play, its wheels' points (`0x4299a0`), the computer's
reaction (`0x406120`, `0x404a00`), then `0x42b780`: `car_smoke_tick`,
`0x42ac40` (the impact's effects), `car_spiked_effects` (tyres) and
`0x42a7d0`. `0x42f280` leaves a hit as `+0x168` the strength, `+0x16C`
the other car, `+0x170` 0x50005 and `+0x178` the point (physics.md).
`0x42ac40` for it, when the strength is over 5 (`0x42ac9d`, compared as
the float's bits):
- the drawn body's jolt (`0x4295a0`, sizes 0x23 and 24-31),
- a burst of sparks (`0x4cac20`) at the point, at the strength × 2
  (`0x5372d8`): 30 sparks from strength 10, fewer below,
- one dust puff (`0x4cae00`, kind 2) there,
- the camera director's interest 4 (`0x4bd9d0`),
- a sound queued (category 5 from `0x553780`, material 5, the low byte
  of `+0x170`, loudness strength × ¼, `0x5372e0`), played by
  `sound_play` (`0x416d90`) on voice 0x1F from `Gen3d.bnk` (`Gen.bnk`
  without 3D sound) at the slot `sound_impact_slot` (`0x4149e0`) picks
  from the loudness × 127 / 10: 33 over 90, 32 over 70, 37 over 50, 34
  over 25, else 30; the same slot twice in a row becomes another (32 →
  37 or 34, 30 → 34, 34 → 30 or 37, 37 → 30 or 34, at random; the last
  kept at `0x552d80`). Its volume is the distance's only: 127 × (5625 −
  2d²) / 5625, none from 53 units, d the listener's distance by
  `0x410cb0` (the larger of |x| and |z| plus a quarter of the smaller,
  then the same with |y|); its azimuth by `0x410d30`;
- the hit's smoke: when the car is traffic (`+0x200` bit 0x10) or the
  strength is over 30, and none is running, `+0x8D4` = |round(strength
  × 0.15)| (`0x5372d0`) and `+0x8D8` the point in the car's frame × 0.6
  (`0x4e0050`). `car_smoke_tick` (each step): `+0x8D4` counts up; a
  particle at the point back in the world, kind 6 (white) up to 16,
  kind 9 (`SP09`: black smoke) up to 64, then `0x4caea0`'s fire (kinds
  9 and 7 rising, a burst at speed 20 one time in two in the first 8
  steps of each 48, and seven times in eight a kind 8 at scale (step %
  48) / 48 inheriting half the velocity, `0x5403e8`, `0x5403f0`). A car
  whose data has `+0x380` bit 0x20 (`0x4396e0`) makes none. It stops
  when the car is on the ground (`+0x164` 0), upright (`+0x10C` ≥ 0.5)
  and its spin's x and z (`+0xE4`, `+0xEC`) are both under 0.2
  (`0x537310`).

So `Rain` 1:51's truck (traffic) smokes from its first hit over 5:
white for a few steps, then black, over the big kind-2 puffs and the
burst's streaks. Under Direct3D kind 7 starts at `7F7F7F7F` fading by
`7F003F7F` (`0x4cb670`, from the race's set-up: the bytes
`0x560f30`-`0x560f37`).

**On the Track page** (`carHitParticles`, `startCarSmoke`,
`carSmokeTick`, `carFire`, `carHitSoundSlot`): `aiCollide` hands back
each car a hit gave a strength, with the point (`AiHit`), without
changing what the hit does to the cars; the next particle step makes
the sparks, dust and smoke, and plays the sound at the camera's
distance and azimuth. The page's police and racers aren't traffic. Its
cars' spin is their yaw, roll and pitch rates about their own axes, put
into the world for the stop test; the status line names the last hit.
In `g2/crash-3.png` (Hometown in the weather, a racer and a police car
at a roadblock, strength 8) the burst's yellow streaks sit at the
contact and a fire burns on the racer; the page's cars hit softer than
`Rain`'s truck (up to strength 8 against the truck's head-on 35 mph),
so their dust puffs are smaller.

**That fire** (November 2026). The status line names only the last
hit, and a hit of 8 on a racer can't start the smoke: the code (`0x42ad15`
compares the strength's bits with 30.0's, `0x41f00000`, unless `+0x200`
has bit 0x10) needs traffic, and `car_kind_from_role` (`0x428700`) gives
police cars 0x20 and 2, racers 1 (and 8 for the computer's), traffic
alone 0x10; `car_pursuit_states` (`0x405740`) only toggles bit 2. The
page's racers and police aren't traffic, and in three runs of the same
roadblock (h2 `r1`-`r3`, dozens of hits each, strengths up to 21) no smoke
started. So the fire must have come from an earlier hit over 30 (not
seen again), but it should then have stopped once the car was down and still. It didn't
because the page ran the cars' effects from the particles' loop, paced
by the frame (up to 64 ticks a frame), while the cars moved at most 16
steps a frame: every tick of a frame saw the frame's last car, so a hit's
air tick (`+0x164`, one more for each hit over 5, `0x5373f4`) held the
stop test off for the whole frame. With every hit made smoke (a debug
build), one frame ran a smoke 20 ticks on one car state (`r4`); a frame
of a second turns it to fire. The page now runs `tickCarEffects` from
`moveCars` after each 64 Hz step's hits and moves, as `car_impacts_task`
runs in the cars' Sc64 list: the same debug build's smokes then end in
3-7 steps with the car's state changing each step (`r5`), and the
roadblock shows sparks and dust, no smoke (`h2/after6-2.png`).

On the Tracks page a fast hit at 28 units (Summit block 196) gives a
small orange cluster, then orange dots and short streaks spreading
sideways for about 0.4 s: more orange and shorter than the clip's, but
the camera was ten times as far and it wasn't the same event, so this
doesn't show a fault. Life, the wedge's width and fogging stay
unchecked.

**The flags word, read (November 2026).** The quad's `+0x6` bit 0 is
read by the polygon drawers the game picks for the 3dfx renderers
(`0x434870`: `polys_draw` `0x4332b0` for quads, `0x433530` for
triangles): set, THRASH state 0x68 (blend) goes to 1, source alpha and
one, so the quad is added scaled by its alpha; clear, 0, source alpha
over ([thrash.md](thrash.md)). The record's `+0x4` word picks the drawer
(the table at `0x5f0780`, through `0x434950`). So sparks are added, as
the Track page draws them; no longer inferred.

**Inferred, not followed:** that the picture is multiplied by the corners'
colour. (Which corner of the picture goes where is now from the list,
above; where the picture's corners sit is from the code, Sparks against
footage above.) That particles are fogged is from the smoke's quads in
`e1-t8s0`'s list (above); a spark's streak hasn't been in a list. The Track page also works the streak in the world rather than
on the screen, doesn't round sizes to pixels, and makes up the car (its
speed 40 or 18, impact 10, `+0x15c` 0, the contact half a unit in from
the wall's line and up from the road, the centre a unit further in).

### Rain and snow

The weather's falling rain or snow and its splashes on the screen,
read from nfs3.exe in November 2026 (`gdecomp`, `asm`, the static data
read from the exe). Reluva has it in `src/engines/nfs/particles.cpp`
(`Weather`) and draws it on the Track page. It isn't a particle kind:
it has its own pools, its own pass and its own pictures.

**Set-up** (`0x4dcdf0`, from the race set-up `0x4bc870`). The `.hrz`
loader (`0x478340`) reads the file's weather type into the byte
`0x8ca1e8`, the start and end slices and the fade into words at
`0x8ca1ea`-`0x8ca1ee`, and the four times into `0x8ca1f0`-`0x8ca1fc`,
which it makes running sums (`0x478992`-`0x4789c0`: stay on, then
fade off ends, off ends, fade on ends). The kind is type % 3 (0 none, 1
rain, 2 snow; `0x8ca22c`). Each kind's 0x34 bytes at `0x564290` + kind
× 0x34 (kind 0 is all zero):

| Offset | Meaning | Rain | Snow |
|---|---|---|---|
| +0x00 | the fall each update: (this / 10) × 2^-16 | 1.2 | 0.2 |
| +0x04 | a flake's half size at the camera, pixels | 3 | 6 |
| +0x08 | percent of 800 drops | 50 (400) | 100 (800) |
| +0x0c | the box's half width round the camera, and the farthest drawn | 25 | 20 |
| +0x10, +0x14 | the box above and below the camera | 15, 5 | 20, 5 |
| +0x18, +0x1c | the wind: heading in degrees, speed in tenths an update | 180, 3 | 0, 0 |
| +0x20 | the drops' colour, in each byte of ARGB | 126 | 200 |
| +0x24 | a splash's life, updates | 16 | 96 |
| +0x28 | a splash's half diagonal, of the view's scales | 0.2 | 0.3 |
| +0x2c | a splash's colour at birth, each byte | 128 | 128 |
| +0x30 | percent of 20 splashes | 100 | 100 |

The wind is (sin, 0, cos) of its heading in turns times its speed, so
the rain drifts 0.3 an update towards −z in the track's coordinates,
whatever the camera's heading. The drops start at random in the box
(`0x4eb230`'s numbers).

The weather type is 0 in every day and night file; the weather files
give rain (type 1) on every track but Country Woods and the Summit,
which snow (type 2). The stretch of road: everywhere in most;
Atlantica's day weather 340 to 240 (past the line) fading over 15
slices, Rocky Pass's night weather 200-1220, the Summit's night
weather 400-1290, Empire City's day weather 579-357 (fades 100, 100,
50). The times (durations in the file, 64 ticks a second): Hometown's
day weather, Atlantica's night weather and Country Woods' and
Aquatica's both 60 s on, 75 s fading off, 80 s off, 95 s fading back,
round again; Atlantica's day weather 60, 20, 20, 95 s; Rocky Pass,
Lost Canyons and Empire City by night shorter cycles (Rocky Pass by
day 40, 10, 6, 5 s); none, so always on, for Hometown by night,
Redrock Ridge, the Summit and Empire City by day.

**How many fall** (`0x4dece0`, per view, after the scene): all of
them, or by the stretch (`0x4dd3d0`, at the view's car's slice: none
outside it, fewer within the fade of its ends), then by the time since
the set-up (`0x4dd330`: all, fading linearly to none, none, fading
back; at the cycle's end the step becomes its new start), then halved
at Special Effects Low and quartered at Off (signed division).

**Moving** (`0x4dd5e0`, from the pass, so once a frame, but only when
the step, `0x7d3684`, is more than 1 past the last move: every other
64 Hz tick). The splashes' lives go down by one. Every drop of the 800
(those not falling too) moves by the velocity. Then for each falling
one: a snowflake not hidden drifts by (sin, cos) of the 16-bit angle
(i + step) × 256 (`0x4f2380`, `0x4f23f0`: 16.16 sine and cosine from a
1024-step table, interpolated) >> 4, × 2^-16, so in a circle of radius
about 2.5 over 4 s, each flake its own way. A drop out of the box
round the camera comes back in: past the side across x, on the other
side with y and z at random; past the side across z, y and z at random;
below, at the top with x and z at random; above, at the bottom. It is
then marked hidden if the camera is under cover (`0x420d60`: the
covered code 2 at the camera's slice or two on, by which side of the
slice's plane it is), and stays hidden until it wraps again out of
cover. `0x4dd510` (from the pass before the rain is drawn, not the
snow) doesn't touch the hidden marks: it clears byte +9 of each drop's
record (`0x8c0bf1`), the streak's tail mark, when the camera has gone
into or out of the car (camera mode 2-4 with `0x79c4a0` set, kept per
view at `0x8ca2b4`) or has just changed (`0x552ff4`, set by the camera
code and cleared once the view is drawn, `0x4213b0`). On a camera
change the pass also calls `0x4dd230` before moving the drops: unless
the camera is under cover, every drop back at random in the box with
its tail mark cleared; the splashes cleared either way.

**Drawing.** The pass sets the view up again with angle 0x78, 120°
(`0x4dbda0` → `0x4bed20` → `0x4dbce0`: half angles 60 and 48 whole
degrees, so x scaled by 184.75 and y by −216.1 at 640 × 480), draws,
and puts the normal view back (`0x4dbde0`, 90° or 75°). So the drops
are drawn nearer the middle of the screen than the scene's projection
would put them. Each falling drop not hidden is projected
(`0x4bf4c0`); then:
- 0 > w > −3 (just behind the camera): back at the top of the box,
  x and z at random;
- w ≤ 0 or w > the half width (25, 20): not drawn;
- off the screen (the projected vertex's clip flags): not drawn;
- w < 3: a splash is made if one of the 20 is free (`0x4dd930`, below)
  and the drop goes back to the top; the rain also plays a sound
  (`0x414f90`, bank 48 at random). With none free it is drawn;
- **rain** (`0x4de700`): a triangle from the streak's tail to the drop
  and two pixels to its right. The tail is kept on the screen: the
  drop's point when it was last not drawn, else halfway from the old
  tail to the drop each frame, so the streak is the screen movement of
  about the last two frames: longer at a lower frame rate;
- **snow** (`0x4de070`): a square from the drop's point, right and
  down, of side (6 − w × 6 / 20) × 2 pixels, at least 1: 12 at the
  camera, 1 at 20. Not in perspective: linear in the depth.

Both in the drops' colour (126 or 200 in every byte) on `DRP1` (rain,
8 × 32, a grey ramp 57-165) or `DRP2` (snow, 8 × 8, a soft dot up to
206) of `GameData/Render/pc/weather.fsh` (`0x4dcd20` loads `DRP%d`
and `SPL%d` with the type), with the polygon flag 1: added, scaled by
the alpha (above). The pictures are opaque 5:6:5, so a rain streak adds
about (126/255)² of the ramp, 14-40: faint; a flake up to 127.
The lists go to the card after the sorted scene (`0x4bc030` from
`0x4bc375`), unsorted and over everything, splashes after drops.

**Splashes** (`0x4dd930`, made only with `0x7d36a0` clear). The drop's
x and y over its depth by the view's rows (before the screen's
scales) are kept, so a splash stays on the screen where it was, like a
drop on the lens. Its life is the kind's less (random % (life / 4)):
13-16 updates for rain (0.4-0.5 s), 73-96 for snow (2.3-3 s); its size
random × the kind's, held to a half to all of it; its angle random.
Drawn (`0x4ddb40`, per view, skipped at Off, `0x4dedf0`) as a square
turned by its angle, corners (x + c, y + s), (x − s, y + c), (x − c,
y − s), (x + s, y − c) with c, s the cosine and sine × the size, then
scaled by the (120°) view's scales, at z 0.1 and rhw 0.9; colour 128 ×
life / life at birth in every byte; for rain the size shrinks the same
way. `SPL1` (rain) is a grey ring, `SPL2` (snow) a grey puff, both
added.

With the Direct3D renderer (flag 0x40) and the effects intensity
(`0x6fbc38`) under 1 the colours are dimmed as the lights' are (k =
min(round(f × 65536), 0xFFFF) >> 8, each byte × k >> 8): the drops'
(`0x4de700`, `0x4de070`) and the splashes' (`0x4ddb40`). The Track
page does this in its Direct3D look (Shift+Y, key I).

**The splash's sound** (`0x4de989`): each splash `0x4de700` makes
(rain only) calls `0x414f90`(view, −1): sound random & 3 of the race's
bank entry 4 (`rain.bnk`, `0x571154`), volume 90 + random % 30, and an
azimuth random % 0xFFFF (in split screen 0x3FFF or 0xC000 by the view),
in that order of random numbers, through `sound_start` (`0x4182b0`),
which scales the volume by the effects volume (`0x6fbbf4` × 55 >> 14,
as for every bank but its specials) and turns the azimuth into a pan
byte (stereo: 64 ahead, 127 a quarter turn right, 0 three quarters).
Splashes come only within 3 of the camera with a slot free, so the
sound is a patter of a few a second.

**Against footage** (November 2026, `Rain`, `Snow`, `Night`):
- `Rain` 0:10-0:22 (race 0:05-0:17): faint thin grey streaks, short
  and steep, over the sky and road, and now and then a pale grey ring
  about 23 game pixels in radius held on the screen for under half a
  second, lighter than what's behind it. Both as the code has them:
  the streaks added at a quarter of `DRP1`, the rings `SPL1` added. The
  ring's size fits the splash's (half diagonal 0.1-0.2 × 185-216).
- `Snow` 0:40 (race 0:35), 2:04 (race 1:59): small white dots of a few
  game pixels, more on the lower half of the screen, and faint grey
  puffs held on the screen for seconds, up to about 35 game pixels
  across: the flakes' squares (12 pixels at most) on `DRP2` and the
  `SPL2` splashes, as the code has them.
- `Snow` 2:09 (race 2:04), in a tunnel: no flakes fall under its
  roof, but a dozen grey splats made just before stay on the screen.
  Both as the code has them: drops coming back into the box under
  cover are hidden, and a snow splash lives 2.3-3 s whatever happens.
- `Rain`: the clip's 143 s are within Hometown's first cycle (60 s on
  from the set-up, then 75 s fading off), so from about race 1:00 fewer
  streaks should fall; the clip's streaks are too faint at its
  compression to count. Checked in the game instead (November 2026,
  `g2-runs/cyc`: `nfs-host`, Hometown in the weather, `0x6fd4cc` poked
  to 1 at `0x474ca0`, the player's car left at the line; `-watch
  0x4ded3d`, the count `0x4dd330` returns in eax, with `-watchread` of
  the step `0x7d3684`, the share drawn `0x564288`, the total `0x8ca234`
  and the times `0x8ca1f0`-`0x8ca1fc`; 529 samples to step 22295,
  348 s). The times are 3840, 8640, 13760 and 19840 steps (60, 135, 215
  and 310 s), the total 400, and the cycle starts at step 0 (the
  set-up): 400 to step 3795; 399 at 3844, 195 at 6294, 8 at 8542, 0 from
  8649 to 13738; 2 at 13777, 186 at 16575, 397 at 19794; 400 again from
  19829 on, the next cycle begun. Each is the code's (8640 − step) × 400
  / 4800 or 400 − (19840 − step) × 400 / 6080, to the drop. The share
  drawn (`0x4dd4c0`: the drops looked at and not hidden, × 100 / 400)
  follows it: 100%, 48%, 0%, back to 100%. So `Rain`'s race clock
  1:00-2:15 runs within the fade from 400 drops to none.
- `rain.bnk` (`bnk-info`): sounds 0-3, the splashes', are 0.01-0.13 s
  at 22050 Hz; 4 is a 1.3 s loop and 6 and 7 four seconds at 11025 Hz
  (`sound_start`'s specials, played at the full effects volume).
- `Night` 1:43-2:10 (the Summit by night in the weather, race 0:00-
  0:28): no snow falls; Summit's night weather file sets snow only on
  slices 400-1290, and the start is outside them.

**On the Track page** (`prepareWeather`, `drawWeather`): the variant's
weather set up when the sky is loaded, ticked with the particles at 64
a second round the camera (the camera's slice and cover standing for
the view's car's), drawn through the 120° screen after the particles
with no depth test, added, fogged by depth as the scene; each corner
is put back into the world on the line through its pixel at the
polygon's depth, so the page's own 90° projection lands it on that
pixel. Its frame rate under software Vulkan is a few frames a second,
so its streaks are longer than the clip's (the tail lags by frames).
The splashes play their sounds (`rain.bnk`, with the page's own random
numbers, so a random pan); a camera jump (N, B, R, Shift+C) is a camera
change (`Weather::cut`, `Weather::tails`; the page's camera is never in
a car); the Direct3D look dims the drops and splashes. The random
numbers are Reluva's.

