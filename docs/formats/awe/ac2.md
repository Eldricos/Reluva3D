# Scene files (.ac2)

`Locations/<area>/<scene>.ac2`, about 380 in Murder on the Orient Express.
Each describes one view of a place: its pictures, hotspots, characters,
effects, camera, music and events. A scene's act variants are separate
files: `Salon_01.ac2` is the scene, and `Salon_01_06a.ac2` holds what
changes in act 6A.

The `.ac2` extension is also used for saves (`Text/Scripts/slot*.ac2`
start with `DESC`). Those are a different set of records, read by
another loader: see [saves.md](saves.md).

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
- **The fields' meanings** came from the same reading. Each field was
  followed from where its load function stores it (the offset in the
  object) to every instruction that touches that offset: the class's
  constructor (its default), its copy and save functions, and the
  methods that use it (drawing, updating, picking, sounds). Their
  values over all 394 scenes (`ac2-dump` of each) were then checked
  against that reading: the cursor numbers against the cursor pictures'
  order, the scroll speeds against the fog and cloud pictures, and so
  on. The addresses are given with each field.

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
| `LOAD`, `FAKE`, `OUTD`, `NVNT`, `SHOW` | u16 each (meanings mostly to be found; `OUTD`, at the scene's +0x14f2, is "outdoors": characters with `MODL` footprints leave them only there, `0x43ffcd`) |
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
| alpha | u32 | +0xb08. Pictures: the picture has alpha. The loader sets it too when the picture isn't whole 256-pixel tiles or is A8R8G8B8 (`0x437402`), and the hit test then goes by the pixel's alpha (`0x438090`). Particles: blended by alpha, else added |
| lighting | u32 | +0xb0c. Models: Direct3D lighting on while drawn (`D3DRS_LIGHTING`, `0x427332`). 1 in every object |
| x, y, z | f32 | flag 1. Pictures: screen pixels (the top-left corner) and depth: backgrounds at z = −10000, hotspots at 5000. Characters: the floor, y up |
| rotX, rotY, rotZ | f32 | flag 2. Radians |
| 13 × f32 | f32 | flag 4, at +0x8c: a `D3DMATERIAL8` without its alphas, which models hand to `SetMaterial` (`0x4271ac`): diffuse (red, green, blue), ambient (3: characters 0.498, about half), specular (3: 1 on characters), emissive (3) and power (characters 5). Pictures are tinted by the diffuse colour (`0x437ce8`) |
| player | u32 | +0xb10. 1 for the player's character (`PCXX`'s constructor sets it, `0x4331e8`); the game's "player" is the first character with it (`0x4365d0`) |
| left, top, right, bottom | u32 | a hotspot's rectangle in screen pixels; characters have one too |
| script | text | flag 8: the commands run when it's used (`yesno quit2 0`, `talk dia_debenham+`) |
| listA | pair list | flag 8 |
| listB | pair list | flag 8: conditional scripts, each a condition and what to run instead (`"act 04a, !tag micheladmonish": "doorlocked sp1 L 1, talk gen_locked 1"`) |
| spriteScaleX | f32 | the object's scale across (1; negative mirrors), at 0xb48. Models use it too (`D3DRS_NORMALIZENORMALS` unless all three are 1, `0x42736b`) |
| `b58` | f32 | 2000. Loaded and saved, never read |
| condition | text | when the object is there (`moving >0`, `weather >0`, `act 06a\| act 06b`); see [Conditions](#conditions) |
| spriteScaleY, spriteScaleZ | f32 | scale down (1), and a third (1) |
| `b54` | u32 | 1. Loaded and saved, never read |
| flat | u32 | 1 for 2D objects (their constructor sets it too) |
| `b68` | u32 | Loaded and saved, never read |
| sound | text | +0xb78: a sound the object plays. While it's shown, a looping one plays (`0x40ec70`; hidden, it stops, `0x40ec30`); an animated picture plays it at a frame (below); particles with their blinking. Characters keep their animation's sound here at run time. Empty in every scene |
| soundLoops | u32 | +0xd78: the sound loops (`AIL_quick_play`'s count 0). 0 in every scene |
| soundFrame | u16 | +0xd7c: animated pictures: the frame at which the sound starts (`0x40379c`) |
| depth | f32 | +0xd0. The drawing depth when set, instead of z (`0x4104b0`): the view outside a window −11000, behind the background's −10000; a chair's cutout 260 |
| cursor | u16 | +0xb1e: the cursor shown over it, a number into `interface\cursor01.tga`, `cursor02`, `cursorlook`, `cursortake`, `cursoropen`, `cursorwalk`, `cursortalk`, `cursorlisten`, `cursorstairs`, `cursorkeyhole`, `cursorback`, `cursoroperate`, `cursorshadow`, `cursorup`, `cursordown`, `cursorempty` (0 to 15, loaded in that order at `0x413f78`). Defaults: hotspots 5 (walk), items 3 (take), characters 6 (talk); exits use 10 (back) |
| conditionalCursors | pair list | +0xaf0: a condition and a cursor number each; the first whose condition holds is used instead (`0x40e890`: `"tag hutwindowopen": "2"`) |
| listD | number pair list | hotspots: the outline, as screen points (x:y), inside their rectangle |
| pickFirst | u32 | +0xb70. In the scene's pick (`0x435eb0`), objects with it that have an outline or no rectangle are tested before all others, front to back |

A field's name in the table is the original's offset in its object
where its use isn't known (or the game never reads it). Reluva uses the
same names (in `src/engines/awe/ac2.cpp`), so they line up with the
executable.

### `SPRT`: a picture's settings

Counted:

| Field | Type | What |
|-------|------|------|
| scroll | u16 | +0xda4: 0, or the picture scrolls: 1 left, 2 right, 3 up, 4 down (fog, clouds and skies; below) |
| scrollSpeed | f32 | +0xda8: its speed, a twentieth of it in pixels a second (`0x4378d3`: 1000 by default; 250 to 800 on the scrolling ones) |
| filter | u16 | +0xdb0: the texture filter it's drawn with: 0 none, 1 point, 2 linear (`0x437bb7`). 1 in every file |
| `dc8` | u16 | Loaded and saved, never read |
| yaw, pitch, roll | f32 | radians; the object's rotation, at 4, 8 and 0xc |

The loader also marks pictures from `elements\fog` and `skies\clouds`
(+0xdc0, `0x4372e0`): those that scroll aren't drawn while the "Fog,
Haze, and Clouds" option is off ([options.md](options.md)).

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
the diffuse red, green and blue (0x8c) times 255, with the alpha from
+0xd7e (not in the file: 255 unless faded). The hit test (`0x437f70`)
uses the same scale: the rectangle from (x, y) with the picture's size
times spriteScaleX and spriteScaleY.

**Scrolling** (`SPRT` scroll, the update at `0x4378b0`): the picture
keeps a position of its own, at first its x, y, z. Each frame it moves
on by the frame's seconds × scrollSpeed × 0.05, in whole pixels (the
rest is kept for the next frame): left (x down), right (x up), up or
down. Past its own width to the left (x ≤ −width) it goes back by its
width; past the screen's right edge (x ≥ 1024) it comes back by its
width; up and down the same with its height and 768. It's drawn there
and again one width (or height) behind: to the right of it when it
scrolls left, to its left when it scrolls right, below it when it
scrolls up, above it when down (`0x437db6`). So a strip of sky goes by
without a break. Reluva does the same (`ScenePage::scrollPictures`).

Salon_01 shows it: the countryside going by is `movingbg02.bik` (256×256
with alpha) twice, at z −10100, between the background (−10000) and the
snowscape behind it (depth −11000), shown while `moving >0`. The left one is
at (100, 54), scaled (−1.5, 1.5) and turned −0.698 (−40°); the right one
is at (1029, 145), scaled (1.5, 1.5) and turned 0.698. Each covers its
window at a slant, and Reluva's Scenes page matches a video of the game
there.

### `HOTS`: a hotspot

Counted: its name (text, `exitover`: also the picture shown on mouse
over, `Locations/Menu.Main/Cutouts/exitover.png`), the sounds on mouse
over and on click (texts, `sound\mouseover1.wav`, `sound\select1.wav`),
hoverSoundLoops (u32, +0x1410: the mouse-over sound loops while the
mouse stays, `0x413117`; 0 in every file), and key (text, 32, +0x13d0):
a key that clicks it, `VK_ESCAPE` or `VK_RETURN` (`0x436110`; the
options screen's `VK_ENTER` matches no key). On mouse over, the hotspot
also shows the object whose file is its name, and hides it again after
(`0x4130e9`).

### `2DSP`, `BNKX`, `ITEM`

No scene uses `2DAN`, `NING` or `ITEM`; what's below is from the code.

- `2DSP` (`0x403cd0`), counted:

  | Field | Type | What |
  |-------|------|------|
  | flags | u32 | +0xdec, bits (`0x403350`..`0x403a31`): 2 loops; 4 waits `delay` before starting; 8 stops after `frames` frames; 0x20 stops after `loops` loops; 0x40 goes back and forth; 0x100 hides it at the end; 0x200 holds the last frame. 1, 0x10 and 0x80 change what starting and stopping do |
  | fps | f32 | +0xdf0: frames a second (10 by default) |
  | delay | f32 | +0xdf4: seconds before it starts |
  | loops | u16 | +0xdf8 |
  | frames | u16 | +0xdfa |
  | startFrame | u16 | +0xdfc |
  | soundDirection | u16 | +0xe02: the sound (soundFrame) plays going forwards (1), backwards (2), or either (0) |

- `BNKX` (`0x406710`), counted: effect (u32, +0xde4: stops when the
  "Animated Effects" option is off, `0x406251`), yaw, pitch and roll
  again, then loop (u32, +0xdd8, 1 by default: 0 plays the video once
  and holds its last frame, `0x4061da`). Only the scarlet kimono
  (`Calais.Night_7-8.Kimono`) has loop 0; Reluva plays it once.
- `ITEM` (`0x420980`), counted: the item's name (text, 32: an
  `Text/itemlist.txt` key), its description (text, +0xe10; the loader
  replaces it with the list's), and two u16s (+0x1610, +0x1612) that are
  loaded and saved but never read. The list also gives the examine
  event (+0x1410) and description (+0x1010).

### `LITE`

No common fields: a u16 count, then a u16, 23 f32s, a u32, a u16, two
texts and a u32.

### `SYST`: particles

Counted, 28 fields of f32 and u16 (the full order is in `ac2.cpp`).
`PART`, `RAIN` and `SNOW` are one class: they share every method (update
`0x42cc20`, draw `0x42d470`) and differ only in their constructors'
defaults (`0x42c5b0`, then `0x434290` for rain and `0x436f40` for snow).
What the fields do, from those methods:

| Field (offset) | Default (PART / RAIN / SNOW) | What |
|----------------|------------------------------|------|
| alpha (common) | 0 / 1 / 1 | 1: blended by alpha; 0: added (`ONE`, `ONE`) |
| x, y, z (common) | 0 / (0, 1500, 0) / (0, 1500, 0) | the emitter, in the world (y up, as characters) |
| rotX, rotY, rotZ (common) | 0 | pitch, yaw, roll: turn mode 0's motion (`D3DXMatrixRotationYawPitchRoll`) |
| depth (common) | — / 500 / 500 | drawing depth among the 2D layers |
| condition (common) | — / `weather >0` / `weather >0` | |
| red, green, blue (`df0`, `df4`, `df8`) | 1, 1, 1 | colour, times 255 into the vertex colour |
| velocityX, Y, Z (`dc8`, `dcc`, `dd0`) | 10, 50, 10 / −500, −1000, 0 / 0, −400, 0 | velocity: mode 2's, or mode 0's speeds per axis |
| burst (`dc4`) | 5 / 50 / 5 | particles per burst |
| interval (`dd4`) | 0.1 / 0.05 / 0.08 | seconds between bursts |
| copies (`dec`) | 1 | times each particle is drawn |
| spread (`dea`) | 1 / 2000 / 2000 | emitted within ±this of the emitter in x and z (whole numbers, `rand() % (2n) − n`) |
| bounce (`de4`) | 0.25 | mode 0: speed kept at each bounce |
| bounces (`de8`) | 3 | mode 0: bounces |
| size (`ddc`) | 4 / 30 / 30 | point size, in world units |
| life (`e00`) | 99 / 0.2 / 5 | lifetime, seconds |
| mode (`e04`) | 0 / 2 / 2 | motion: 0 thrown, 1 still, 2 falling |
| blink, onTime, offTime (`e14`, `e18`, `e1c`) | 0, 3, 5 | if blink is set: on for onTime s, off for offTime s (with the object's sound) |
| screenSpace (`e28`) | 0 | drawn in screen space (an orthographic 1024×768 view) |
| warmUp (`e34`) | 0 | at load, run for 4.5 s in steps of 0.01 s, so it's already going when the scene appears (`0x42e68b`, `0x42cbb0`). Every particle system in the scenes has it, and Reluva does the same |
| sway, swaySpeed (`e0c`, `e10`) | 0, 0 / 0, 0 / 20, 5 | sway: x plus sway·sin(angle), the angle growing by swaySpeed a second |
| gravity (`e08`) | 9.8 | mode 0's gravity |
| fadeIn, fadeOut (`e40`, `e44`) | 0 | fade in over the first, out over the last so many seconds |
| signX, signY, signZ (`e38`, `e3a`, `e3c`) | 0, 1, 0 | mode 0: each axis's direction: 1 made positive, 2 negative, 0 as it comes (y: 0 is a coin toss) |

Each frame, each particle moves by its mode:

- **1, still:** stays where it was emitted (within ±spread on all three
  axes); gone after its lifetime.
- **2, falling:** the system's velocity times the frame's time; gone below
  y = 0. Emitted within ±spread in x and z, at the emitter's height.
- **0, thrown:** emitted with a random direction: around by a in [0, 2π),
  out from straight up by b in [0, π/4], speeds (cos a·sin b·velocityX,
  r·cos b·velocityY, sin a·sin b·velocityZ), r in [0.5, 1). Its position
  is its start plus that velocity times its age, turned by the object's
  rotation, with y lowered by ½·gravity·age². Below y = 0, or past its
  lifetime, it starts again from where it is with its velocity times
  bounce, as many times as bounces.

New particles come burst at a time every interval seconds, at most 4,096.
They are drawn as Direct3D point sprites (`POINTSPRITEENABLE`,
`POINTSCALEENABLE`, `POINTSIZE` = size, `POINTSCALE_A` = `B` = 0,
`C` = 1, `POINTSIZE_MAX` = 64): a square of size × the screen's height ÷
the distance from the eye pixels, at most 64. The texture (the object's
file) is multiplied by the vertex colour, with depth tested but not
written.

In Salon_01, snow falls past each window (x −730 and 580, from y 300,
falling at 300 a second, at depth −10500: in front of the snowscape, behind
the moving countryside), shown while `weather >0`.

### `MODL`, `ANIM`, `WALK`: models and characters

- `MODL` (`0x426310`), counted: a u32 (`shadow`: the character casts
  one), a pair list (models attached at load:
  `models\poirot\hat_model.gr2`), breath (u32, +0xe30: a breath
  particle system, `BRTH`, `breath01.tga`, follows the bone `Upper
  Lip_R`, blowing forward, `0x425fa9`; outdoor scenes in the cold set
  it), footprints (u32, +0xe34: in an outdoor scene, `OUTD`, the
  character leaves footprint models as it walks, `snowprint_model.gr2`
  with the tag `snowshoeson`, else `footprint_model.gr2`, which fade,
  `0x43fda0`, `0x43fd00`), two animation names (text, 32: `anim`, the
  skeleton's track mask `TrackMask<anim>` its animations are limited to,
  and `anim2`, a prefix tried on every animation's name: `sit` makes
  `idle1` `sitidle1`), clickToWalk (u32, +0xe38, 1 by default: when the
  player's character has it, a click on the floor walks it there,
  `0x416343`; 72 of the 146 `PCXX` have 0, and Reluva leaves them where
  they are), and the condition (text) on which `anim` and `anim2`
  apply.
- `ANIM`, counted: five f32s and three u16s (0.15, 5, 8, 8, 12, 50, 30,
  20 on Debenham): the talk ease (twice it for full-body animations),
  the blink interval and the idle variations' interval (each a least
  and a most, in seconds), and the chances of variations 2, 3 and 4 on
  (percent). A count over 8 adds that many renamed animations: a
  condition (text, 256), the name scripts use (64) and the file played
  for it (64), or a sound (`.wav`).
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
| offsetX, offsetY, offsetZ | f32 | `Offset` (+0x74): from that point, along the camera's own axes | −10, 276, 990 |
| fov | f32 | `FOVY`, vertical, radians | 0.646 (37°) |
| near, far | f32 | clip planes | 10, 10000 |
| `238`, `23a`, cellSize | u16 | (the object's own: 1024 and 0, loaded and never read; then the walk grid's cell size, at +0x23c) | 1024, 0, 20 |
| grid | u16 size, then size × size bytes (at +0x23e) | the walk grid | 64 × 64 |
| `1240`, whiteoutStart, whiteoutEnd | u32, f32, f32 | (the object's own: a u32 never read; then where the player starts to turn white and is all white, in z: [shadows.md](shadows.md), `0x402e20`) | |

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

### The walk grid

Each camera has its own map of where characters can walk: 64 × 64 cells
over the floor (y = 0), centred on the world's origin, each `cell` units
square (cellSize, the u16 at +0x23c: 20, 25, 35 or 50 in the Salon's views). Cell
(a, b) is byte a × 64 + b of the grid, and

    x = (a − 32) · cell + cell / 2
    z = (32 − b) · cell − cell / 2

is its centre (`0x406dd0`). The other way (`0x406e60`), a point goes to
the cell with (a − 32)·cell ≤ x ≤ (a − 31)·cell and
(31 − b)·cell ≤ z ≤ (32 − b)·cell; on a boundary, the higher a and b.
Drawn over the backgrounds, the 1s trace the floor's edges and what
stands on it (the Salon's aisle between the chairs, the bar counter's
foot), so the axes are right.

Values (the path-finder's test, `0x42e8a0`): 0 open; 1 blocked; 2
blocked unless the search allows it; 3 blocked on the first try at a path
and open after (`0x42e9e0` copies the grid with 3s cleared). The cells
outside a room are 0 too: the walls' 1s keep a search inside.

- **Clicks:** `0x406f20` turns a screen point into a cell: Granny's picking
  ray for the pixel (`GrannyGetPickingRay`, y counted from the bottom),
  met with the floor plane, then the cell under that point. It can also
  say whether the cell is open (0 or 3).
- **Scripts:** `walk a b` walks to cell (a, b). In the samples, 17 of the
  18 such commands in scenes with the player's character are reachable
  from where it stands; `awe-check` counts them for a whole install.
  `walk sp1` and the like walk to a character's walk point (the `POINT`s
  of its `WALK` part); a name walks to that object (`0x440b30`).
- **Paths** (`0x440ce0`): from the character's cell to the goal's, if they
  are at least 3 apart (in a + b). The path-finder (a separate object,
  `0x42e840`) is tried three times, each less strict: as the grid is,
  accepted if the path has at most 20 points; with 3s open, under 32;
  with the start and goal cells cleared too, under 32. Its points are
  cells, walked to through their centres.

  **The path-finder.** This was worked out from `motoe.exe`'s code,
  `0x42e840` to `0x432250`, and checked point for point against that
  code run in the x86 interpreter (below). The object is built for a
  search on a lattice: 48 moves (to the 8 neighbours, then 2 and 3 cells
  away), 8 headings, and tables of turning costs (`0x42ea90`, `0x42ec60`,
  `0x42ed90`). But the game builds it with headings off (`0x42e900` sets
  the flags at +0x1010..+0x1014 to 1, 0, 0, 0, 1 and +0x1028 to 8), so
  most of that is never used. What runs (`0x42ef40`):

  1. **Ends.** If the goal or the start cell is blocked, there's no path.
  2. **A\*** (`0x42f1f0`), in a window of 192 × 192 cells that holds both
     ends: the goal and start more than 128 cells apart in a or b give
     no path. Each window cell is a node.
     - **Moves**, tried in this order:
       - (0, −1), (1, 0), (0, 1), (−1, 0), each costing 128;
       - (1, −1), (1, 1), (−1, 1), (−1, −1), each costing 181.
     - **Estimate:** the straight distance to the goal times 128,
       truncated (`0x42ea40`'s table).
     - **Open list:** a linked list. New nodes join at the end. The node
       taken next is the first one with the least cost plus estimate.
     - **Moving on:** a move is skipped if it leaves the window, its cell
       is blocked, or it goes back to the start. A node reached more
       cheaply than before is updated, and joins the list again if it had
       left it.
     - **Stopping:** the search stops when the goal is taken from the
       list, or the list runs out (no path).
  3. **Points** (`0x430420`): the cells from the goal back to the start,
     at their centres (n + 0.5). The start itself is left out, and a path
     longer than 127 points gives none.
  4. **Smoothing** (`0x430690`). It goes along the points from the start,
     keeping the last point kept, which starts as the start cell itself.
     - **The turn** (`0x430df0`, `0x431790`): from the point kept, facing
       its heading, along a circle of radius 0.1 cell, then straight. Of
       the two ways round, the shorter is taken. The start faces heading
       0.
     - **The test** (`0x430b60`): a turn is clear if, at every 0.2 along
       it, the cell under the point is open, as are the cells under the
       corners of a square 0.1 wide round it. Leaving the grid fails the
       test.
     - **Dropping points:** while the turn from the point kept to the
       point after the current one is clear, the current one is dropped.
       Otherwise the current one is kept.
     - **Headings:** each point gets the heading the turn into it ends
       with.

     The headings come out as angles in [0, 2π), each sum snapped into
     that range and to 0 within 1e-8 of either end. `acos` is the C
     runtime's, as atan2(√((1 + x)(1 − x)), x).

  Its results are the points left, from the goal back towards the start,
  each three doubles: cell a and b, and a heading. 0x441089 truncates them
  to cells and walks through their centres, from the last point to the
  first.

  Reluva's port is `WalkGrid::search` and `WalkGrid::findPath`
  (`src/engines/awe/walk_grid.cpp`), with the game's three tries.
  - **Arithmetic:** the game does its sums on the x87 at 53 bits. The
    port does the same sums in doubles, in the same order, so its results
    are the same to the bit. The file is built without fused
    multiply-adds, which would round differently.
  - **The check:** `reluva-tool awe-path <game folder> -all [pairs]`
    plans paths between random open cells of every scene, four ways each
    (3s open or not, 2s allowed or not). It plans each both with the
    port and with the game's own code from the user's `motoe.exe`, run in
    the x86 interpreter (`src/engines/awe/game_paths.cpp`), and compares
    every point and heading.
  - **Result:** on the English 1.0 install, 20 pairs in each of the
    281 scenes with a walk grid gave 22,480 searches (19,024 with a
    path), with 0 differences.
  - **Speed:** a search takes about 0.2 ms, against 5 to 35 ms for the
    game's code in the interpreter (31 ms on average over that run).
  - **Assumption:** the interpreter keeps the x87 at 53 bits, which is
    Windows' default for a program; it is assumed that the game leaves it
    there.

Characters walk at their walk animation's own pace. The walk carries the
body forward (Poirot's root, `Bip01`, moves 240 units in its 0.8 s
loop: 300 units a second), and the loop starts again from the same place,
so the character moves on by that much each loop.

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

- The fields the game loads but never reads keep their offsets: the
  common `b54`, `b58` and `b68`, `SPRT`'s `dc8`, the item's `1610` and
  `1612`, and the camera's `238`, `23a` and `1240`.
- The light's fields (`LITE`), the camera's first three (`180`, `184`,
  `188`) and the material's emissive colour and power (`bc`, `c0`, `c4`,
  `cc`) are being named with the characters' lighting.
- What Reluva doesn't do yet: the cursors (`cursor`,
  `conditionalCursors`), the characters' breath and footprints, the
  objects' own sounds (`sound`, `soundLoops`, `hoverSoundLoops`; empty or
  0 in every scene), the hotspots' keys (Reluva's save and options
  screens press them by name instead), `pickFirst` in its picking, and
  the animated pictures (`2DAN`, `NING`: in no scene).
- The scene records `LOAD`, `FAKE`, `NVNT` and `SHOW`.
