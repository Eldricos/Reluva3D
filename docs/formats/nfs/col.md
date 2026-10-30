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
(Hometown's 141-key loop is in both). Type 1, a still object with a
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
      are added to car `+0x920` and `+0x924` (`0x42a9bd`), its tilts
      by the look of it. Not side-specific: other hit types do it too.
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
      In `Gen.bnk` 29 is 0.44 s, 30 0.53 s and duller, 32 0.82 s,
      33 1.18 s and brighter (16-bit PCM but 33, EA-XA). Not listened
      to, so not named. `Gen3d.bnk` has the same sounds two slots up
      and nothing in 29 and 30; whether the game allows for that
      wasn't followed.

    Then, for any wall hit (`0x42b11a`), by car `+0xb8` (its speed by
    the look of it):
    - 22 or more, and car `+0x15c` (unknown) under 3: kind-3 codes get
      a burst of kind-3 particles (`0x4cac20`; 30 of them, fewer below
      speed 40, thrown with the car's velocity ± 3) and a second sound,
      material 18: slot 32, 34, 35 or 37 at random (`0x4149ac`); kind-2
      codes get one more kind-2 particle. With `+0x15c` 3 or more,
      every code gets a kind-2 particle;
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
  | 3 | dust | material 16 (slot 30) | 35: Summit 26, Aquatica 6, Country Woods 3 | unknown |
  | 4 | sparks | material 8 | 13,644: every track, most of the city and canyon ones | rock, concrete, buildings |
  | 5 | dust | material 17 (slot 29) | 6,013: Country Woods (nearly all), Summit, Rocky Pass, Hometown | grass, dirt or snow verges |

  The materials come from the Track page (walls, key G, October 2026):
  Rocky Pass block 88, a guard rail on the left (code 1) and a rock
  face on the right (4); Empire City block 4, a railing on the right
  (1) and a building on the left (4); Hometown block 8, grass banks
  with wooden fences (5); Country Woods block 40, snow banks (5), though
  a brick wall stands there too. Code 3 wasn't looked at.

This is what the game drives on and keeps cars within. It will matter
for driving, AI and the camera. Reluva reads it, and uses it for the
walls guide and the wall hits' particles (below) so far.

### Particles

The wall hits' sparks, dust and smoke, read from nfs3.exe in October
2026, from making to drawing. Reluva has them in
`src/engines/nfs/particles.cpp` and shows them on the Track page (key P
at the wall nearest the line of sight; Shift+P a fast hit). The ticks
below are the game's, 64 a second (`0x7d3684`); a particle's life is
counted every other one.

**Templates.** A kind's 128 bytes at `0x560b64` + kind × 128 (static
data; 14 kinds, those of the wall hits here). What the code reads:

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
when the Horizon option (`0x6fbc30`) is Off, or when it is Low on
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
  (B, A), (A, −B), (−B, −A) from the centre, A = sin(angle) × half,
  B = cos(angle) × half (`0x4cc235`).
- With renderer flag bit 6 (`0x7a3a58`) set and the effects intensity
  under 1, kinds with `+0x58` bit 0 are dimmed as the lights are
  (`0x4cbf2f`; [frd.md](frd.md), Lights).

**Pictures** (`0x4cb590`): `SNnn` of `GameData/Render/pc/sfx.fsh` for
kind nn, `SPnn` when the night flag (`0x6fd4c8`) is set. `SN03` (sparks)
is 8 × 8, opaque, yellow; `SN02` (dust) a dark brown puff and `SN06`
(smoke) a grey one, 32 × 32 with alpha.

**Inferred, not followed:** that flags bit 0 means the quad is added
rather than blended (the lights' glows set it too, `0x49155f`, and the
Track page adds those); that the picture is multiplied by the corners'
colour; which corner of the picture goes where; whether particles are
fogged. The Track page also works the streak in the world rather than
on the screen, doesn't round sizes to pixels, and makes up the car (its
speed 40 or 18, impact 10, `+0x15c` 0, the contact half a unit in from
the wall's line and up from the road, the centre a unit further in).

