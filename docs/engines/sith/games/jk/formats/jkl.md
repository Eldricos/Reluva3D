# Jedi Knight: levels (`.jkl`)

> **Status:** partial · **Code:** `src/engines/sith/formats/level.cpp` · **Checked on:** US retail, all 35 files · **Confidence:** layout confirmed; behaviour inferred unless marked

## Overview

The levels: the 25 single-player levels in `JK1.GOB`, the 6 multiplayer
and 3 Capture the Flag levels in theirs, each in `jkl\`, named by the
episode ([episode-jk.md](episode-jk.md)). `Res2.gob` has a 35th,
`static.jkl`, with the lists every level shares and no geometry. Plain
text, up to 3.5 MB; the comment on the first line names the file it was
saved as and when (`Created 9/2/1997`).

A level is sectors, convex rooms whose surfaces are its walls, floors
and ceilings; a surface with an adjoin is a way into the next sector.
Then the things placed in the sectors, each from a template.

## Layout

Line by line; a `#` starts a comment to the line's end. Sections start
`SECTION: <name>` (or `Section:`), in this order:

| Section | What |
|---|---|
| `JK` | Comments only |
| `COPYRIGHT` | A picture in `.` and `@`, ending `copyright (c) 1997 lucasarts entertainment co.` |
| `HEADER` | The settings below |
| `SOUNDS` | `World sounds <n>`, then a `.wav` a line, not numbered |
| `MATERIALS` | `World materials <n>`, then `<i>: <file.mat> <xTile> <yTile>` |
| `GEORESOURCE` | The geometry ([below](#georesource)) |
| `SECTORS` | `World sectors <n>`, then each sector ([below](#sectors)) |
| `AICLASS`, `MODELS`, `SPRITES`, `KEYFRAMES`, `ANIMCLASS`, `SOUNDCLASS`, `COGSCRIPTS` | `World <what> <n>`, then `<i>: <file>`: the `.ai`, `.3do`, `.spr`, `.key`, `.pup`, `.snd` and `.cog` files it uses |
| `COGS` | `World cogs <n>`, then `<i>: <script.cog> <values...>`: each script and the values it starts with |
| `TEMPLATES` | `World templates <n>`, then `<name> <based on> <key=value...>` |
| `THINGS` | `World things <n>`, then `<i>: <template> <name> <x> <y> <z> <pitch> <yaw> <roll> <sector> <key=value...>` |

Every list's count is room for that many, not how many there are: a list
may end sooner, at a line `end` (`01narshadda.jkl`: `World models 122`
then 90 models, `World things 552` then 352 things). One line of
`static.jkl` has `5;` for `5:`.

### HEADER

`Version 1`; `World Gravity` (4.0); `Ceiling Sky Z`; `Horizon Distance`;
`Horizon Pixels per Rev`; `Horizon Sky Offset` and `Ceiling Sky Offset`
(two numbers each); `MipMap Distances` and `LOD Distances` (four each);
`Perspective distance`; `Gouraud distance`.

### GEORESOURCE

In this order:

| Subsection | Lines |
|---|---|
| `World Colormaps <n>` | `<i>: <file.cmp>`: 1 or 2; a sector names one ([cmp.md](cmp.md)) |
| `World vertices <n>` | `<i>: <x> <y> <z>` |
| `World texture vertices <n>` | `<i>: <u> <v>`, in texels of the surface's material |
| `World adjoins <n>` | `<i>: <flags> <mirror> <dist>` |
| `World surfaces <n>` | `<i>: <mat> <surfflags> <faceflags> <geo> <light> <tex> <adjoin> <extralight> <nverts>`, then each corner `<vertex>,<texture vertex>` (-1 for none), then each corner's light |
| (none) | A normal for each surface, `<i>: <x> <y> <z>`, after a comment `#--- Surface normals ---` |

A surface's material is -1, and its geometry mode 0, for an open adjoin:
35,406 of the 126,639 surfaces. Geometry mode 4 is textured, 3 a plain
colour (666 surfaces). The corners' light runs from 0 to 100; 14% are
above 1.

Surfaces with flags `0x200` and `0x400` are the skies: in every level
with one, its sky materials (`00starry.mat`, `03sky09k.mat`, `09skyb.mat`,
`14sky.mat`...) are on surfaces with one of them, and hardly another
surface is. They would be the horizon and ceiling skies the header's
settings place *(inferred)*.

### SECTORS

Each starts `SECTOR <i>`, then:

| Line | What |
|---|---|
| `FLAGS <hex>` | |
| `AMBIENT LIGHT <l>`, `EXTRA LIGHT <l>` | |
| `COLORMAP <i>` | Into the level's colormaps |
| `TINT <r> <g> <b>` | |
| `BOUNDBOX <6 numbers>` | The lowest corner, then the highest |
| `COLLIDEBOX <6 numbers>` | The same; in 5,438 of the 12,415 sectors |
| `SOUND <file.wav> <volume>` | Its ambient sound; in 3,650 |
| `CENTER <x> <y> <z>`, `RADIUS <r>` | |
| `VERTICES <n>` | Then `<i>: <vertex>`: the level's vertices its surfaces use |
| `SURFACES <first> <count>` | Its surfaces: a run of the level's |

### How it holds together

In all 34 levels with geometry:

- the sectors' surface runs follow each other, and cover each of the
  level's surfaces once;
- adjoins come in pairs, each the other's mirror;
- each adjoin belongs to one surface.

### Templates and things

A template's settings are `key=value` words (`type=player`,
`model3d=ky.3do`, `puppet=ky.pup`, `cog=kyle.cog`...), over those of the
template it's based on (`none` for none). A thing's own settings, after
its sector, are over its template's (a door's `numframes=2` and its
`frame=(x/y/z:pitch/yaw/roll)` positions). The player is the thing of
`type=player`.

## Behaviour

A surface faces into its sector, as its normal says: from behind, it
isn't seen *(inferred)*. Its texture's texels are its texture vertices',
which run past the texture's edges to repeat it. Its corners' light,
plus its sector's and its own extra light, picks a light level of the
sector's colormap *(guessed: how the game uses light above 1, and the
ambient light, isn't traced)*.

## In Reluva

`parseLevel` (`src/engines/sith/formats/level.h`) reads one into a
`Level`; `Level::setting` looks a thing's setting up through its
template's bases. The data viewer's Levels page draws a level from the
player's start, through each sector's colormap, or from above (V), the
surfaces facing away from the camera left out. `reluva-tool sith formats
<install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 35 files, every line of them, and
  their column comments; the levels drawn in the data viewer (Katarn's
  house, the fuel station, Nar Shaddaa's streets, each recognisable).
- **How it was checked:** every level read by `reluva-tool sith
  formats`; the sectors, adjoins and surfaces checked against each other
  as above; `sith.unit.levels` on a level made by the test, with the
  quirks above.

## Open questions

- The flags of the adjoins, surfaces, faces and sectors but the sky's,
  the tint, and how the game lights a surface from the light values.
- What the game makes of `5;`.
