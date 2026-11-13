# id Tech 3: models (`.md3`) and skins (`.skin`)

> **Status:** decoded · **Code:** `src/engines/idtech3/formats/md3.cpp`, `model_cache.cpp` · **Checked on:** every `.md3` of Elite Force: 578 in 1.0 and 1.1, 594 in 1.2, 716 with the Expansion Pack (`reluva-tool idtech3 model-check`, November 2026) · **Confidence:** confirmed unless marked

## Overview

Models (`.md3`), the `.skin` files that choose their shaders, and the players' `animation.cfg`.

## Layout

All values little-endian; integers 32-bit, floats IEEE single.

### Header (108 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IDP3` |
| 4 | i32 | version, 15 |
| 8 | char[64] | name |
| 72 | i32 | flags |
| 76 | i32 × 4 | frame, tag (per frame), surface and skin counts |
| 92 | i32 × 4 | offsets of the frames, tags, first surface, and the end |

**Frames** (56 bytes each): f32[3] bounds minimum, f32[3] maximum, f32[3]
local origin, f32 radius, char[16] name.

**Tags** (112 bytes each, frame by frame): char[64] name, f32[3] origin,
f32[3][3] axes. A tag is where another model attaches: a player's torso
sits on its legs' `tag_torso`, the head on the torso's `tag_head`, a weapon
on `tag_weapon`. What's attached is placed by the tag's axes (as columns)
and origin, in the tagged model's space.

### Surfaces

Each surface starts with a 108-byte header, offsets relative to it:

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IDP3` |
| 4 | char[64] | name (what `.skin` files refer to) |
| 68 | i32 | flags |
| 72 | i32 × 4 | frame count (the model's), shader, vertex and triangle counts |
| 88 | i32 × 5 | offsets of the triangles, shaders, texture coordinates, vertices, and the next surface |

- **Shaders:** char[64] name, i32 index (the original's own use).
- **Triangles:** i32[3], indices into one frame's vertices. Wound like the
  maps' polygons (clockwise seen from the front).
- **Texture coordinates:** f32[2] per vertex, the same in every frame.
- **Vertices:** frame by frame, 8 bytes each: i16[3] position in 1/64 units,
  and the normal as two angles in a byte each. The high byte is the
  latitude around Z and the low byte the longitude from +Z, both
  × 2π/256: normal = (cos lat · sin lng, sin lat · sin lng, cos lng).

- **What the original's loader checks** (`R_LoadMD3`: `0x455d70` in 1.2's
  `stvoy.exe`, `0x451dc0` in the Expansion Pack's, the same in
  `stvoyHM.exe`): the version, 15, and that the model has a frame; a
  surface with more than 1000 vertices or 2000 triangles only gets a
  warning. A surface's own frame count isn't read: every surface has the
  model's frames. The loader steps to the next surface by the surface's
  end offset, even when it's 0. *(from the code)*
- **An empty surface.** The Expansion Pack's
  `models/mapobjects/scavenger/k_chair1.md3` (placed by
  `_holodeck_warlord`) has a second surface, `line01`, with 0 frames, no
  vertices or triangles, and every offset 0. Its shader name is read from
  the surface's own start (`IDP3line01`, which isn't a shader), and it
  draws nothing; the chair is its first surface. The file goes on past the
  header's end with the remains of an earlier export, which nothing reads.

Files ending `_1.md3` and `_2.md3` are lower-detail copies.

### Skins

Text, one surface per line: `surfacename,shader`. Lines for tags
(`tag_head,`) and empty shaders are ignored; surface names are matched
ignoring case.

### Player animations (`animation.cfg`)

A player's folder holds an `animation.cfg` naming the runs of frames its
parts play. How the file is read was taken from the GPL ioquake3 game code
(`cg_players.c`); Reluva's reader is `src/engines/idtech3/formats/player_animation.cpp`.

Lines of words (`sex f`, `soundpath alexa`) are settings; every other line
is four numbers and a comment naming the animation:

    first-frame  frames  looping-frames  frames-per-second  // NAME

- Names starting `BOTH_` play on legs and torso together, `TORSO_` on the
  upper body only, `LEGS_` on the lower body only. Elite Force's file lists
  them in that order, as Quake III's does.
- The frame numbers count through both models as if one followed the
  other, but the legs model has no torso frames: a legs animation's first
  frame is that much earlier in the legs model, by the first legs
  animation's first frame minus the first torso animation's (43 for
  Elite Force's players: legs from 138 in the file, 95 in `lower.mdr`).
- After playing through, an animation repeats its last *looping-frames*
  frames, or with 0 stays on its last frame.
- Legs and torso each play their own animation at their own speed; the
  torso sits on the legs' `tag_torso` of the legs' current frame, the head
  on the torso's `tag_head`. Drawing the halves on unrelated frames is
  what tears them apart at the waist.

## Behaviour

Shader names are looked up without their extension: many models name
`models/x/y.tga` where a script defines `models/x/y` (the Borg map objects
do), see [shader.md](shader.md).

### Drawing

Models have no lightmaps or vertex colours. A surface's shader (or, with
no script, the image of that name) is lit by a light: an ambient and a
directed colour from a direction. In a map that's the light grid where the
model stands (see [bsp.md](bsp.md)); `rgbGen lightingDiffuse`, `vertex` and
`exactVertex` all mean that light. Without a map, a fixed light.

Players are three models in one folder: `lower.md3`, `upper.md3`,
`head.md3`, assembled by their tags, with `<part>_<skin>.skin` choosing
their shaders.

## In Reluva

### In maps

Items, weapons and characters get their models from the game code, not
the map, so the viewer shows only what entities name themselves in
`model` (not brush models, `*n`) or `model2`, placed by `origin` and
`angles` ("pitch yaw roll", or `angle` for the yaw). Static props are
usually built into the map itself as triangle soups.

## Sources and checks

`model-check` reads every model, `.md3` and `.mdr` alike
([mdr.md](mdr.md)):

| Release | `.md3` | `.mdr` | Models | Surfaces |
|---|---|---|---|---|
| 1.0, 1.1 | 578 | 170 | 748 | 2790 |
| 1.2 | 594 | 196 | 790 | 2924 |
| 1.2 with the Expansion Pack | 716 | 201 | 917 | 3833 |

Every surface finds its shader or image except `k_chair1.md3`'s empty one
(above, the Expansion Pack's) and 12 that name none, in every release:
models whose shader the game code picks
(`models/misc/telep.md3`) and player heads whose skin the game chooses
(`models/players/munrocrew/head.md3`). Placed models sit where the
original puts them.

Sources: the GPL Quake III Arena source (`code/qcommon/qfiles.h`, the
renderer's `tr_model.c`, `tr_surface.c`), read for the layout, not copied;
the retail executables for what their loader checks, at the addresses
given.
