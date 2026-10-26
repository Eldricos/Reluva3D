# Scene files (.ac2)

`Locations/<area>/<scene>.ac2`, about 380 in Murder on the Orient Express.
Each describes one view of a place: its pictures, hotspots, characters,
effects, camera, music and events. A scene's act variants are separate
files: `Salon_01.ac2` is the scene, and `Salon_01_06a.ac2` holds what
changes in act 6A.

The `.ac2` extension is also used for saves (`Text/Scripts/slot*.ac2`
start with `DESC`). Those are a different set of records, read by
another loader, and aren't covered here yet.

## How this was worked out

- Hexdumps of `Salon_01_06a.ac2`, `Salon_01_06b.ac2` (identical but for
  one letter of the scene name) and `menu.main_01.ac2` gave the shape:
  four-character tags, UTF-16 text in fixed buffers, and runs of `0xCD`.
  `0xCD` is what Visual C++'s debug heap fills new memory with, so the
  files are the game's objects written field by field.
- `motoe.exe` (English 1.0, 684,032 bytes) was then read where it loads a
  scene, for interoperability (CONTRIBUTING.md). The functions:

  | Address | What |
  |---------|------|
  | `0x435990` | the scene loader: reads tags until `DONE` |
  | `0x42b2a0` | the object factory: a tag to a new object |
  | `0x40fa20` | every object's common fields |
  | `0x438590` | 2D objects (`SPRT`) |
  | `0x413320` | hotspots (`HOTS`) |
  | `0x426310` | 3D models (`MODL`) |
  | `0x405330` | animated models (`ANIM`) |
  | `0x4403b0` | characters (`WALK`) |
  | `0x42b060` | other characters (`FLLW`, `COND`, idles) |
  | `0x42e2a0` | particles (`SYST`) |
  | `0x403cd0` | animated pictures (`2DSP`) |
  | `0x406710` | videos (`BNKX`) |
  | `0x420980` | items |
  | `0x421ab0` | lights |
  | `0x406a60` | the camera |

  Each object's load function is its class's virtual function at
  `vtable + 0x44`; the classes' constructors set `vtable`, the tag
  (object `+0xaf8`) and the field-group flags (object `+0xb74`).
- Every record in the three files was then checked to end exactly where
  the next tag begins (`reluva-tool ac2-dump` reads them to `DONE`).

## Basics

- **Numbers** are little-endian: 16-bit (u16), 32-bit integers (u32) and
  floats (f32).
- **Tags** are stored as a 32-bit number whose characters read from the
  most significant byte: `SCEN` is `0x5343454e`, bytes `4e 45 43 53`
  ("NECS").
- **Text** is UTF-16 in a fixed buffer, usually 256 units (512 bytes),
  sometimes 32 or 64. It ends at the first 0. What follows in the buffer
  is left over from earlier text or `0xCD`, and means nothing.
  Disabled scripts in the files are text whose first character was
  zeroed, with the rest still readable.
- **Counted records.** Most records start with a u16 count of their
  fields. The fields follow in a fixed order, and reading stops when the
  count is reached, so older files can have fewer fields.
- **Pair lists** are a u16 count, then that many pairs of 256-unit
  texts. Number pair lists are the same with u32s.
- **Optional parts.** After its own fields, an object may have a tagged
  part (`SPRT`, `MODL`...). The loader saves the file position, reads a
  tag, and goes back if it isn't the one it wants.

## The scene's records

The scene loader reads a tag at a time until `DONE` (or the end):

| Tag | Content |
|-----|---------|
| `SCEN` | text: the scene's own path (`Locations\Salon\Salon_01_06a.ac2`) |
| `LOAD`, `FAKE`, `OUTD`, `NVNT`, `SHOW` | u16 each (meanings to be found; `OUTD` likely "outdoors") |
| `FOOT` | text (32): the scene's footstep sounds |
| `PUZZ` | text (32): the scene's puzzle, if any |
| `MUSC` | pair list: music (the second of each pair is a file, `sound\music\motoetheme.mp3`) |
| `EVNT` | pair list: events (`reset`) |
| `CAMR` | the camera, below |
| `DONE` | the end |
| anything else | an object, made by the factory, which then reads its own fields |

## Objects

The factory knows these tags (class sizes from its allocations):

| Tag | What | Reads |
|-----|------|-------|
| `2DOB` | a picture: background, cutout, overlay | common, `SPRT` |
| `2DAN` | an animated picture | common, `SPRT`, `2DSP` |
| `NING` | lightning (built on `2DAN`) | as `2DAN` |
| `BINK` | a video in the scene (water, flames...) | common, `SPRT`, `BNKX` |
| `SPOT` | a hotspot | common, `SPRT`, `HOTS` |
| `ITEM` | an item lying in the scene | common, `ITEM` |
| `LITE` | a light | its own fields only |
| `PART`, `RAIN`, `SNOW` | particles | common, `SYST` |
| `NPCX` | a character | common, `MODL`, `ANIM`, `WALK`, then `FLLW`, `COND`, idles |
| `PCXX` | the player's character | common, `MODL`, `ANIM`, `WALK` |
| `FOOT` | footprints, a model (not reachable from a scene: the scene's `FOOT` comes first) | common, `MODL` |

### Common fields (`0x40fa20`)

One u16 count covers all of them. Groups marked with a flag are only
there for classes that have it: pictures (`2DOB` and those built on it,
`SPOT`, `ITEM`) have 1, 4 and 8; particles 1, 2 and 8; models and
characters all four.

| Field | Type | Notes |
|-------|------|-------|
| file | text | the picture, model or texture (`locations\menu.main\backgrounds\menu.main_01.jpg`) |
| name | text | hotspots: the exit's `Text/navigation.txt` key (`restaurantcar`, shown as "To Restaurant Car") |
| `b08` | u32 | 1 on overlays and effects |
| `b0c` | u32 | 1 in every object seen: shown? |
| x, y, z | f32 | flag 1. Pictures: screen pixels (the top-left corner) and depth: backgrounds at z = −10000, hotspots at 5000. Characters: the floor, y up |
| rotX, rotY, rotZ | f32 | flag 2. Radians |
| 13 × f32 | f32 | flag 4: scale (3), colour (3: characters 0.498, about half), then 7 more (1 1 1 0 0 0 5 on characters) |
| `b10` | f32 | |
| left, top, right, bottom | u32 | a hotspot's rectangle in screen pixels; characters have one too |
| script | text | flag 8: the commands run when it's used (`yesno quit2 0`, `talk dia_debenham+`) |
| listA | pair list | flag 8 |
| listB | pair list | flag 8: conditional scripts, each a condition and what to run instead (`"act 04a, !tag micheladmonish": "doorlocked sp1 L 1, talk gen_locked 1"`) |
| spriteScaleX | f32 | 2D objects: scale across (1; negative mirrors), at 0xb48 |
| `b58` | f32 | 2000 |
| condition | text | when the object is there (`moving >0`, `weather >0`, `act 06a\| act 06b`); see [Conditions](#conditions) |
| spriteScaleY, spriteScaleZ | f32 | 2D objects: scale down (1), and a third (1) |
| `b54` | u32 | 1 |
| flat | u32 | 1 for 2D objects (their constructor sets it too) |
| `b68` | u32 | |
| `b78` | text | |
| `d78` | u32 | |
| `d7c` | u16 | |
| `d0` | f32 | pictures: their drawing depth when set, instead of z (the view outside a window: −11000, behind the background's −10000; a chair's cutout: 260) |
| `b1e` | u16 | hotspots 1, characters 6: a cursor? |
| listC | pair list | |
| listD | number pair list | hotspots: the outline, as screen points (x:y), inside their rectangle |
| `b70` | u32 | |

A field's name in the table is the original's offset in its object
where nothing better is known yet. Reluva uses the same names (in
`src/engines/awe/ac2.cpp`), so they line up with the executable.

### `SPRT`: a picture's settings

Counted: `da4` u16, layer f32 (1000), `db0` u16, `dc8` u16, then yaw,
pitch and roll (f32, radians; the object's rotation, at 4, 8 and 0xc).

### How 2D objects are drawn

The draw method (`0x437ac0`) uses Direct3D 8's `ID3DXSprite`: `Begin`,
then `DrawTransform` for each 256-pixel tile of the picture, then `End`.
The matrix comes from `D3DXMatrixTransformation` with:

- scaling: spriteScaleX, spriteScaleY, spriteScaleZ (0xb48);
- rotation: a quaternion from yaw, pitch and roll
  (`D3DXQuaternionRotationYawPitchRoll`);
- rotation centre: a vector at 0x40, which no loader sets (0);
- translation: x, y (and z), the tile's offset added.

So a picture's pixel (u, v) goes to (u·sx·cos r − v·sy·sin r + x,
u·sx·sin r + v·sy·cos r + y): scaled, turned about its top-left corner
(clockwise on the screen for a positive roll), then moved. The colour is
red, green and blue (0x8c) times 255, with the alpha from `d7c`. The
hit test (`0x437f70`) uses the same scale: the rectangle from (x, y)
with the picture's size times spriteScaleX and spriteScaleY.

Salon_01 shows it: the countryside going by is `movingbg02.bik` (256×256
with alpha) twice, at z −10100, between the background (−10000) and the
snowscape behind it (d0 −11000), shown while `moving >0`. The left one is
at (100, 54), scaled (−1.5, 1.5) and turned −0.698 (−40°); the right one
is at (1029, 145), scaled (1.5, 1.5) and turned 0.698. Each covers its
window at a slant, and Reluva's Scenes page matches a video of the game
there.

### `HOTS`: a hotspot

Counted: its name (text, `exitover`: also the picture shown on mouse
over, `Locations/Menu.Main/Cutouts/exitover.png`), the sounds on mouse
over and on click (texts, `sound\mouseover1.wav`, `sound\select1.wav`),
a u32, and a cursor (text, 32).

### `2DSP`, `BNKX`, `ITEM`

- `2DSP`: counted, three f32s and four u16s (frames and timing,
  probably).
- `BNKX`: counted, five f32s: `de4`, then yaw, pitch and roll again,
  then `dd8`.
- `ITEM`: counted, the item's name (text, 32: an `Text/itemlist.txt` key),
  a text, two u16s.

### `LITE`

No common fields: a u16 count, then a u16, 23 f32s, a u32, a u16, two
texts and a u32.

### `SYST`: particles

Counted, 28 fields of f32 and u16 (the full order is in `ac2.cpp`).
`PART`, `RAIN` and `SNOW` are one class: they share every method (update
`0x42cc20`, draw `0x42d470`) and differ only in their constructors'
defaults (`0x42c5b0`, then `0x434290` for rain and `0x436f40` for snow).
What the fields do, from those methods:

| Field | Default (PART / RAIN / SNOW) | What |
|-------|------------------------------|------|
| `b08` (common) | 0 / 1 / 1 | 1: blended by alpha; 0: added (`ONE`, `ONE`) |
| x, y, z (common) | 0 / (0, 1500, 0) / (0, 1500, 0) | the emitter, in the world (y up, as characters) |
| rotX, rotY, rotZ (common) | 0 | pitch, yaw, roll: turn mode 0's motion (`D3DXMatrixRotationYawPitchRoll`) |
| `d0` (common) | — / 500 / 500 | drawing depth among the 2D layers |
| condition (common) | — / `weather >0` / `weather >0` | |
| `df0`, `df4`, `df8` | 1, 1, 1 | colour, times 255 into the vertex colour |
| `dc8`, `dcc`, `dd0` | 10, 50, 10 / −500, −1000, 0 / 0, −400, 0 | velocity: mode 2's, or mode 0's speeds per axis |
| `dc4` | 5 / 50 / 5 | particles per burst |
| `dd4` | 0.1 / 0.05 / 0.08 | seconds between bursts |
| `dec` | 1 | times each particle is drawn |
| `dea` | 1 / 2000 / 2000 | emitted within ±this of the emitter in x and z (whole numbers, `rand() % (2n) − n`) |
| `de4` | 0.25 | mode 0: speed kept at each bounce |
| `de8` | 3 | mode 0: bounces |
| `ddc` | 4 / 30 / 30 | point size, in world units |
| `e00` | 99 / 0.2 / 5 | lifetime, seconds |
| `e04` | 0 / 2 / 2 | motion: 0 thrown, 1 still, 2 falling |
| `e14`, `e18`, `e1c` | 0, 3, 5 | if set: on for `e18` s, off for `e1c` s (with the sound `b78`) |
| `e28` | 0 | drawn in screen space (an orthographic 1024×768 view) |
| `e0c`, `e10` | 0, 0 / 0, 0 / 20, 5 | sway: x plus `e0c`·sin(angle), the angle growing by `e10` a second |
| `e08` | 9.8 | mode 0's gravity |
| `e40`, `e44` | 0 | fade in over the first, out over the last so many seconds |
| `e38`, `e3a`, `e3c` | 0, 1, 0 | mode 0: each axis's direction: 1 made positive, 2 negative, 0 as it comes (y: 0 is a coin toss) |

Each frame, each particle moves by its mode:

- **1, still:** stays where it was emitted (within ±`dea` on all three
  axes); gone after its lifetime.
- **2, falling:** the system's velocity times the frame's time; gone below
  y = 0. Emitted within ±`dea` in x and z, at the emitter's height.
- **0, thrown:** emitted with a random direction: around by a in [0, 2π),
  out from straight up by b in [0, π/4], speeds (cos a·sin b·`dc8`,
  r·cos b·`dcc`, sin a·sin b·`dd0`), r in [0.5, 1). Its position is its
  start plus that velocity times its age, turned by the object's rotation,
  with y lowered by ½·`e08`·age². Below y = 0, or past its lifetime, it
  starts again from where it is with its velocity times `de4`, until it
  has bounced `de8` times.

New particles come `dc4` at a time every `dd4` seconds, at most 4,096.
They are drawn as Direct3D point sprites (`POINTSPRITEENABLE`,
`POINTSCALEENABLE`, `POINTSIZE` = `ddc`, `POINTSCALE_A` = `B` = 0,
`C` = 1, `POINTSIZE_MAX` = 64): a square of `ddc` × the screen's height ÷
the distance from the eye pixels, at most 64. The texture (the object's
file) is multiplied by the vertex colour, with depth tested but not
written.

In Salon_01, snow falls past each window (x −730 and 580, from y 300,
falling at 300 a second, at d0 −10500: in front of the snowscape, behind
the moving countryside), shown while `weather >0`.

### `MODL`, `ANIM`, `WALK`: models and characters

- `MODL`, counted: a u32, a pair list, two u32s, two animation names
  (text, 32: `sit`), a u32, a text.
- `ANIM`, counted: five f32s and three u16s (0.15, 5, 8, 8, 12, 50, 30,
  20 on Debenham). A count over 8 adds that many named animations:
  text (256), text (64), text (64).
- `WALK`: a u16 count, then points of u16, position (3 × f32), rotation
  (3 × f32).
- Characters other than the player then have a tag: `FLLW` (u32: whom
  they follow) and the next tag, `COND` (text: a condition) and the next
  tag. Then, whatever that tag is (`IDLE` in the files), a pair list of
  idle animations. The game doesn't check that last tag.

## The camera (`CAMR`)

The camera object holds a Granny camera (`granny_camera`) at offset 4,
which it hands to `GrannyBuildCameraMatrices` once loaded (`0x406d5d`).
The record's fields are the Granny camera's, as
`GrannyInitializeDefaultCamera`'s defaults lay it out:

| Field | Type | Granny camera | Salon_01 |
|-------|------|---------------|----------|
| 3 × f32 | f32 | (the object's own; ambient light?) | 0.39 0.35 0.31 |
| x, y, z | f32 | `Position`: the point it turns about | −8.1, 121.8, 515.5 |
| pitch, yaw, roll | f32 | `ElevAzimRoll`, radians | −0.232, −0.017, 0 |
| 74, 78, 7c | f32 | `Offset`: from that point, along the camera's own axes | −10, 276, 990 |
| fov | f32 | `FOVY`, vertical, radians | 0.646 (37°) |
| near, far | f32 | clip planes | 10, 10000 |
| 3 × u16 | u16 | (the object's own: 1024, then the grid's cell size?) | 1024, 0, 20 |
| grid | u16 size, then size × size bytes | | 64 × 64 |
| 3 × f32 | f32 | | |

The view matrix is the inverse of
`T(Position) · Ry(azimuth) · Rx(elevation) · Rz(roll) · T(Offset)`,
looking down −Z with +Y up. The projection is an ordinary perspective
from the vertical FOV and the 4:3 frame. This was found by calling the
DLL's own functions (`GrannyInitializeDefaultCamera`,
`GrannySetCameraAspectRatios`, `GrannyBuildCameraMatrices`,
`GrannyWorldSpaceToWindowSpace`) in the x86 interpreter
(`reluva-tool awe-camera`). Reluva's matrices match them to within 1e-7
for every camera tried, and `awe-check` compares them for a whole
install. Roll's place in the order is a guess, since no camera seen so far
uses it.

**Characters** are placed with the same axes: +Y up, the floor at y = 0.
The game converts every model when it loads it
(`GrannyComputeBasisConversion` at `0x426e08`: right +X, up +Y, back +Z,
the file's own units), so a vertex (x, y, z) in 3D Studio MAX's axes
becomes (x, z, −y). Then it's turned by rotY about Y and moved to the
character's x, y, z. Characters drawn this way stand where the
backgrounds expect them: at a door, on the floor, at the right size.

The grid is a 64 × 64 map of 0s and 1s, with 1s outlining the room's
walls in the Salon. It differs between views of the same room, and how it
maps to the world isn't known yet (`walk 31 0` in scripts may be cells
of it).

## Conditions

Objects (condition), conditional scripts (listB), music (`MUSC`) and
characters (`COND`) hold conditions in the game's own language:

    moving >0
    !has icebucket, act >06a
    act 06a| act 06b| act 07a
    moving 0, !act 06a

Clauses are joined by `,` (all of them) and `|` (any of them); `!` turns
one round. A clause is a word and maybe an argument. The evaluator
(`0x407d00`..`0x408a00`) compares each word's name in turn: `act`,
`chapter`, `debug`, `demo`, `has`, `available`, `lastscene`,
`location`, `master`, `moving`, `rand`, `scene`, `tag`, `nouse`, `talk`,
`test`, `time`, `timer`, `use`, `used`, `wearing`, `weather`, and
`nogen`, `forcegen`, `nogreet` for characters. Number arguments starting
with `<` or `>` compare that way; otherwise they must be equal. Acts
("06a") compare as text. `0` alone never holds.

How `,` and `|` combine when both appear is assumed (`|` splits first);
no sample mixes them. Reluva's evaluator is `testCondition`
(`src/engines/awe/conditions.cpp`).

## Still to find

- What the unnamed fields do: compare scenes, and watch the original.
- The camera grid, and the `walk` command's arguments.
- The save format (`DESC`, `INFO`, `GLBT`...).
