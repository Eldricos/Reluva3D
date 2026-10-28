# .bnd collision bounds

Models' collision shapes, `bound/<model>_BOUND.bnd` in `mm2core.ar` (525
files: every car, props, landmarks; building fronts have none). Read by
`src/engines/age/bound.cpp`. The `.bbnd` files (324) look like binary
forms of the same and `.ter` (184) like terrain grids for them; neither
is read yet.

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
  car's collisions), an effect and a sound (not used yet).
- `quad a b c d m` and `tri a b c m`: polygons by vertex number, then
  their material's number.

Reluva checks every polygon's vertices exist. 17,515 vertices, 8,259
quads and 4,958 triangles in all.
