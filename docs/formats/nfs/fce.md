# Need for Speed III: `.fce` models

Status: **implemented** (`src/engines/nfs/fce.cpp`), and checked on all
56 retail models:

- the cars, traffic and police cars (`car.fce` in each `car.viv`);
- the front end's track models (`go*.fce` in `trkgo.viv`);
- the cone and `cop0`-`cop4` in `GameData/Render/pc`.

Sources and checks:

- **Field order.** The order of the fields follows the NFS modding
  community's descriptions of NFS III's FCE (as used by tools such as
  FCEFinish).
- **Layout.** Every offset and size below was checked on the retail models
  with survey scripts, October 2026:
  - each part's counts sum to the model's;
  - each part's first vertex and triangle are the running totals;
  - every triangle index falls inside its part;
  - each table's size accounts for the space up to the next.
  The name tables were found by looking for where the parts' names begin.
- **By eye.** The Diablo, exported with `reluva-tool fce-obj` and
  rendered with its `car00.tga`, has the right shape, and its skin lands
  in the right places.

All values are little-endian. Floats are 32-bit IEEE.

## Header (`0x1F04` bytes)

| Offset | Size | Meaning |
|---|---|---|
| `0x0000` | u32 | room for a pointer: the game puts its polygon buffer here when it loads the model; the files hold leftovers |
| `0x0004` | u32 | number of triangles |
| `0x0008` | u32 | number of vertices |
| `0x000C` | u32 | number of texture pages: 1 on every car, 2-7 on the `Render/pc` models (the highest page used, plus 1) |
| `0x0010` | u32 | offset of the vertices (0) |
| `0x0014` | u32 | offset of the normals (12 × vertices) |
| `0x0018` | u32 | offset of the triangles (24 × vertices) |
| `0x001C` | u32 | offset of the game's workspace, 32 bytes per vertex, after the triangles (below) |
| `0x0020` | u32 | offset of a table of 12 bytes per vertex (not the vertices) |
| `0x0024` | u32 | offset of a table of 12 bytes per vertex, to the end of the file: the normals the lighting uses (`0x49da30`) |
| `0x0028` | 3 floats | half the model's size in x, y, z |
| `0x0034` | u32 | number of dummies (at most 16) |
| `0x0038` | 16 × 3 floats | dummy positions |
| `0x00F8` | u32 | number of parts (at most 64) |
| `0x00FC` | 64 × 3 floats | part positions |
| `0x03FC` | 64 × u32 | each part's first vertex |
| `0x04FC` | 64 × u32 | each part's number of vertices |
| `0x05FC` | 64 × u32 | each part's first triangle |
| `0x06FC` | 64 × u32 | each part's number of triangles |
| `0x07FC` | u32 | number of primary colours (at most 16) |
| `0x0800` | 16 × 4 u32 | primary colours: hue, saturation, brightness, and a fourth value |
| `0x0900` | u32 | number of secondary colours |
| `0x0904` | 16 × 4 u32 | secondary colours |
| `0x0A04` | 16 × 64 | dummy names |
| `0x0E04` | 64 × 64 | part names |
| `0x1E04` | 64 × u32 | room for the pages' texture handles, filled at load; leftovers in the files |

The table offsets count from `0x1F04`. Name slots past the counts may
hold leftover names.

The colours are the paint choices offered for the car. The values fit
hue, saturation and brightness out of 255 (the Diablo's first is 30, 184,
216); this is to be checked against the front end's colour picker.

## Vertices and parts

- Vertices and normals are 3 floats each.
- A vertex is relative to its part's position.
- Triangles index the vertices of their own part (0 is the part's first
  vertex). Reluva makes them index the whole model's vertices.

Parts are levels of detail and pieces that move. Their names are
unreliable: the El Niño's tiny body is called `body6`, and the left and
right wheels swap names between levels on most cars. Their places follow
a fixed scheme, counted over the 40 car models:

- **12 or 13 parts** (the cars you drive):
  - 0: the high body;
  - 1-4: the high wheels;
  - 5: the medium body;
  - 6-9: the medium wheels;
  - 10: the small body;
  - 11: the tiny body;
  - 12: headlights, on 7 cars.
- **6 parts** (traffic and police): 0 the body, 1-4 the wheels, 5 a tiny
  body.
- One model has 5 parts.

The wheels sit at their hubs (Diablo: x ±0.83, y −0.29, z 1.37 front,
−1.40 rear), so they can turn about their position.

The dummies are lights:

- `HFLO`, `HFRE` at the front;
- `TRLN`, `TRRN` at the rear;
- `SMLN`, `SMRN` on some cars.

## Triangles (56 bytes)

| Offset | Size | Meaning |
|---|---|---|
| 0 | u32 | texture page: 0 is the car's `car00.tga` |
| 4 | 3 × u32 | vertex indices, within the part |
| 16 | 12 | `00 FF 00 FF...` in 97% of triangles, otherwise grey values such as `80 80 80 FF`; the game never reads it |
| 28 | u32 | flags (below): mostly 0, 1, 2, `0x80` or 5 |
| 32 | 3 floats | u of each corner |
| 44 | 3 floats | v of each corner (0 at the top of the texture) |

The flags, as the game draws (`0x49d0e0` copies them to its polygons;
`0x49dbf0` draws):

| Flag | Meaning |
|---|---|
| `0x1` | no reflection (environment map) |
| `0x2` | a stronger reflection colour (`0x55e6e0`, default white, instead of `0x55e6dc`, half grey) |
| `0x4` | two-sided: not culled when facing away |
| `0x8` | unlit: one constant colour instead of each corner's lighting |

`0x10`, `0x20` and `0x80` are never tested.

**The reflection** (`0x49db70`, drawn at `0x43400f`): each reflecting
triangle is drawn again with a sphere map of the track's sky (`CHRD`,
`CHRN`, `CHRW` or `CHNW` in `sky.fsh`), added with source alpha (THRASH
blend 1) in the corner colour: `7F7F7F7F` (half grey at half alpha), or
`FFFFFFFF` with flag `0x2`. A corner's place on the map comes from its
normal turned into the camera's space (x, y, z):
u, v = 0.5 + (x, y) × 0.707106 / √(2 (1 + |z|)).

The Cars page draws the flags so, with Hometown's sky maps (`E` picks
one, or none). Taking y downward on screen is Reluva's reading.

Every car uses only page 0. The `Render/pc` models (the cone and
`cop0`-`cop4`) use pages 0-6, from their `.art` files.

## How the game loads a model

Read from nfs3.exe (October 2026). The car loader (`0x4b7a60`) reads the
whole file into memory and fixes it up in place (`0x49cfa0`):

- it allocates 132 bytes per triangle and 4 per vertex, and stores the
  pointer at `0x0000`;
- it points each polygon's corners into the workspace at `0x001C`, at
  the part's first vertex plus the index, 32 bytes each;
- it copies each triangle's page, indices, flags and u, v (not bytes
  16-27).

The textures go into `0x1E04` (`0x49d320`): with one page, the car's
TGA; with more, each `.art` image (`0x49d130`). A triangle's texture is
`0x1E04`[page & 63] (`0x49dd56`).

The workspace holds each vertex as drawn: screen x, y, z at 0, 4, 8; the
lit colour (ARGB) at 16; clip flags at 20 (a triangle whose three
corners share one is dropped); the reflection's u, v at 24 and 28,
made from the normals (`0x49db70`). The files keep the last values the
authoring tool computed: `amd7`'s has screen positions near 240, 274.

## `.art`: the `Render/pc` models' pages

Worked out from the bytes (October 2026): one image after another to the
end of the file, each a u32 width, a u32 height, then width × height
pixels of 4 bytes: blue, green, red, alpha, the top row first. Page n is
the n'th image. All six files end exactly after their last image:

| File | Pages |
|---|---|
| `cone.art` | 64 × 128, 1 × 1 |
| `cop0.art` | 5 of 16 × 16 |
| `cop1.art` | 7 of 16 × 16 |
| `cop2.art` | 6 of 1 × 1 (plain colours) |
| `cop3.art` | 6 of 16 × 16 |
| `cop4.art` | 32 × 32, 8 × 8, 8 × 8, 32 × 32, 8 × 8, 8 × 8 |

The page counts match the highest page each model uses. Drawn with them
on the Cars page, the cone is orange with white stripes, and the five
`cop` models are police officers in different uniforms (presumably those
standing at roadblocks). `cone.tga`, beside them, is the same picture as
the cone's first page.

## Coordinates and winding

The units are metres, with +y up and +z the car's front.

The space is left-handed (as Direct3D's): x right, y up, z forward. The
evidence is the front end's spinning "NFS" logo (`gengo.viv/go00.fce`),
whose letters read the right way round only when the model is mirrored
into a right-handed space. The wheel names can't settle it, as they
disagree with each other.

Seen from outside, triangles wind clockwise: in 98% of them,
(b − a) × (c − a) points away from the corners' normals.

Reluva negates z when it reads models into its right-handed world. That
makes them counter-clockwise from outside, which is `gfx3d`'s front face.

## Tools

- `reluva-tool fce-info <file.fce | file.viv car.fce>` lists the parts,
  dummies, colours, texture pages and flags.
- `reluva-tool fce-obj ... <out.obj>` writes the model as Wavefront OBJ,
  placed and mirrored as Reluva draws it.
