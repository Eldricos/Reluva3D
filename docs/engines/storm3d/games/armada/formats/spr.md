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
`sprite=rift`) to `@end_emitter`: the particles of the effects.
`@sprite_node` lines (183) place sprites in 3D; their fields aren't read
yet.

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
emitters' settings, and the sprite nodes' lines as written.

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

- The sprite nodes' fields, the emitters' settings, and the materials.
- Whether an animation's rows with no `@keyframes` count in the game.
