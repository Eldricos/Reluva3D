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
    neither (`0x430c2e`). The impact code (`0x42ac40`) uses it for two
    tables of 8: `0x553740`, the kind of effect spawned at the contact
    point (`0x4cae00`; codes 1, 2, 4: kind 3, which also gets
    `0x4cac20` at low speed; 3, 5, 7: kind 2; 0, 6: none), and
    `0x553760` (8, 8, 8, 16, 8, 17, 16, 16), loaded at `0x42aea0` but
    used nowhere found. The kinds are entries of a 128-byte effect
    table (`0x560b64`); what each looks like isn't known.

  How 1, 3, 4 and 5 differ beyond the effect kind isn't known.

This is what the game drives on and keeps cars within. It will matter
for driving, AI and the camera. Reluva reads it, but uses it only for
the walls guide so far.
