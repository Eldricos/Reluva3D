# Star Trek: Armada: sprite tables and fonts (`.spr`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/sprite_table.cpp` · **Checked on:** the English retail 1.0 install's 24 tables · **Confidence:** layout confirmed; meanings inferred unless marked

## Overview

Every flat picture the game draws is a sprite: a named rectangle of a
texture ([textures.md](textures.md)). The 24 sprite tables in `Sprites/`
name them, with the animations that move them and the particle emitters
of the effects:

| Tables | Sprites | What |
|---|---|---|
| `sprites.spr` and the 14 it includes (`cursor.spr`, `explosion.spr`, `tex_anim.spr`...) | 213 | Cursors, effects, lights, planets, nebulae, the starfield; with 91 animations and 28 emitters |
| `gui_global.spr` (927), `gui_federation.spr`, `gui_klingon.spr`, `gui_romulan.spr`, `gui_borg.spr` | 1,218 | The interface, and each race's own panels |
| `font.spr`, `font_jag.spr`, `title.spr` | 1 each | The fonts ([Fonts](#fonts)) |

`BZONE.FNT` and `BZONEJ.FNT` in the install's folder aren't the game's
fonts: `Armada.exe` never names them. Their name suggests
Battlezone's, left behind *(guessed)*.

## Layout

Text, with `#` comments to the line's end. The first line is
`sprite_table`.

### Directives

| Line | Seen | Meaning |
|---|---|---|
| `@include <table>` | 16, in `sprites.spr`, `gui_global.spr` and `gui_map.spr` | Another table read in its place |
| `@reference=<n>`, `@referenceWidth=<n>`, `@referenceHeight=<n>` | 107 | The size the rectangles after it are measured against: the texture's own, by the tables' comments ("texture size in pixels from which the u,v,w, and h values were measured") |
| `@skip=off`, `@skip=(r,g,b)` | 9 | A colour key: pixels of that colour are see-through ("source colour keying", `cursor.spr`'s comment) |
| `@tmaterial=<name>` | 79 | How the sprites after it are drawn: `interface`, `additive`, `wormhole`... |

### Sprites

`<name> <texture> <u> <v> <w> <h> [@parameter...]`: the texture's name
without folder or extension, and the rectangle in the reference size.
Its parameters:

- `@anim=<animation>`, one or more.
- `@cursor`: the sprite is a mouse pointer.
- `@origin=(x,y)`: its hot spot.

### Animations

```text
@animation font_uv
offset 224 0.0 step
@keyframes
0	0
4	0
```

The line after `@animation` gives what it changes (`offset`, `width`,
`colour` or `draw`), how many keyframes, how long in seconds (0 for a
font's, chosen by character) and `step` or `linear`. Then either:

- **`@keyframes` and a row each**, numbers: an offset's U and V, a
  width, or a colour's time and RGB written as `0.5 (1,0.6,0.1)`.
  `animation.spr`'s `holdbeam` and `assimbore` have their rows with no
  `@keyframes` before them; Reluva reads them as keyframes all the same
  *(inferred)*.
- **`@auto=square`, `row` or `column`**, and `@keyframes` with no rows:
  "Automatically generate keyframes", by the tables' comments. The frames
  are cells the sprite's size, laid out along a row, down a column or in a
  square: `tex4x4`'s 16 frames make a 4 × 4 flipbook, and the textures'
  layouts match.

### Emitters and sprite nodes

`@emitter <name>`, then `key=value` lines (`type=spray`, `velocity=12`,
`sprite=rift`) to `@end_emitter`: the particles of the effects. 28, in
`emitter.spr`, which a model's emitter nodes name ([sod.md](sod.md)):

| Setting | | |
|---|---|---|
| `type` | 28 | `spray` (16), `cloud` (11) or `beam` (1) |
| `sprite` | 23 | The sprite node each particle is |
| `pixel` | 22 | A colour, `(0.9,0.9,0.2)`: perhaps for when sprites aren't drawn *(guessed)* |
| `lifespan` | 27 | A particle's life, in seconds |
| `velocity` | 28 | How fast a particle moves, in units a second *(inferred)* |
| `expansion_rate` | 28 | How fast it grows, of its size a second; below 0 it shrinks *(inferred)* |
| `firing_period` | 17 | A spray's time between particles, in seconds |
| `spread_angle` | 16 | A spray's spread about its axis, in degrees *(inferred)* |
| `particle_count`, `cloud_radius` | 11 | A cloud's particles, and how far out they are |

A particle plays its sprite's colour animation over its life: each
emitter's lifespan is its animation's length (`steammed` 1.999 seconds,
`steamfade2` 2.0; `plasmamed` 2.999, `fade3` 3.0; `contrail` 0.249,
`fadefast.25` 0.2499), and the animations fade to black, so a particle
is gone as it dies *(inferred)*. A spray fires along its node's front
axis, the third row of its transform: the engines' emitters, at the
ships' backs, point back, and the hulls' vents point out *(inferred)*.

The table's own headings sort them. "Damage emitters", venting plasma
(`plasmasml` to `plasmaxl`) and gas (`steamsml` to `steamxl`), hang in
the models under the ship's systems (`engines`, `life`), so they run
when that system is damaged *(inferred)*: 132 of the 157 emitter nodes
in `SOD/`. The rest run always: weapons' trails (`contrail`,
`rocket`), the special weapons' (`rift`, `riftcloud`, `override`,
`microorg`), and the Premonition gate's (`premgate`).
`@sprite_node` lines (183) are the sprites the models' sprite nodes draw
([sod.md](sod.md)), as the tables' own comment names their fields:

```text
# @sprite_node name sprite animation (width,height) (r,g,b)
@sprite_node ion1 ion1 red1.colour  (80,80) (.2,.1,.1) billboard
```

- **The name**, which a model's node has, or has with `_<number>` after
  it: `ion1_1` and `whitestrobe_1_2` draw `ion1` and `whitestrobe`. So
  2,208 of the models' 2,267 sprite nodes find theirs; the rest are
  markers (`assimbigftur1`) *(inferred)*.
- **The sprite**, and **an animation**: `const` (87) for none, a colour
  animation (`red1.colour`, `inoutpurple`), or another (`fire`, `strobe`).
- **The size** in the model's units, as half the width and height: an
  asteroid 1.5 to 4, a nebula's ion cloud 80. Half, because the planets'
  glows (below) are 495 for planets of about 450 from the centre (the
  mesh's name, `earth450`, and its size drawn agree): their soft discs
  show as the rims just outside the planets that the original draws
  only if they reach 495 from the centre *(inferred)*.
- **A colour**: RGB from 0 to 1.
- **`billboard`** (132): turned to face the camera; without it, the
  sprite lies as its node does *(inferred)*. One table writes
  `//billboard`, taken as the flag commented out.

A sprite node can also be an object's own: `planets.spr`'s "Nimbus for
planets" are named for the planets' definitions (`Mbarisa`, `Mearth`),
and no node of their models has those names, so the game puts them at
the object (its glow, an orange or blue rim round the planet). The Borg
collectives' (`bcolect1`) are the same. `MQonos`'s would be `Mquonos`,
spelt differently, so it has none *(inferred)*.

Three lines have slips the reader allows for: a bracket closed twice
(two) or left open (one).

### Fonts

A font is one sprite, `char`, whose texture is a sheet of characters:

```text
char	font	0	0	0	10	@anim=font_uv	@anim=font_w
```

Its two animations have a keyframe for each character from the space
(32) up: `font_uv` the character's place on the texture, `font_w` its
width. `font.spr` and `font_jag.spr` draw from `font.tga` (128 × 128, 11
pixels to a row, the characters in its alpha); `title.spr` from
`font256.tga`, measured as 128 and so twice the size. Each header says
224 keyframes; each width animation has 223, so the last character, 255,
has none.

## In Reluva

`SpriteTable` (`src/engines/storm3d/formats/sprite_table.h`) reads a
table and what it includes: sprites, animations with their keyframes,
emitters' settings, and the sprite nodes (`spriteNodeFor` finds a model
node's).

- **The data viewer's Sprites page** (`SpritePage`,
  `src/engines/storm3d/pages/sprite_page.h`) shows each sprite on its
  texture and enlarged, steps through its keyframes, and writes a line of
  text with a font's characters.
- `reluva-tool storm3d spr <game folder> <table.spr>`: a table's
  sprites, animations and emitters.
- `reluva-tool storm3d spr-check <game folder>`: reads every table, and
  reports lines not read, keyframe counts that differ from their headers,
  and animations no table has.

## Sources and checks

- **Where the facts come from:** the 24 tables, and the developers'
  comments in them; `Armada.exe`'s strings for the fonts it names.
- **How it was checked:** `spr-check` reads every line of all 24, every
  animation a sprite names is in a table, and the only counts that
  differ are the three fonts' widths. On the Sprites page the font writes
  text in the game's characters, a pointer's frames step along its row,
  and a flipbook's tenth frame is the third row's second cell. The unit
  tests (`storm3d.unit.spr`) cover each form, and a font's and a
  flipbook's frames.

## Open questions

- The materials; `pixel`, and what a `beam` emitter does.
- Whether a sprite node's own colour is used when its animation sets a
  colour: the Maps page takes the animation's *(guessed)*.
- Whether an animation's rows with no `@keyframes` count in the game.
