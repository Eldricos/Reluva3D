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
| name | text | |
| `b08` | u32 | 1 on overlays and effects |
| `b0c` | u32 | 1 in every object seen: shown? |
| x, y, z | f32 | flag 1. Pictures: screen pixels (the top-left corner) and depth: backgrounds at z = −10000, hotspots at 5000. Characters: the floor, y up |
| rotX, rotY, rotZ | f32 | flag 2. Radians |
| 13 × f32 | f32 | flag 4: scale (3), colour (3: characters 0.498, about half), then 7 more (1 1 1 0 0 0 5 on characters) |
| `b10` | f32 | |
| left, top, right, bottom | u32 | a hotspot's rectangle in screen pixels; characters have one too |
| script | text | flag 8: the commands run when it's used (`yesno quit2 0`, `talk dia_debenham+`) |
| listA, listB | pair lists | flag 8 |
| alpha | f32 | 1 |
| `b58` | f32 | 2000 |
| text | text | |
| `b4c`, `b50` | f32 | 1 and 1 |
| `b54` | u32 | 1 |
| flat | u32 | 1 for 2D objects (their constructor sets it too) |
| `b68` | u32 | |
| `b78` | text | |
| `d78` | u32 | |
| `d7c` | u16 | |
| `d0` | f32 | |
| `b1e` | u16 | hotspots 1, characters 6: a cursor? |
| listC | pair list | |
| listD | number pair list | |
| `b70` | u32 | |

A field's name in the table is the original's offset in its object
where nothing better is known yet. Reluva uses the same names (in
`src/engines/awe/ac2.cpp`), so they line up with the executable.

### `SPRT`: a picture's settings

Counted: `da4` u16, layer f32 (1000), `db0` u16, `dc8` u16, then three
f32s.

### `HOTS`: a hotspot

Counted: its name (text, `exitover`: also the picture shown on mouse
over, `Locations/Menu.Main/Cutouts/exitover.png`), the sounds on mouse
over and on click (texts, `sound\mouseover1.wav`, `sound\select1.wav`),
a u32, and a cursor (text, 32).

### `2DSP`, `BNKX`, `ITEM`

- `2DSP`: counted, three f32s and four u16s (frames and timing,
  probably).
- `BNKX`: counted, five f32s.
- `ITEM`: counted, the item's name (text, 32: an `Text/itemlist.txt` key),
  a text, two u16s.

### `LITE`

No common fields: a u16 count, then a u16, 23 f32s, a u32, a u16, two
texts and a u32.

### `SYST`: particles

Counted, 28 fields of f32 and u16 (the full order is in `ac2.cpp`). In
the main menu's snow: an emission rate of 15, a lifetime, a gravity of
9.8, and so on.

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

A u16 count (23), then:

| Field | Type | Menu |
|-------|------|------|
| 3 × f32 | f32 | 0.5 0.5 0.5 |
| x, y, z | f32 | 0, −171, 469.8 |
| pitch, yaw, roll | f32 | 0.349 (20°), 0, 0 |
| 3 × f32 | f32 | 0, 200, 0 |
| fov | f32 | 0.611 (35°) |
| near, far | f32 | 100, 10000 |
| 3 × u16 | u16 | 1024, 0, 50 |
| grid | u16 size, then size × size bytes | 64 × 64. Only read when size ≤ 64 (else skipped) |
| 3 × f32 | f32 | |

The grid is likely the floor's walkable area or its depth for placing
characters. To be checked against a room scene.

## Still to find

- What the unnamed fields do: compare scenes, and watch the original.
- The camera's exact projection, so characters can be placed on the
  backgrounds. This needs a room scene with a background.
- The save format (`DESC`, `INFO`, `GLBT`...).
