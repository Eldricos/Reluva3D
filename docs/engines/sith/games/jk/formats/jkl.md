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
`0x4c739b`) *(from the code)*; seen running, below. That light times 63 is the light level
of the sector's colormap ([cmp.md](cmp.md#light-levels)), blended
across the surface. So a corner's light of 1 or more is full light, as
for the 14% above 1. When all its corners' lights are within 1/64 of
each other, the surface is drawn with the one light (`0x4c7449`)
*(from the code)*.

Light mode 0 draws the texels as they are, unlit; mode 1 by the
surface's extra light alone, held the same way (`0x43c59a`, `0x43c4dc`)
*(from the code)*. The surface's geometry, light and texture modes are
each held to the most the renderer's settings allow (`0x4c72b3` to
`0x4c72f9`) *(from the code)*: the texture mode's, below.

The `AMBIENT LIGHT` isn't used for surfaces: a sector's things are lit
by its `AMBIENT LIGHT` plus its `EXTRA LIGHT`, held to 0 to 1
(`0x4c7c2a`) *(from the code)*.

With a 3D card the game draws through Direct3D instead, in a renderer
of its own (`0x43b1c0`), used while `0x889ef8` is set (`0x43aea7`): it
was 1 with the game running through Direct3D and 0 in software
*(confirmed)*. That renderer works out a corner's light as above, held
to 0 to 1 and no less than the sector's `EXTRA LIGHT`, but times 255
(`0x43b71b` to `0x43b7af`), and makes it the corner's colour, the same
in red, green and blue (`0x43b9b4`, `0x43bb56`) *(from the code)*; the
card draws the texture's colours by it *(inferred)*. So a textured
surface's light isn't stepped in the colormap's 64 light levels. Light
mode 0 gives 255, unlit, and mode 1 the surface's extra light (`0x43b9c7`,
`0x43b829`) *(from the code)*. A surface of a plain colour (geometry
mode 3) still takes its colour through the colormap: the light times 63
picks the light level, and the palette gives the colour (`0x43bd0c`,
`0x43bf1a` to `0x43bf4b`) *(from the code)*. Either colour is then
multiplied by the colormap's three floats if the sector's colormap isn't
the current one ([cmp.md](cmp.md#behaviour)).

### Texture mode

The texture mode says how a textured surface's texels follow its
texture vertices across the screen, in the software renderer
(`0x43c3c0`: its texture mode held to the setting at `0x43c3eb`, the
way of drawing chosen by it at `0x43c765`) *(from the code)*:

| Mode | Drawn | Surfaces | Confidence |
|---|---|---|---|
| 0 | Affine: the texture's coordinates stepped evenly along each span, with no division (the span loops from `0x47dc40`, through `0x4500b0`), so a surface seen at a slant swims | 7: 3 untextured, 4 textured | from the code |
| 1 | Corrected for perspective: each span is cut into steps of 16 pixels (`0x4833e9` to `0x483451`), the texture's coordinates divided by the depth at each step's end and stepped evenly within it (the span loops from `0x480270`) | 126,617, all but 22 | from the code |
| 2 | As 1, the setting holding it to 1; were it not held, nothing would be drawn (`0x43c771`) | 15 | from the code |

The setting is *Setup*'s "Texture" detail (`GUI_TEXTURE`, "Set texture
mapping detail"): `Affine` holds every surface to mode 0 and `Perspect`
(the default, `0x401029`) to mode 1 (its items at `0x4154f0` and
`0x41550d`, the choice kept at `0x8606b4` and given to the renderer at
`0x401e8d`) *(from the code)*. Through Direct3D no read of a polygon's
texture mode was found: the card's texturing decides.

### Tint

Each frame, the tint of the sector the camera is in becomes the
player's screen tint, each of red, green and blue held to 0 to 1
(`0x4c6427`, `0x4c8d30`) *(from the code)*. The screen's tints are
added up (`0x428e7b`) and the palette changed by them: each colour's
red is multiplied by 1 + red − (green + blue) / 2, held to 0 to 255, and
its green and blue the same way (`0x4290e0`) *(from the code)*, rounded
to the nearest, a half up *(confirmed)*. The water's 0.4 0.8 1.0 makes
red 0.5 times, green 1.1 and blue 1.4: under the water in
`06abarons.jkl`, every colour of the HUD, drawn the same before and
after, became its colour so multiplied and rounded, 3788 pixels without
a difference.

With a 3D card there is no palette to change: each frame the screen's
effects are copied to the Direct3D renderer (`0x401f46`, `0x43abb0`)
while `0x8605ec` is set, which it was with the game running through
Direct3D *(confirmed)*. That renderer multiplies each corner's red by the
same 1 + red − (green + blue) / 2, and its green and blue the same way,
whenever a tint is above 0 (`0x43b209` to `0x43b296`, `0x43ba6d` to
`0x43baa1`); then by a fourth value the effects hold, when it's below 1
(1 to start with, `0x42886f`; `0x43baa4`); and holds each to 0 to 255
(`0x43bae7`) *(from the code)*.

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
| `0x100` | 336 | None found: no test of the bit on a surface's flags, and no cog sets, clears or tests it. The word is read whole only by the cog verb `GetSurfaceFlags` (`0x500570`) and copied into network play's message for a changed surface (`0x4f8f80`, `0x4f9050`). What it meant to the tool that wrote the files can't be told from the game, and changes nothing in it | from the code |
| `0x200` | 1,053 | Horizon sky: placed by the header's horizon settings, drawn unlit (`0x4c74fd`, `0x4f2e30`) | from the code |
| `0x400` | 686 | Ceiling sky: placed by the ceiling sky's (`0x4c750f`, `0x4f2f60`) | from the code |
| `0x800` | none | Scrolling: a thing standing on it moves with it (`0x4cc520`, `0x4ce4b8`); stopping the surface's animation clears it (`0x4efa2c`) | from the code |
| `0x1000` | 21 | Icy: a thing on it has 0.3 of its friction (`0x4f74e3`) | from the code |
| `0x2000` | 361 | Very icy: 0.1 (`0x4f74d6`) | from the code |
| `0x4000` | 3,608 | Deflects shots: a weapon whose `typeflags` has `0x400` bounces off it, playing its `deflected` sound, up to 6 times (`0x4d4ebf`, `0x4d4edc`, `0x4d4feb`); a weapon's `typeflags` are kept at its `0x130` (`0x4d43da`). In the files, the bolts of the bryar, crossbow and repeater and `+force_saber` have `0x400`. Mostly on metal (`13wbpipe.mat` 1,522, `00wbig4.mat` 379...); the force fields' cogs set it (`00_forcefield.cog`, `14_forcefield.cog`...) | from the code |
| `0x10000` | 142 | Metal: steps, landing and hits sound of metal (`0x4e4c09`, `0x4cdfed`, `0x4e8c09`) | from the code |
| `0x20000` | 4,116 | Water: steps and landing sound of water (`0x4e4c26`, `0x4cdff9`); halves a value of the player's movement, its speed *(inferred)* (`0x4d8aa2`) | from the code |
| `0x40000` | 57 | Puddle: steps and landing (`0x4e4c43`, `0x4ce005`) | from the code |
| `0x80000` | 540 | Earth: steps and landing (`0x4e4c60`, `0x4cdfe1`) | from the code |

A surface whose material is colours only loses `0x200` and `0x400`
and gets geometry mode 3; a sky gets light mode 0 (`0x4e5e91`,
`0x4e5eb6`) *(from the code)*. A weapon that hits a sky is removed
(`0x4d4e9d`) *(from the code)*.

### Skies

A sky's texture vertices aren't used: each frame the game works out a
texture place for each of its corners on the screen *(from the code)*.

- **The horizon sky** (`0x200`, `0x4f2e30`): the corner's place from the
  screen's centre, in pixels, times `Horizon Distance` divided by the
  view's focal length (`0x4f2dd0`), turned by the camera's roll; less the
  camera's yaw and pitch in degrees times `Horizon Pixels per Rev` / 360;
  plus `Horizon Sky Offset`, and the surface's own scroll. So the sky
  stays put as the camera moves, and slides a revolution's pixels as it
  turns once.
- **The ceiling sky** (`0x400`, `0x4f2f60`): where the ray from the
  camera through the corner meets the plane at `Ceiling Sky Z`, facing
  down, or 1,000 units along the ray if it never does (`0x508be0`); its x
  and y times 16 (`0x52316c`), plus `Ceiling Sky Offset` and the
  surface's scroll. So the ceiling sky is a flat plane overhead, 16
  texels a unit.

The header's three settings are kept at the world's `0xdc` to `0xe4`,
and its offsets at `0xe8` and `0xf0` (`0x4d0341` to `0x4d03f3`); the cog
verbs `SetHorizonSkyOffset` and `SlideHorizonSky`, and the ceiling's,
change the offsets as a level plays.

### Face flags

| Flag | Surfaces | What the game does | Confidence |
|---|---|---|---|
| `0x2` | 532 | See-through: drawn by routines of their own (`0x43c559`), or at an alpha of 90 of 255 through Direct3D (`0x43b428`; [Light](#light)). In the files, on water and glass (`13fwater.mat` 260, `00fwater.mat` 81...) | from the code |
| `0x4` | 34,598 | Cleared on loading, unless bit 0 is set at `0x8ee668` (`0x4e5e63`). That starts at 0 (`0x4c46d9`), and only a saved game's copy of it is written there (`0x4dac8f`): the game never sees the flag | from the code |

### Adjoin flags

| Flag | Adjoins | What the game does | Confidence |
|---|---|---|---|
| `0x1` | 36,336 | Seen through: the renderer goes on into the next sector only through an adjoin with `0x1` whose surface has no material, geometry mode 0, face flag `0x2`, or a texture with a transparent colour (`0x4c6a64` to `0x4c6a93`). A collision search made with `0x100` or `0x1000` passes only these (`0x4e87ea`; [Collision searches](#collision-searches)) | from the code |
| `0x2` | 35,958 | Passable: a search made without `0x100` passes only these (`0x4e880e`), and collision looks into the next sector only through them (`0x4e836f`) | from the code |
| `0x10` | 154 | Stops a search made with `0x200` and not `0x100` (`0x4e87f7`): shots, and the camera's views from outside ([Collision searches](#collision-searches)). 143 of the 154 have no material | from the code |
| `0x80` | none | Set on loading on an adjoin whose surface can't be seen through, by the renderer's rule (`0x4e5f40`) | from the code |
| `0x4`, `0x8` | 36,357; 89 | None found: no test of either bit on an adjoin's flags. The word is read whole only by the cog verb `GetAdjoinFlags` (`0x500660`) and copied into network play's message for a changed surface (`0x4f8f80`, `0x4f9050`). Two cogs set and clear `0x8` with `0x2`, as 10, for a force field's "move" and "no move" (`10_ffieldswitch.cog`, `12_ffieldswitch.cog`), which `0x2` alone does. What either meant to the tool that wrote the files can't be told from the game, and changes nothing in it | from the code |

### Collision searches

A collision search (`0x4e8160`) follows a line from a sector into the
next through the adjoins its flags let it pass (`0x4e87dd` to
`0x4e8810`) *(from the code)*:

- with `0x4`, any adjoin;
- with `0x100` or `0x1000`, only adjoins with `0x1`;
- with `0x200` and not `0x100`, none with `0x10`;
- without `0x100`, only adjoins with `0x2`.

Each of the 21 calls of the search, all direct, was followed back to
the flags it passes *(from the code)*:

| Flags | Made by |
|---|---|
| `0x2122` | The line of sight from one thing to another (`0x4e7500`), for the things `FireProjectile` may aim at (`0x4d5dce`) *(inferred)*, an item the player touches (`0x4fbe2e`, in the item and player collision `0x4fbe10`) and the cog verb `HasLOS` (`0x505086`). One other caller asks for `0x2022` (`0x4fba6b`) |
| `0x102`, `0x2102` | The AI's checks (`0x4eb090`, `0x4eb300`), made by its instincts `follow`, `circlestrafe`, `blindfire`, `listen`, `randomturn`, `flee`, `withdraw`, `dodge` and `sensedanger`, and through `0x4ead60` and `0x4eaf40` by `lookfortarget`, `turretfire` and `saberfighting` |
| `0x103`, `0x12a` | Three routines nothing calls: no call of them, and no pointer to them in the program (`0x4e6fb0`, a line from the camera; `0x4e7310`; `0x4e73f0`) |
| A thing's physics flags | A weapon's move (`0x4d4742`): `0x200` in all 85 weapon templates in the files, none with `0x100` or `0x1000`; the cogs set no physics flag but `0x1` and `0x100000` |
| `0x2200` | The camera's views of modes 4, `0x20` and `0x40` (`0x4c56b0`, through `0x4c6050`), those other than the first person's *(inferred)* |
| `0x2802`, `0x2812` | The search for the floor under a thing (`0x4f5715`) |
| 0, 1, 2, 4, `0x40`, `0x44`, `0x482`, `0x2002` | The rest: things' moves, the sector a point is in (`0x4e71b0`), the AI's check of the floor ahead (`0x4eb4b0`), and others |

So no search is made with `0x1000`. Shots and the camera's outside views
stop at adjoins with `0x10`; the lines of sight and the AI's checks with
`0x100` go through any adjoin with `0x1`, passable or not.

## In Reluva

`parseLevel` (`src/engines/sith/formats/level.h`) reads one into a
`Level`; `Level::setting` looks a thing's setting up through its
template's bases. The data viewer's Levels page draws a level from the
player's start (its position, 0.04 higher, facing its yaw), as the game's
camera sees: 90 degrees across the view (`0x4c4deb`), the focal length
half the view's width over tan 45° (`0x4436ad` to `0x443721`) *(from the
code)*; through each sector's colormap, or from above (V), the
surfaces facing away from the camera left out, and the things with a
model or a sprite in their places (H hides them). `reluva-tool sith formats
<install>` reads them all.

The viewer lights a corner as the game's colormap renderer does
([Light](#light)): the more of the corner's light with the surface's,
and the sector's, each held to 0 to 1; skies and light mode 0 unlit;
light mode 1 by the surface's extra light alone. The skies are placed
as the game places them ([Skies](#skies)), the camera having no roll; see-through
faces are drawn at 90/255 after the rest, the farthest first; and the
sector the camera is in tints the view as the game tints its palette,
rounding as it does ([Tint](#tint)), the status line giving that sector's flags and tint. The
viewer textures every surface corrected for perspective at each pixel,
as the game's default does each 16 pixels: the four textured surfaces of
mode 0, which the game draws affine, differ a little at a slant
([Texture mode](#texture-mode)). The
other flags change how the game plays, not what it draws: M marks the
surfaces or sectors with each in turn (floors, skies, scrolling, ice, the
four step sounds, see-through faces, underwater sectors, pits...), and J
moves the camera to the next sector marked.

## Sources and checks

- **Where the facts come from:** the 35 files, every line of them, and
  their column comments; the levels drawn in the data viewer (Katarn's
  house, the fuel station, Nar Shaddaa's streets, each recognisable);
  `JK.EXE` (US retail), its machine code read for the flags, the light,
  the tint and the lists' numbers, at the addresses given: from each
  field's place in the loader to the code that tests it. The names of
  cog messages (`entered`, `sighted`...) and of sounds (`landmetal`...)
  are the program's own, by the numbers it gives them (`0x4de21d`, and
  its table of sound names at `0x54b408`), as are those of cog verbs
  (`HasLOS`, `FireProjectile`...: where each is registered, `0x501f61`,
  `0x505832`) and of the AI's instincts (registered from `0x5091b0`).
- **How it was checked:** every level read by `reluva-tool sith
  formats`; the sectors, adjoins and surfaces checked against each other
  as above; `sith.unit.levels` on a level made by the test, with the
  quirks above. What the code says of each flag was set against the
  files: the counts of each flag in all 34 levels with geometry; the
  sectors with `0x1000` against those with a `COLLIDEBOX`; the floors'
  normals; the materials on each flag's surfaces; the texture vertices'
  texels a unit by flag; the underwater sectors' tints.
- **The flags no code reads** were looked for in every `test`, `and` and
  `bt` of the program whose mask holds the bit, combined masks too, each
  traced back to the word it tests (a surface's flags are at its `0x10`,
  an adjoin's at its `0`); in the cog verbs that get, set and clear them
  (`0x5004b0` to `0x500693`) and every use of those in the 540 cogs; and
  where the words are copied whole. The weapons' `typeflags` and physics
  flags were read from every template in the files, through their bases.
- **Seen running:** the game's first frame of `01narshadda.jkl` from the
  player's start, drawn in software by `reluva-tool sith host`, and the
  viewer's from the same start: the same view, the game's weapon and the
  Rodian's animation aside, and the walls', bench's, ceiling's and
  grating's colours within 2 of 255 of each other. Under the water in
  `06abarons.jkl` (the game's `warp` cheat, `0x420000`, taking x, y and z,
  `0x53c95c`), the tint as above.
- **The Direct3D path** was told from the software one in the running
  game: `reluva-tool sith host` into the first level, with and without
  `-d3d`, reading `0x8605ec` and `0x889ef8` at the end.

## Open questions

None.
