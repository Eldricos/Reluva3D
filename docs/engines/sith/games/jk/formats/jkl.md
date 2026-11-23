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
`static.jkl` has `5;` for `5:`, in its models. The game doesn't read
that number: it splits a line into words at spaces, tabs and commas
(`0x4315c0`), looks at the first word only to see whether it's `end`,
and puts the model the second names in the list's next free place
(`0x4e977b`, `0x4e98e7`) *(from the code)*. So `5;` reads as `5:`
would.

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
surface is. `0x200` is the horizon sky and `0x400` the ceiling sky,
placed by the header's settings for each
([Surface flags](#surface-flags)).

The flags are in hexadecimal (`%x`: `0x4e5cc2`, `0x4e5e2c`, `0x4e5e4c`)
*(from the code)*; what each does is in [Behaviour](#behaviour).

### SECTORS

Each starts `SECTOR <i>`, then:

| Line | What |
|---|---|
| `FLAGS <hex>` | [Sector flags](#sector-flags) |
| `AMBIENT LIGHT <l>`, `EXTRA LIGHT <l>` | What lights its things, and its surfaces ([Light](#light)) |
| `COLORMAP <i>` | Into the level's colormaps |
| `TINT <r> <g> <b>` | The screen's tint while the camera is in it ([Tint](#tint)) |
| `BOUNDBOX <6 numbers>` | The lowest corner, then the highest |
| `COLLIDEBOX <6 numbers>` | The same; in 5,438 of the 12,415 sectors, those with flag `0x1000` |
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
`type=player`. 2,386 template lines name a model (`model3d=`), 743 a
sprite (`sprite=`).

A thing's model is placed with its origin at the thing's position,
turned by its pitch, yaw and roll as a model's nodes are
([3do.md](3do.md#behaviour)): crates and fences stand on the ground that
way, without the model's insertion offset, as drawn in the data viewer.

## Behaviour

A surface faces into its sector, as its normal says: from behind, it
isn't seen *(inferred)*. Its texture's texels are its texture vertices',
which run past the texture's edges to repeat it. A surface of geometry
mode 0 isn't drawn (`0x4c6da6`) *(from the code)*.

What follows was read from `JK.EXE`'s code, and isn't checked against
the game running.

### Light

In the renderer that draws through the colormaps (`0x43c3c0`), a
surface of light mode 3, as all but 15 are, gets a light at each corner:
the corner's light plus the surface's extra light, held to 0 to 1; or
the sector's `EXTRA LIGHT`, held to 0 to 1, if that's more (`0x43c693`,
`0x4c739b`) *(from the code)*. That light times 63 is the light level
of the sector's colormap ([cmp.md](cmp.md#light-levels)), blended
across the surface. So a corner's light of 1 or more is full light, as
for the 14% above 1. When all its corners' lights are within 1/64 of
each other, the surface is drawn with the one light (`0x4c7449`)
*(from the code)*.

Light mode 0 draws the texels as they are, unlit; mode 1 by the
surface's extra light alone, held the same way (`0x43c59a`, `0x43c4dc`)
*(from the code)*. The surface's geometry, light and texture modes are
each held to the most the renderer's settings allow (`0x4c72b3` to
`0x4c72f9`) *(from the code)*.

The `AMBIENT LIGHT` isn't used for surfaces: a sector's things are lit
by its `AMBIENT LIGHT` plus its `EXTRA LIGHT`, held to 0 to 1
(`0x4c7c2a`) *(from the code)*.

### Tint

Each frame, the tint of the sector the camera is in becomes the
player's screen tint, each of red, green and blue held to 0 to 1
(`0x4c6427`, `0x4c8d30`) *(from the code)*. The screen's tints are
added up (`0x428e7b`) and the palette changed by them: each colour's
red is multiplied by 1 + red − (green + blue) / 2, held to 0 to 255, and
its green and blue the same way (`0x4290e0`) *(from the code)*. The
water's 0.4 0.8 1.0 makes red 0.5 times, green 1.1 and blue 1.4.

In the files, every underwater sector (flag `0x2`) has a tint, 390 of
the 461 that one; 143 other sectors have one, 128 of them red (1 0 0).

### Sector flags

Read as hexadecimal (`0x4f8827`) *(from the code)*. Of the 12,415
sectors:

| Flag | Sectors | What the game does | Confidence |
|---|---|---|---|
| `0x1` | none | No gravity: things in it aren't pulled down (`0x4f66de`) | from the code |
| `0x2` | 461, 14 levels | Underwater: its things move by swimming (`0x4f5972`); a thing in it more than 20 seconds takes 10 damage every 2 seconds (`0x4ecc0d`); the view sways while the camera is in it (`0x4c6446`). Each has a tint ([Tint](#tint)) | from the code |
| `0x4` | 782, 27 levels | Its cog is sent `entered` and `exited` as things come and go (`0x4cd344`, `0x4cd22d`), and `sighted` when it's first drawn (`0x4c669d`) | from the code |
| `0x8` | none | Its things are pushed by its thrust, a vector the file doesn't hold (`0x4f6682`) | from the code |
| `0x10` | 712, 19 levels | Left off the map (`0x4d93a6`) | from the code |
| `0x40` | 199, 15 levels | A pit: a thing falling in it faster than 3 units a second, on nothing, is marked as falling to its death, then killed (`0x4c8b9f`, `0x4c8bfe`). The code is the player's *(inferred)* | from the code |
| `0x1000` | 5,438, 34 levels | It has a `COLLIDEBOX`: reading one sets it (`0x4f89af`). Collision skips the sector's surfaces for a thing wholly inside the box (`0x4e8718`, `0x507f3e`) | from the code; the counts agree |
| `0x4000` | none | Seen: set when the renderer first draws the sector (`0x4c669a`); the map shows only seen sectors, unless set to show all (`0x4d9516`) | from the code |

### Surface flags

Of the 126,639 surfaces:

| Flag | Surfaces | What the game does | Confidence |
|---|---|---|---|
| `0x1` | 16,963 | Floor: the search for the floor under a thing finds only these (`0x4e89e1`, `0x4f5656`). All but 47 face up | from the code |
| `0x2` | 1,325 | Its cog is sent `entered` when a thing lands on it, `exited` when it leaves, `crossed` when one goes through its adjoin, and `touched` when one hits it, at most each half second (`0x4cdf4b`, `0x4ce50b`, `0x4e7dc5`, `0x4e8ba7`) | from the code |
| `0x4` | 90,681 | Solid: collision tests a surface with no adjoin only if it has `0x4` (`0x4e87c4`). Every surface without an adjoin has it | from the code |
| `0x8` | 285 | No floor for walking: the check that a thing's next step has floor under it finds none here (`0x4eb580`, `0x4eb6d7`). The AI makes that check (`0x50ad1d`) *(inferred)* | from the code |
| `0x10`, `0x20`, `0x40` | 4,143; 5,967; 497 | The texture's scale: their texture vertices are 640, 160 and 40 texels a unit, against 320 for the others. No code was found to read them | inferred |
| `0x80` | 62 | No damage on impact: else a thing hitting it at over 2.5 units a second takes (speed − 2.5)² × 45 (`0x4e8b7d`, `0x4e949f`) | from the code |
| `0x200` | 1,053 | Horizon sky: placed by the header's horizon settings, drawn unlit (`0x4c74fd`, `0x4f2e30`) | from the code |
| `0x400` | 686 | Ceiling sky: placed by the ceiling sky's (`0x4c750f`, `0x4f2f60`) | from the code |
| `0x800` | none | Scrolling: a thing standing on it moves with it (`0x4cc520`, `0x4ce4b8`); stopping the surface's animation clears it (`0x4efa2c`) | from the code |
| `0x1000` | 21 | Icy: a thing on it has 0.3 of its friction (`0x4f74e3`) | from the code |
| `0x2000` | 361 | Very icy: 0.1 (`0x4f74d6`) | from the code |
| `0x10000` | 142 | Metal: steps, landing and hits sound of metal (`0x4e4c09`, `0x4cdfed`, `0x4e8c09`) | from the code |
| `0x20000` | 4,116 | Water: steps and landing sound of water (`0x4e4c26`, `0x4cdff9`); halves a value of the player's movement, its speed *(inferred)* (`0x4d8aa2`) | from the code |
| `0x40000` | 57 | Puddle: steps and landing (`0x4e4c43`, `0x4ce005`) | from the code |
| `0x80000` | 540 | Earth: steps and landing (`0x4e4c60`, `0x4cdfe1`) | from the code |
| `0x100`, `0x4000` | 336; 3,608 | No code was found to read them | |

A surface whose material is colours only loses `0x200` and `0x400`
and gets geometry mode 3; a sky gets light mode 0 (`0x4e5e91`,
`0x4e5eb6`) *(from the code)*. A weapon that hits a sky is removed
(`0x4d4e9d`) *(from the code)*.

### Face flags

| Flag | Surfaces | What the game does | Confidence |
|---|---|---|---|
| `0x2` | 532 | See-through: drawn by routines of their own (`0x43c559`), or at an alpha of 90 of 255 in the other drawing path (`0x43b428`). In the files, on water and glass (`13fwater.mat` 260, `00fwater.mat` 81...) | from the code |
| `0x4` | 34,598 | Cleared on loading, unless bit 0 is set at `0x8ee668` (`0x4e5e63`). That starts at 0 (`0x4c46d9`), and only a saved game's copy of it is written there (`0x4dac8f`): the game never sees the flag | from the code |

### Adjoin flags

| Flag | Adjoins | What the game does | Confidence |
|---|---|---|---|
| `0x1` | 36,336 | Seen through: the renderer goes on into the next sector only through an adjoin with `0x1` whose surface has no material, geometry mode 0, face flag `0x2`, or a texture with a transparent colour (`0x4c6a64` to `0x4c6a93`). A collision search made with `0x100` or `0x1000` passes only these (`0x4e87ea`) | from the code |
| `0x2` | 35,958 | Passable: a search made without `0x100` passes only these (`0x4e880e`), and collision looks into the next sector only through them (`0x4e836f`) | from the code |
| `0x10` | 154 | Stops a search made with `0x200` and not `0x100` (`0x4e87f7`). A weapon's search is made with its physics flags (`0x4d4742`), and `_weapon`'s are `0x200`: shots stop there *(inferred)*. 143 of the 154 have no material | from the code |
| `0x80` | none | Set on loading on an adjoin whose surface can't be seen through, by the renderer's rule (`0x4e5f40`) | from the code |
| `0x4`, `0x8` | 36,357; 89 | No code was found to read them | |

## In Reluva

`parseLevel` (`src/engines/sith/formats/level.h`) reads one into a
`Level`; `Level::setting` looks a thing's setting up through its
template's bases. The data viewer's Levels page draws a level from the
player's start, through each sector's colormap, or from above (V), the
surfaces facing away from the camera left out, and the things with a
model or a sprite in their places (H hides them). `reluva-tool sith formats
<install>` reads them all.

The viewer lights a corner by its light plus the sector's and the
surface's extra light, held to 0 to 1, where the game takes the more of
the corner's light with the surface's, and the sector's
([Light](#light)). It doesn't yet use the flags, the skies' placing, or
the tint.

## Sources and checks

- **Where the facts come from:** the 35 files, every line of them, and
  their column comments; the levels drawn in the data viewer (Katarn's
  house, the fuel station, Nar Shaddaa's streets, each recognisable);
  `JK.EXE` (US retail), its machine code read for the flags, the light,
  the tint and the lists' numbers, at the addresses given: from each
  field's place in the loader to the code that tests it. The names of
  cog messages (`entered`, `sighted`...) and of sounds (`landmetal`...)
  are the program's own, by the numbers it gives them (`0x4de21d`, and
  its table of sound names at `0x54b4c0`).
- **How it was checked:** every level read by `reluva-tool sith
  formats`; the sectors, adjoins and surfaces checked against each other
  as above; `sith.unit.levels` on a level made by the test, with the
  quirks above. What the code says of each flag was set against the
  files: the counts of each flag in all 34 levels with geometry; the
  sectors with `0x1000` against those with a `COLLIDEBOX`; the floors'
  normals; the materials on each flag's surfaces; the texture vertices'
  texels a unit by flag; the underwater sectors' tints.

## Open questions

- What the adjoin flags `0x4` and `0x8`, and the surface flags `0x100`
  and `0x4000`, do: all are in the files, but no code was found to read
  them.
- Which things make the collision searches with `0x100`, `0x200` and
  `0x1000` that the adjoin flags `0x1`, `0x2` and `0x10` sort; only the
  weapons' is traced.
- How the other drawing path, for a 3D card, lights a surface and shows
  the tint; and what the texture mode does.
