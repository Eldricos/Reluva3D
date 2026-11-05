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

## Lighting

From nfs3.exe (November 2026), the drawing of a car (`car_draw_wheels`
`0x4bab70` and its siblings):

- **The car's colour** (`0x4ba810`, `carShadeColour`): the track's
  shading where the car is, each channel the mean of the four corners
  of the road polygon under it (`0x4206f0`, `FF7F7F7F` without one),
  averaged again over the car's and its four wheels' polygons. The
  track's colours are kept already scaled by the horizon's ambient
  percentages (`0x419660`, integer, truncated). By night (`0x4b97b0`,
  `carLitBy`) each other car whose lights are on adds, when this car's
  position is inside its light (near plane to reach, no wider or higher
  than deep), the light's colour × (1 − |x| / z)(1 − z / reach), a roof
  light's × (1 − z / reach) (z as |z|, its red behind; that car's
  headlights only count then); the lights' sum is divided by their
  number and added, at most 255.
- **Each vertex** (`0x49d8f0`, `0x49da30`, `carShade`,
  `carVertexColour`): base = (r / 3, g / 3, b / 2), top = 2 × the
  colour (at most 255), colour = base + (top − base) × n · L when
  n · L > 0, else base, n the vertex's normal turned with the car.
  L (`0x55d2a8`, set by the horizon loader `0x478340`) is
  (sin θ, 0.5, cos θ) made a unit vector, θ the `.hrz` rotation
  (degrees × 65536 / 360, the game's sine table; `carLightDirection`).
  Fixed for the track: the car is lit from the sky's rotation, not the
  sun's picture.
- **The reflection** (`0x4ba350`, November 2026; `carReflection`,
  `reflectionFrame`). Only with the option at `0x6fbc24` on (on from a
  234 MHz processor up, frd.md) and not in the rear-view mirror (view 1, whose winding `0x4349a0` turns); the body (part 0,
  or 5 at the lower detail) and the headlights (12) get it, the wheels
  not (`0x4bb200`, `0x4bab70`). The picture is the sky's sphere map,
  `CHRD`, `CHRN`, `CHRW` or `CHNW` of the track's `sky.fsh` by the
  variant (`0x47ac30`, at `0x6ff9a0`). Its two corner colours:
  - from the car's colour above (after `carLitBy`) in HSV (`0x49c880`,
    integer, the hue a turn in 256) and the sky's colour (`0x6ff9f8`,
    the clear colour: the sky's top, black where no sky is drawn,
    hrz.md);
  - under cover (`0x420bf0` at the car's slice, col.md) the cover's
    reach n (the first n from 1 at which slice ± n isn't covered, 8 at
    most) mixes the sky towards the car's colour, the sky's share
    256 − 32 n of 256, and dims both colours by (256 − 16 n) / 256;
  - the mixed sky in HSV gives the hue; the normal colour (`0x55e6dc`)
    96 / 256 of its saturation, the strong one (`0x55e6e0`, flag 0x2)
    half; their value and alpha 128 + the sky's value × the car's value
    / 512, held to 127-255, the normal one less a quarter of the paint's
    value (+0x8B0, the paint colour's HSV value from the set-up, kept by
    `0x4b7a60`; a race's dump has the cars' fce colours there, the
    Corvette's first 0, 255, 112). Back to RGB (`0x49ca50`), × the dim
    (255 / 256 in the open), then × the effects intensity (`0x49d800`).

  The map's place for a corner (`0x49de20`, `0x49db70`): its normal
  turned with the car into the track, then into the frame of the
  camera's view of the car (`0x49d780`, kept at `0x55e6ec`): d = the
  camera less the car, yaw atan2(dx, dz) − ½ turn, pitch
  atan2(√(dx² + dz²), dy) − ¼ turn, Ry(yaw) Rx(pitch), no roll; so z
  points from the camera to the car and x along the view's right. Then
  u, v = 0.5 + (x, y) × 0.707106 / √(2 (1 + |z|)), y upwards: the map
  has the sky in its lower half. The Tracks page draws it over each
  computer car (key Z).
- **The glows** (`0x4b8740`, `carLightGlow`), one per dummy by its
  name's letters: H a headlight, T a tail light, S a roof light; the
  second F (facing forward) or R (back); the third L or R (a roof
  light's side); the fourth O or E (which police headlight flashes
  first). With d the car's forward · the unit vector from the light to
  the camera (−d for R), drawn when d > 0, and with b = 1 − 0.3 ×
  (r + g + b) / 765 of the car's colour, at least 0.7
  (`carGlowBrightness`):
  - T, with the lights on or braking (+0x509, the brake held: only when
    more than 5 m/s too fast, frd.md): red, alpha (198 braking,
    else 128) × min(1, 4d) × b, size max(d, 0.25) × 0.8 × b;
  - H, with the lights on, or a police car's siren on and its fourth
    letter O in the first half of the 32-step cycle, E in the second:
    white, alpha 255 × min(1, 4d) × b, size d × 0.8 × b × (1.5 on high
    beams);
  - S, a police car's siren on: p = 2 × (step mod 32) / 32, folded back
    above 1; L blue (`0xFF8080FF`) with p, R red (`0xFFFF8080`) with
    1 − p: alpha 255 × p × b, size max(p, 0.25) × 1.5 × b, pulled 0.75
    towards the camera.

  Each is a `glw3` quad (`0x8b4840`, loaded from `sfx.fsh` by
  `0x4d5370`; `0x492370` draws it, `0x491190` too for the track's
  lights along with `glw0`) added by its alpha, its corners `size`
  from its centre, turned by ¼ turn × (x / z + 1) across the screen.

  **Its depth** (`0x492370`, November 2026). The quad is projected at
  the light's place (only when that is from 0 to 300 deep, `0x43960001`
  compared as an integer, and its centre on the screen), its corners
  `size` × the screen's scales / z, but its 1 / z, the depth the
  z-buffer tests and the sorting key, are taken from the sixth argument
  plus the seventh, the light's own z when the sixth is 0. For the T and
  H glows the car's drawing (`0x4bb200`, `0x4bab70` → `0x4b8740`)
  passes the least view depth of the body's vertices (`0x49de20` for
  part 0, part 5 at the lower detail: the minimum of the transformed
  vertices' z in `0x49d9d0`) and 0, so a glow is as near as the body's
  nearest point: the body never hides it, while anything nearer than
  the car does. The roof lights pass 0 and −0.75: their own depth less
  0.75. The Tracks page does the same: each quad is moved along its ray
  to that depth and scaled with it, which leaves it on the same pixels.
  Until November 2026 it pulled the T and H glows 0.3 towards the
  camera instead, which let the body hide them when seen from the side.
- **The rain's flares** (`0x491bc0`, November 2026; `rainFlare`). For
  each T or H glow drawn whose dummy's fifth letter isn't N, with the
  Special Effects option not Off, in the weather (`0x6fd4cc`), the car's
  slice not covered and not in the mirror (view 1): the road polygon under the
  light (`trackpos_build`), and only on a surface the table at
  `0x55308c` (16 surfaces × 0x24 bytes, a row of 16 per weather kind)
  gives 10: surfaces 1, 4, 6, 7, 10, 11 and 12 in rain or snow (the two
  rows agree there), the polygon's record byte 1 & 0x3F (frd.md). A
  `glw4` quad (`0x8b486c`, a glow at the top smeared down) on the
  ground from the light towards the camera: with d the light less the
  camera on the ground, L its length and L' = L × (−the light's way ·
  d / L) (the car's forward for H, backward for T), half wide size / 2
  (`0x53bcc8`) either side, long 0.75 L' (`0x53bce0`), at most 30, or
  the half width when that is more; the glow's colour × 166 / 256, then
  × the effects intensity; sorted at L'. **Its height** (November
  2026): `trackpos_build` (`0x420650`) finds the road polygon under the
  light and puts the light on the road by `0x4969c0`; the corners start
  at that height and each is put on the road the same way by
  `0x4968a0`, then raised 0.125 (`0x53bcf0`). Both take the point into
  the road's frame at the track position (`trackpos_road_frame`,
  `0x420230`: with a polygon found, `+0x64` = 2, its up and forward the
  block's virtual road vector that the polygon's 8-byte record's byte 0
  picks, frd.md, six s16 / 32768; right = up × forward; the point the
  polygon's vertex 1), drop the part along the up, and take the point
  back: the height is point.y + ((p − point) · right) right.y +
  ((p − point) · forward) forward.y. On a cambered road that isn't the
  height straight below, which the page took until November 2026
  (`polygonRoadFrame`, `roadFrameHeight`). Without a polygon there is
  no flare. On Hometown and Lost Canyons the vector a road polygon's
  record picks is the polygon's own normal or its negative (a wall's),
  which the projection doesn't mind: within 0.99 for all but 27 of
  Hometown's 11,624.
- **The fog's beam** (`0x492980`, from `0x4b8efe`; `fogBeam`,
  `fogBeamColour`). For each H dummy with the lights on (or flashing
  with the siren), facing the camera or not, while the fog's density
  this frame (`0x678b40`, set by the sky's draw, `0x470ad0`) is over 30:
  white, alpha 228 × min((fog − 30) / 30, 1) (`0x53ffe0`-`0x540000`),
  half on low beams. Within 100 of the camera (D), n = 12 × (1 − D /
  100) truncated, 3-12, quads of `glw1` (`0x8b47e8`, cloud-like noise)
  round a cone along the car's forward: a ring of radius 0.1 at the lamp
  in the colour, one of 0.75 six units ahead in black and clear, its
  first corner at ½ × (−½ − (r + u) / 2) turns (r, u: the unit vector to
  the camera along the car's right and up). Looking into it (that
  vector · forward f above 1 − w, w = 5 × atan2(0.75, 6) in turns,
  0.099) the colour is × (1 − (f − 1 + w) / w), truncated. v runs from
  −s at the far ring to 1 − s at the lamp, u round the ring, s = (+0x944
  & 0x7FF) / 2048, +0x944 going up by int(the speed along the car's
  forward + 1) a step: the picture streams along the beam with the
  speed. Sorted at the deepest corner − 0.4.

  The Tracks page draws both for its computer cars, added by their
  alpha over the scene but not through it. Checked in `nfs-host` (a
  night race in the rain on Hometown, `-count`): in its first 2,338
  frames `0x491bc0` reached its quad 1,905 times of 2,014 calls, the
  road's surface 1, and the intro's shots show short red smears on the
  wet road under the cars' tail lights, the picture's bright top a
  little in front of the light, as the page has them; `0x492980` was
  called as often, the fog there being 60. A day race in the rain on
  Hometown with the player held on slice 230 (fog 85) and a computer
  car held six slices on, turned to face it 2 m to the side (`t5-fog`,
  `-pokeevery` of its position, rows and speed) shows that car's two
  headlight glows and their short flares on the road below them, and no
  beam: looking into it the beam's colour is scaled to nearly nothing,
  as the code has it.

  The page compared with the game's frames (November 2026), its cars
  placed from the frame's car dump (`RELUVA_NFS_TRACK_CARS`) and its
  camera from the same sky draw (`RELUVA_NFS_TRACK_CAMERA`):
  - **The fog** (`t5-fog`, sky draw 500, frame 2700): the car facing
    the camera 36 ahead shows two white headlight glows, their short
    flares on the wet road below and no beam on both, the glows of the
    same size. The page's car is paler, more fogged: 145, 140, 141 on its
    lower body against the game's 171, 168, 166.
  - **By day** (`t6-day`, Hometown on the grid, sky draw 150, frame
    2345; four cars 19-74 ahead): in the same places in the view (the
    page's window has another shape). The game's cars are other models
    than the page's Corvette, so the reflections can only be compared
    roughly: the game's silver car
    19 ahead shows the sky as grey-white bands along its flanks, the
    page's Corvette deep blue streaks of `CHRD`'s sky there.
  - **By night** (`t6-night`, Hometown, sky draw 150, frame 2345, the
    cars 70 and more ahead, `projtex` off): both show the cars' red tail
    light glows, small and of the same size, the bodies dark grey; the
    page's bodies a little lighter.

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
