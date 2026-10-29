# LEGO Alpha Team: `.FIN` models

Source: the 147 `.FIN` files in the English retail `Fin.pac` (October 2026).
This is how the layout was worked out:

- **Splitting the files into objects.** The objects were found by their
  headers: a class name, then a value used once as an id and linked to
  from elsewhere. Every candidate for the 27 class names in the table
  below behaves that way; the other `Ni…`/`DD…` strings are object names.
- **Each class's fields.** These were found by comparing many objects of
  the class and decoding a few by hand: the 4-object `OG9997.FIN` (a box)
  first, then larger ones.
- **The rule for the field before the collision volume.** It came from
  where 1 to 5 fields failed to fit.
- **Testing each hypothesis.** Each one was tested by whether every object
  of the class then ended exactly where the next began. All 21 classes that
  are read now do so for every object in every file. `reluva-tool fin-check`
  repeats the check, and also checks that every link points at an object of
  the right kind and that every image named is in the archives.

The NifTools project's `nif.xml` (GPL-3), which describes later NetImmerse
and Gamebryo files and some of these early versions, was used for the
meaning of fields once their place was found: the names of the
bounding-volume types and of the alpha blend factors. Where it disagrees
with these files (a capsule is 28 bytes here, not 32, for example), the
files win.

`src/engines/netimmerse/fin.{h,cpp}` reads them.

## The file

- The line `Dweezil 23` and a newline, where NetImmerse's own files have
  their own header text.
- Then the objects, one after another, each:
  - its class name: a 32-bit length, then the characters;
  - a 32-bit id: the object's address in memory when the file was saved,
    unique within the file. Links between objects are these ids, and 0
    means none;
  - its fields, with no padding and no size, so a reader has to know
    every class to find the next one.
- The objects with no parent are each preceded by the string
  `Top Level Object` (length-prefixed like a class name). These are the
  model's roots, usually one.
- The file ends with the string `End Of File`.

All numbers are little-endian; floats are 32-bit IEEE.

"A pointer" below means a 32-bit word that was a memory address when the
file was saved: 0 means the data after it is left out, anything else that
it follows.

## Shared fields

**NiObjectNET** (every class read except `DDActorSharedData`):
- the name (a length and characters, often empty);
- the number of extra-data objects, then each written in place: its class
  name and its fields. Two kinds are seen:
  - `Ni3dsPropAnimExtraData`, on materials: a number and a link to its
    animator;
  - `TexturePropExtraData`, on textures: a number, a link to its
    `NiFlipTextures`, and a number;
- a byte of flags. Bit 0 hides the object and everything under it. It's
  set on 266 nodes, and the names of every one of them are effects shown
  on events: "Effect group" sparks, explosions, coronas, "lights on", a
  stunned agent's "stars". It's clear everywhere else. `NiImage` has 1
  here, meaning something else for an image.

**NiAVObject** (nodes and shapes), after those:

| Field | Size |
|---|---|
| translation | 3 floats |
| rotation, row by row; applied to column vectors | 9 floats |
| scale | float |
| velocity | 3 floats |
| number of properties, then their links | 4 + 4 each |
| a number, 1 to 5; meaning unknown | 4 |
| a pointer to the collision volume; 0: none | 4 |
| the collision volume, if there is one | below |

An object's place in its parent is `translation + rotation × (scale ×
point)`. This was checked by drawing the characters, whose parts only fit
together this way.

**Collision volumes:** a 32-bit type, then:

| Type | Name (nif.xml) | Fields |
|---|---|---|
| 0 | sphere | centre, radius (4 floats), a byte |
| 1 | box | centre, three axes, three extents (15 floats), a byte |
| 2 | capsule | 7 floats, a byte |
| 4 | union | a count, then that many volumes |
| 5 | half-space | a plane (4 floats) |
| 6 | (not in nif.xml) | a count, then that many volumes |

The levels' `DDEnv` roots hold most of them: unions of up to 80 boxes and
capsules, which are the level's walls and floors.

## Nodes

**NiNode:** NiAVObject's fields, then two numbers and a byte (always 2, 0
and 1 or 0; meaning unknown), the children (a count and links), and the
effects (a count and links; always 0 here).

What each subclass adds after NiNode's fields:

| Class | Count | Adds |
|---|---|---|
| `NiBillboardNode` | 909 | a number (1): the billboard mode. The viewer turns the node's +Z axis to the camera, which is how its quads lie: they're flat in X and Y |
| `NiLODNode` | 64 | 5 bytes, the number of levels (one per child), 20 bytes for each level (its distances), a byte |
| `Ni3dsAnimationNode`, `Ni3dsBone` | 860, 213 | 48 bytes of animation settings: 1, 1, 2, then floats 0, 1, 0 and the scene's range in ticks (16,000 to 356,800 in Evil Ogel), then 16 bytes of 0. The keys are in `DDActorSharedData` (below) |
| `DDUnit` | 93 | a link, *guess*: to the model's `DDActorSharedData` |
| `DDEnv` | 54 | a link, as `DDUnit`'s; the root of every level |

A level's sky is under a node named `Exterior` (holding `Backdrop`).

The characters have, on each arm and leg bone, a node named `LOD_RArm1`,
`LOD_LLeg2` and so on, over a billboard of one flat quad. Their textures
are `LodBlack.tga`, `LodRed.tga` and the like: plain colours. *Guess*:
stand-ins for a character far away, which the game switches to itself, as
nothing in the file marks them. The viewer leaves them out with the hidden
effects.

## Shapes

**NiTriShape** (13,226) and **NiEnvMappedTriShape** (13, which adds
nothing): NiAVObject's fields, then the geometry, in this order:

| Field | |
|---|---|
| number of vertices | 16 bits |
| a pointer, then the vertices | 3 floats each |
| a pointer, then the normals | 3 floats each |
| the bounding sphere: centre and radius | 4 floats |
| number of triangles | 16 bits |
| number of texture coordinate sets | 16 bits |
| a pointer, then each set's coordinates | 3 floats (u, v, w) per vertex |
| a pointer, then the vertex colours | 4 floats (r, g, b, a in 0..1) per vertex |
| a pointer, then a plane per triangle | 4 floats each |
| the triangles | three 16-bit indices each |

Every index is below the number of vertices, in every file. A level's
shapes have two coordinate sets: the first for the texture, which tiles,
and the second, in 0..1, for the lightmap. Texture coordinates are used
as they are: v runs down the image, as the TGA files are decoded (the
minifigures' faces show the right way up).

**Ni3dsSkin** (82, in the 15 character models): a shape whose vertices
follow bones, under an "auto geom parent" node. A shape's fields, then:
- two bytes (1 and 1 in every skin);
- for each vertex, the number of its influences (16 bits), then each
  influence:
  - its weight (a float);
  - the vertex's position in the bone's space (3 floats);
  - the bone (a link to an `Ni3dsBone`).

Every vertex of every skin has exactly one influence, of weight 1 (2,960
vertices), so the skinning is rigid. A vertex goes where its bone's
placement puts its offset. The shape's own vertices are in the modelled
pose, with the arms out, while the bones are where the file puts them; the
two only agree for a vertex near its bone. Drawn this way, all 15
characters are whole, in the pose of their first keys; animated by their
tracks (below), they walk, turn and wave.

**DDCorona** (215): a glow. A shape's fields up to the vertex colours
(5 vertices: a square and its centre), then a link and 9 floats of its own
instead of planes and triangles.

## Properties

Each is NiObjectNET's fields, then:

| Class | Count | Fields |
|---|---|---|
| `NiMaterialProperty` | 3,728 | ambient, diffuse, specular, emissive (3 floats each), glossiness, alpha |
| `NiAlphaProperty` | 2,278 | a byte (blending on), source and destination factors (6 and 7 in every file: source alpha and one minus source alpha) |
| `NiTextureProperty` | 2,068 | a number (0), the number of images, their links: one, or several frames for a `NiFlipTextures` to cycle |
| `NiMultiTextureProperty` | 582 | a count (1), a link to an image (a lightmap from `LMap.pac`), 6 numbers (1, 0, 1, 0, 1, 1) |
| `NiTextureModeProperty` | 8,967 | 3 numbers (3, 0 to 2, 0 to 3) |
| `NiVertexColorProperty` | 285 | a number (1) |
| `NiZBufferProperty` | 215 | two bytes (0 and 0; *guess*: no depth test or write) |
| `NiSpecularProperty` | 61 | a byte (1) |
| `NiShadeProperty` | 5 | nothing more |

The viewer applies a property to the object it's on and everything under
it, unless something nearer has its own of the same class. That's how
NetImmerse's scene graph is generally described, and the models look
right drawn that way.

**NiImage** (3,372): NiObjectNET's fields (its byte is 1), the file name (a
length and characters, e.g. `CorrBlk.tga`) and a number (7). No pixels
are stored in the models. All 1,260 names are in `Map.pac`, `LMap.pac` or
`Etc.pac`.

## Animation

Every class is now read; 116 of the 122 morph shapes read to exactly their
end (below). `reluva-tool fin-check` also checks the actors' tracks against
the 3ds nodes (below). The viewer plays all of it.

**How it was worked out.** Each class's objects were split out and dumped
as numbers by scratch scripts, and fields were found by comparing many
objects. Key sizes were then fixed by requiring every object to parse to
exactly its end. That is how the 46-byte Euler prefix and the scale keys'
16 bytes were found, by trying each size across all 147 files; one size
fits every file and no other does. Meanings were checked against what can
be measured:

- the flip-books' frame counts against their textures' image counts;
- the tracks' first keys against the nodes' saved placements;
- the morph targets against their shapes' vertices.

Nothing came from the game's code or any other source.

**Times** are 3ds Max ticks, 4,800 a second (160 a frame at 30 frames a
second). An actor's clip "at_rest" from 3.333 s starts at the 16,000-tick
key.

### DDActorSharedData (147, one per model)

The game's own data for a model. It doesn't start with NiObjectNET's
fields. In order:

- four strings: the model's code (`B0001`), one always empty, its name
  (`Evil Ogel`), and a texture set (`tl0019` in the levels, else empty);
- a 16-bit number, then two floats (0.7 and 202,500 for the characters,
  1 and 0 for the rest; meaning unknown);
- the clips: a count, then each:
  - its name (`at_rest`, `turn right`, `win`, `anim1`...);
  - start and end, in seconds;
  - a byte: 1 if a sound follows. Then the sound's name (`u0179_on`), 10
    bytes, four floats (1, 1, 600, 300: *guess*, volume, pitch and two
    distances) and a byte;
  - three lists of 16-bit numbers (a count, then the numbers; meaning
    unknown);
- the tracks: a count, then four for each:
  - **rotation**: a key count; if it's not 0, a key type, then the keys;
  - **position**, the same;
  - **scale**, the same;
  - **visibility**: a key count, then keys of 5 bytes (a time and a byte,
    0 hidden), with no type.
- the footprint: a count, then that many points (3 floats), a polygon at
  floor height: the square a part fills on the board.

Each key starts with its time; the types add tangents or other data after.
Offsets are in floats from the key's start:

| Track | Type 1 (straight) | Type 2 (Bezier) | Type 3 (TCB) |
|---|---|---|---|
| rotation | 44 bytes: time, angle, axis (3), quaternion (w, x, y, z, at 5), two floats | 64: as type 1, then a control quaternion (at 11) and its angle | 96: as type 1, tension, continuity, bias, then control quaternions in (at 14) and out (at 18), and their angles |
| position | 16: time, x, y, z | 64: time, value, tangent in (at 4), tangent out (at 7), a (at 10), b (at 13) | 76: time, value, tension, continuity, bias, tangent in (at 7), tangent out (at 10), a (at 13), b (at 16) |
| scale | 8: time, scale | 16: time, value, tangent in, tangent out | |

Rotation **type 4** is Euler angles. Its count is 1, then 46 bytes, then a
float track for each of X, Y and Z: a key count, then (if not 0) a type
and the keys. Type 1 keys are 8 bytes (time, angle in radians), type 2 are
16 (time, angle, tangents in and out), type 3 are 20 (no file uses them).

The quaternions are absolute: each key's matrix, by the usual formula for
column vectors, equals what the angle and axis next to it give.

**Between keys** (u running 0 to 1 from one key to the next):

- a curved **position** is `value + u (out + u (a + u b))`, its key's own
  cubic. The tangents are already scaled to the interval either side: they
  differ by the ratio of the intervals. `a` and `b` are the Hermite
  polynomial's coefficients. In 719 of 737 Bezier segments and 2,900 of
  2,957 TCB ones, a = 3d − 2 out − in' and b = −2d + out + in', where d is
  the step to the next key and in' its tangent in. The rest are jumps
  whose stored a and b weren't recomputed; the game uses the stored
  ones, and so does Reluva;
- a curved **rotation** is squad:
  `slerp(slerp(q, q', u), slerp(out, in', u), 2u(1 − u))`, out being the
  key's control quaternion and in' the next's. None of the three slerps
  takes the shorter way round. 91% of the type 2 controls are exactly
  Shoemake's `q exp(−(log(q⁻¹q₊) + log(q⁻¹q₋)) / 4)`; the rest sit where
  intervals change or the track jumps;
- a curved **number** (a scale, an Euler angle) is Hermite's, with the
  key's tangent out and the next key's tangent in;
- Euler angles make the rotation **Z × Y × X** (applied to column
  vectors: X first);
- straight keys are blended linearly, rotations by slerp.

**Checked against the game.** The retail game was run in Reluva on
Mission One (and M2020) with its solved puzzle. Its nodes' local
transforms were read from its memory each frame: in NetImmerse's node
objects, the translation, rotation matrix and scale follow 40 bytes after
the pointer to the node's name. Each frame was fitted with the time that
best matches one bone, and the others compared at that time:

- Dash's bones (squad rotations): within 3 × 10⁻⁶ of squad over 60
  frames. Straight slerp was up to 0.14 off. Taking the shorter way round
  between the control quaternions was up to 1.3 off.
- Evil Ogel's hose (Bezier positions): within 5 × 10⁻⁶ over 700 frames,
  against 0.74 for straight lines in the 11 frames where they differ.
- The Hand Cart Worker's head (three Euler angles): Z × Y × X within
  5 × 10⁻⁷. The other orders were 1.7 × 10⁻⁴ to 10⁻² off. Hermite and
  straight angle curves fitted equally there, so the number curves are
  taken from the positions' scheme rather than seen apart.

**Which node a track drives:** the 3ds nodes (`Ni3dsAnimationNode`,
`Ni3dsBone`) in the order a depth-first walk from the roots meets them, a
node before its children, children in order. For all 102 models with
tracks there are exactly as many tracks as 3ds nodes. 1,027 of the 1,035
tracks' first keys equal the node's saved translation or rotation: the
files were saved in the pose at the first key. Track 1 of Evil Ogel
(B0001) is `-hips`, track 2 `-hiphang`, then the right leg, the left, the
spine, and the arms.

The levels' actors have clips (`anim1`...) but few or no tracks; their
animation is in the morph shapes and material animators below.

### NiFlipTextures (282)

NiObjectNET's fields, where the byte is 0, 1 or 2 (*guess*: the cycle,
loop, reverse or clamp). Then three bytes, the frequency, the phase (in
ticks), the cycle's length (in ticks) and a link to the
`NiTextureProperty` whose images it cycles.

In every file, length × frequency ÷ 160 is exactly the property's number
of images, from 2 to 21. So each image lasts 160 ticks of the
controller's own time (the clock times the frequency), which is the same
as length ÷ images ticks of the clock. The phase's units and the cycle
byte's meaning aren't confirmed; the game's current image wasn't found in
its memory.

### Ni3dsColorAnimator (439), Ni3dsAlphaAnimator (393)

NiObjectNET's fields, then seven bytes (0, 0, 0, 1, 1, 0, 1). The
controller follows: the cycle (2, or 0 in four), a number (0), then
frequency, phase, start and stop. Then a link to the
`NiMaterialProperty` it animates; the material links back through its
`Ni3dsPropAnimExtraData`.

A colour animator then has a channel: 3 (248 of them), 1 (99) or 0 (92).
0 is ambient and 3 emissive. The first key of each channel 0 animator
equals its material's ambient colour (all 92), and of each channel 3
animator its emissive colour (all 248). 1 is diffuse, by NifTools'
numbering of the same field (0 ambient, 1 diffuse, 2 specular, 3
emissive), which those agree with. Both kinds then have a key count, a
type and the keys:

- alpha: type 1 is 8 bytes (time, alpha); type 2 is 16 (with two
  tangents);
- colour: type 1 is 16 (time, r, g, b); type 2 is 64 (with two tangents
  and 24 bytes more).

### Ni3dsMorphShape (122)

A shape's fields, then, in the 116 that read exactly:

- a controller: two numbers (1, then 0x01000001), the cycle (2), a number,
  frequency, phase, start and stop;
- two bytes;
- the number of targets (6 in most), a number, four floats;
- the keys: a count, then each:
  - its time;
  - six floats (*guess*: tension, continuity, bias and the like);
  - the number of targets again;
  - the weights, the incoming tangents, then the outgoing tangents, one of
    each per target;
- each target's vertices (3 floats each).

The first target is the shape's own vertices. The others are offsets from
them. So the shape at a time is the first target plus each other times its
weight. In E3020 the weights step one-hot through the targets, swaying
the seaweed. Six, in three files, have a different number before the
floats and extra bytes after the keys. They are drawn as modelled.

### NiLight (110, in the levels)

NiAVObject's fields, then:

- 12 bytes (0);
- a direction: (0, 0, −1) or none;
- a byte;
- three floats: for a spot, 22.5 (*guess*: its angle in degrees);
- the ambient, diffuse and specular colours (diffuse and specular always
  equal);
- two floats: 12,800 to 179,200 and 0, or 1 and 0.5 (*guess*: ranges);
- a byte;
- the kind: 1 for the spots shining down −Z, 2 for the omni lights;
- seven bytes.

The levels are lit by their lightmaps, so the viewer only marks where the
lights are (G). The game never hands Direct3D a light (`SetLight`) in
Mission One, so the fields' meaning couldn't be read from what it does
with them.

### DDCorona: glows

A glow's shape is a 2 × 2 square in X and Z and its centre, with white
vertex colours; its image is its own or inherited texture. The viewer
draws it facing the camera, added to what's behind it.
