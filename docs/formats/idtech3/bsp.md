# id Tech 3: compiled maps (`maps/*.bsp`)

Status: **all lumps read and cross-checked** (`src/engines/idtech3/bsp.cpp`);
the light grid is kept raw. Checked on maps built by the tests. **Not yet
checked against Elite Force's retail maps.** Elite Force's tools are
derived from Quake III's, so its maps are expected to be the same version
46, but this needs confirming: the reader fails with a clear message if
the version or any record size differs.

Sources: the GPL Quake III Arena source (`code/qcommon/qfiles.h`, the map
compiler and renderer) and the community's "Unofficial Quake 3 Map Specs".

All values are little-endian. Integers are 32-bit signed, floats IEEE
single precision.

## Header

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IBSP` |
| 4 | i32 | version, 46 |
| 8 | 17 × (i32 offset, i32 length) | the lumps, in the order below |

## Lumps

| # | Lump | Record size | Record |
|---|---|---|---|
| 0 | entities | text | `{ "key" "value" ... }` blocks, see [text.md](text.md); usually NUL-terminated |
| 1 | shaders | 72 | char[64] name, i32 surface flags, i32 content flags |
| 2 | planes | 16 | f32[3] normal, f32 distance |
| 3 | nodes | 36 | i32 plane, i32[2] children (negative = −(leaf+1)), i32[3] mins, i32[3] maxs |
| 4 | leafs | 48 | i32 cluster (−1 = outside), i32 area, i32[3] mins, i32[3] maxs, i32 first/count leaf surface, i32 first/count leaf brush |
| 5 | leaf surfaces | 4 | i32 surface |
| 6 | leaf brushes | 4 | i32 brush |
| 7 | models | 40 | f32[3] mins, f32[3] maxs, i32 first/count surface, i32 first/count brush. Model 0 is the world; entities use the others as `"model" "*n"` |
| 8 | brushes | 12 | i32 first side, i32 side count, i32 shader |
| 9 | brush sides | 8 | i32 plane, i32 shader |
| 10 | vertices | 44 | f32[3] position, f32[2] texture st, f32[2] lightmap st, f32[3] normal, u8[4] RGBA colour |
| 11 | indices | 4 | i32, relative to the surface's first vertex |
| 12 | fogs | 72 | char[64] shader, i32 brush, i32 visible side (−1 = none) |
| 13 | surfaces | 104 | see below |
| 14 | lightmaps | 49152 | 128×128 RGB |
| 15 | light grid | 8 | u8[3] ambient, u8[3] directed colour, u8[2] direction (latitude, longitude); the grid's size comes from the world model's bounds |
| 16 | visibility | — | i32 cluster count, i32 bytes per cluster, then one bit per cluster pair: bit `to` of row `from` set when `to` can be seen from `from` |

### Surfaces

| Offset | Type | Meaning |
|---|---|---|
| 0 | i32 | shader |
| 4 | i32 | fog (−1 = none) |
| 8 | i32 | type: 1 polygon, 2 patch, 3 triangle soup (models), 4 flare |
| 12 | i32 × 2 | first vertex, vertex count |
| 20 | i32 × 2 | first index, index count |
| 28 | i32 | lightmap (negative = none) |
| 32 | i32 × 2 | position in the lightmap |
| 40 | i32 × 2 | size in the lightmap |
| 48 | f32[3] | lightmap origin in the world |
| 60 | f32[3][3] | lightmap s and t vectors and the surface normal; for patches, [0] and [1] are the patch's bounds instead |
| 96 | i32 × 2 | patch width and height in control points |

Patches are grids of quadratic Bézier patches: width × height control
points (both odd), each 3×3 block one patch, sharing edges. The engine
tessellates them; the file holds only the control points.

## What the reader checks

Every lump lies inside the file and holds whole records; every index into
another lump is in range (node planes and children, leaf ranges, model and
brush ranges, surface shaders, vertices and indices); a patch's size
matches its vertex count; vis data covers every cluster. Errors name the
record, the field and its value, e.g. `BSP surface 0: index 2 is vertex 3
(of 3)`. The rest of the engine relies on this and doesn't check again.

### Tolerated, as the original engine does

Some Elite Force maps hold values out of range that the original engine
accepts. Reluva corrects them the same way and counts them (`Bsp::fixes`,
shown in the map viewer and by `reluva-tool bsp-check`):

| Value | Original behaviour | Reluva sets |
|---|---|---|
| surface lightmap ≥ lightmap count | lit by its vertex colours | lightmap −1 |
| surface fog that doesn't exist | not checked | fog −1 |
| flare (or bad surface) with vertices or indices | never used | no vertices or indices |
| unknown surface type | (ioquake3 refuses the map) | type 0, never drawn |

The original's behaviour was read from the GPL Quake III Arena renderer
(`tr_bsp.c`, `tr_shader.c`). The last row is more lenient than Quake III,
since Elite Force's engine may differ; maps that need it show up in
`bsp-check`'s counts.
