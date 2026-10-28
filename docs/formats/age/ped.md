# Pedestrians (`anim/`)

The people on the sidewalks: four models (`pedmodel_man`, `_manw`,
`_woman`, `_womanw`, the `w` ones dressed for rain), each a skinned mesh
on a skeleton of 19 bones, and 66 animations shared by the men and by the
women. All in `mm2core.ar`'s `anim/` folder, but the colours, which are
in the models' packages (`geometry/pedmodel_*.pkg`). Read by
`src/engines/age/ped_model.cpp`.

Worked out from the files (all but the animations are text) and by
drawing the result with Reluva's renderer until walkers walked.

## `pedmodel_<x>.skel`: the skeleton

```
NumBones 19
bone root {
	offset 0.000570 1.147210 -0.000000
	bone spine {
		offset 0.000000 0.050592 0.018812
		...
```

Each bone's joint is `offset` from its parent's. The bones in file order
(root, spine, neck, head, clavicle_r, shoulder_r, elbow_r, wrist_r, the
same on the left, pelvis, hip_l, knee_l, ankle_l, hip_r, knee_r,
ankle_r) are the order the animations and the model number them in.

## `pedanim_<x>.anim`: an animation

```
u32 0
u32 frames
u32 channels           60: 3 + 3 * 19
5 bytes                a float and a byte, not known
f32 value[frames][channels]
```

Each frame is the root's position, then each bone's turn about X, Y and
Z in radians, applied Z(Y(X)) (the order that made the walk look right;
most joints turn about one axis). The models face -Z: the root's
position goes down Z by 1.41 m over the man's 20-frame walk. The
animations' speed isn't in them; Reluva plays 20 frames a second, which
makes the walk 1.4 m/s and the run 4.8 m/s. `pedanim_manantrnch.anim` is
104 bytes and isn't an animation this reads; the four `*dive*.anim` files
have a frame or two.

## `pedmodel_<x>.csv`: the moves

```
# anim name,mma name,first frame,last frame,Y AXIS Offset,Y AXIS DISTANCE,X AXIS Offset,X AXIS DISTANCE,default next[
STAND,pedanim_manstand,1,30,0,0,0,0,STAND
WALK,pedanim_manwalk,1,20,0.281,1.409,0,0,WALK
WALK_LDIVE,pedanim_manw2dl,1,24,0,0,0,2.224,LDIVE_GROUNDL
...
```

A move's animation, its frames, how far it carries the walker forward
("Y") and sideways ("X", positive to the left), and the move that
follows. Reluva takes the distances from the animations' own root
positions instead.

## `pedmodel_<x>.mod`: the model

Text, two layouts. Both start:

```
version: 1.09
verts: 117
normals: 248
...
v	0.001060	-0.218585	0.036570     a position, in its bone's space
n	...                                   a normal
c	1 1 1 1                               a colour (one)
t1	0.542735	0.101010              texture coordinates
mtl Businessman1:SKIN {                  a material
	packets:	1
	primitives:	30
	diffuse:	0.686275 0.568627 0.439216
	...
}
```

and end with `mtxv` (how many positions each bone has: they come in bone
order) and `mtxn` (the same for normals). Between, the corners and
triangles:

- The men's: a `packet <corners> <triangles> <bones> { ... }` for each
  of a material's packets, its corners as `adj <position> <normal>
  <colour> <tex> <?> <bone>` (the bone is an index into the packet's
  `mtx` line), its triangles as `tri a b c` over its corners.
- The women's: every corner as `adj <position> <normal> <colour> <tex>
  <?>`, then every triangle over them; each material takes the next of
  them as its `adjuncts:` and `primitives:` counts say.

Reluva takes each corner's bone from `mtxv` in both.

## The colours

Each model's package has one mesh with a group for each material and 48
paint jobs, which is where the people's different clothes and skins come
from. The groups are the materials in another order, and the women's
have their corners merged (241 against the model's 696), so Reluva
matches each material to the group with as many triangles, the nearest
colour in the first paint job deciding between those. The women's
`.remap` file (`17`, then `1 3 2 5 4 ...`) looks like that matching
written down, but its numbers don't fit it as read so far; it isn't used.

`pedmodel_*.rays` and `.shaders`, and `pedmodel_wolf.skel` (a
four-legged skeleton), aren't read.
