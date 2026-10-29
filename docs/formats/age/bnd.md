# .bnd collision bounds (and .bbnd, .ter)

Models' collision shapes, `bound/<model>_BOUND.bnd` in `mm2core.ar` (525
files: every car, props, landmarks; building fronts have none). Read by
`src/engines/age/bound.cpp`. `.bbnd` (324) is the same in binary, read
where there's no `.bnd`; `.ter` (184) is a grid over a bound, not read.

## How this was worked out

They're text. Every line of all 525 starts with one of `version:`,
`verts:`, `materials:`, `edges:`, `polys:`, `v`, `mtl`, the material's
`elasticity:`, `friction:`, `effect:` and `sound:`, a closing `}`,
`quad` or `tri`; all are version 1.01. The car's (`vpbug_BOUND.bnd`) is
16 vertices making a hull of 14 quads round the Beetle's body.

## Layout

```
version: 1.01
verts: 16
materials: 1
edges: 0
polys: 14

v	0.942760	0.244950	-1.979500
...
mtl default {
	elasticity: 0.100000
	friction: 0.500000
	effect: none
	sound: 0
}

quad 0  1  2  3  0
tri 4 5 6 0
```

- `v x y z`: a vertex, in the model's coordinates.
- `mtl <name> { ... }`: a material: elasticity and friction (used by the
  car's collisions), an effect and a sound. All 635 materials in the 525
  files have elasticity 0.1, friction 0.5 and effect `none`, and sound 0
  or `none`: only their names (`default`, `grass`, `cobblestone`,
  `water`, `deepwater`, `sand`, `mud`) say anything, and Reluva takes the
  city material of that name ([age-driving.md](../../engines/age-driving.md)).
- `quad a b c d m` and `tri a b c m`: polygons by vertex number, then
  their material's number.

Reluva checks every polygon's vertices exist. 17,515 vertices, 8,259
quads and 4,958 triangles in all.

## `.bbnd`

The binary form, little-endian. Each of the 324 has a `.bnd` beside it,
and read both ways all 324 are the same: vertices, materials (names,
elasticity, friction, effect and sound) and polygons.

| Bytes | Contents |
|---|---|
| 1 | version, 1 |
| 4 × 3 | counts: vertices, materials, polygons |
| 12 × vertices | x, y, z (floats) |
| 104 × materials | name (32, zero-padded), elasticity, friction (floats), effect (32), sound (32) |
| 10 × polygons | four vertex numbers (u16), then the material's (u16); a fourth vertex of 0 makes it a triangle |

There's no edge count; the `.bnd`s' edges are all 0 anyway.
`loadBound` takes `bound/<model>_BOUND.bnd`, else the `.bbnd`.

## `.ter`

Beside 184 of the bounds, for the larger models (landmarks, the cities'
bridges and ramps). Not read; what's known:

| Bytes | Contents |
|---|---|
| 4 | a float, 1.1 |
| 4 | the bound's polygon count (it matches every time) |
| 4 | a second count |
| 1 | 0 |
| 4 × 3 | the bound's size along x, y and z |
| 4 × 5 | a grid: cells across x (the size / 10, rounded up), 1, cells across z (likewise), the cells, and a count of references |

then three floats, the cells per metre along x, y and z (5 cells over
the Imax's 46.5 m: 0.1075, as stored), the bound's box (lowest corner,
highest), and a u16 for each cell, rising from 0 (starts in a list).
What follows doesn't read as lists of the bound's polygons: checked over
all 184, the numbers listed for a cell name polygons that overlap it only
22% of the time, so they're likely edges (the second count). It's a
lookup grid for colliding, which Reluva's own 8 m grid does without.
