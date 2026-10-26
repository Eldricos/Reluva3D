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
| 12 | u32 | unknown (`0x550FF8` along Hometown's start) |
| 16 | 3 × s8, u8 | normal (÷ 127), then an unknown byte |
| 20 | 3 × s8, u8 | forward |
| 24 | 3 × s8, u8 | right |
| 28 | s32 | the left wall's distance from the point along −right, 16.16 |
| 32 | s32 | the right wall's distance along right, 16.16 |

This is what the game drives on and keeps cars within. It will matter
for driving, AI and the camera. Reluva reads it, but uses it only for
the walls guide so far.
