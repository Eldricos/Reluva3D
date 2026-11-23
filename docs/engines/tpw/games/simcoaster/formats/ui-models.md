# SimCoaster: the interface's flat models (`ui/*.MD2`)

> **Status:** partial · **Code:** `src/engines/tpw/ui/ui_model.cpp` · **Checked against:** the 172 models of `ui.wad` and a recording · **Confidence:** inferred unless marked

## Overview

The game's windows, tabs and buttons aren't pictures: they are models
([md2.md](md2.md)), flat, in `ui.wad`: `!window_s.MD2` a window,
`filebut.MD2` a button of the Personnel File, `filetab.MD2` its tabs.
Each is a template the game stretches to the size it wants, and holds,
as separate meshes, the states it draws in.

## Layout

### The screen they're made on

The models lie in the plane z = 0 (or a constant), in a space of 2048 ×
1536 units, x to the right from 0, y down from 0 to −1536: a 4:3 screen.
At 640 × 480, 3.2 units are a pixel; and their textures come out a
texel a pixel: every edge of the templates below is 3.2 units a texel of
its texture (`!window`'s corners 99.2 units for 31 texels, `filepage`'s
51.2 for 16, `filetab`'s 25.6 for 8, `!frame`'s 32 for 10).

Some sit where the game draws them: `filepage.MD2`, the Personnel File's
page, is at (122, 99) to (528, 380) on a 640 × 480 screen, where the
recording shows it to the pixel, its ruled lines at the recording's; the
first of `filetab.MD2`'s tabs is at the recording's first tab's corner.
Most don't: they're laid out in rows and columns for the artist, their
states one beside another.

### Stretching

A template is a grid of quads: four columns of positions and four rows
for a window or a framed button (`!window_s`, `!cbut`, `!frame`, 9
quads), fewer for others (`filetab`, `filebut`, 3). Its edges are as
wide as its texture's edges; the game draws it at any size, the edges
kept, the middle stretched *(inferred: drawn so, the Personnel File's
window, tabs and buttons match the recording)*.

### States

A control's model has a mesh for each state, in the order of
`b_okay.MD2`'s, which names them: `b_okay` (as it is), `disabled`,
`hilite` (under the mouse), `hidown`, `helddown` and `down`. Their
textures' names end the same way: none, `g`, `h`, `hd`, then `d`
(`b_okayg`, `b_okayh`, `b_okayhd`, `b_okayd`). Some models use fewer
textures for their six meshes: `filebut`'s are `filebut`, `filebut`,
`filebuth`, `filebutd`, `filebutd`, `filebutd`; `filetab`'s `filetab`
three times, then `filetabd` (the player shown, light; the others grey).

### The Personnel File's models

| Model | Texture | What |
|---|---|---|
| `!window_s` | `newpanel1s` | The window, with its shadow, 8 pixels to the left and below; `!window` without |
| `filepage` | `filepage`, `filepage2` | The page and its ruled lines, 16 pixels apart |
| `filetab` | `filetab`, `filetabd` | A player's tab |
| `filebut` | `filebut`, `filebuth`, `filebutd` | "Enter Game", "Load A Game", "Delete Player" |
| `!cbut` | `b_header` | "Quit Game", "Coaster Design Kit": blue buttons |
| `!frame` | `f_frame` | A plate with a light blue rim, its middle opaque black: behind the portrait and the blue buttons |
| `mugshot` | `mugshot1` to `mugshot8` | The board's portraits, one a mesh: the first the advisor, over the front end; the fifth the woman of the Personnel File |
| `b_skip`, `b_play` | `b_skip`, `b_play` and their states | The front end's round button: skip, then play ([front-end.md](../systems/front-end.md#the-portrait-and-the-button)) |
| `filebox` | `filebox` | The box a new player's name is typed in |
| `filebutok` | `filebutok`, `filebutokg`, `filebutokh`, `filebutokd` | Its tick: grey (disabled) until a name is typed, then green |
| `!window` | `newpanel1` | The delete dialog: the window without its shadow |
| `b_okay`, `mb_cancel`, `b_close` | `b_okay`, `mb_cancel`, `b_close` and their states | The dialog's tick, cross and close button, round |

### The park's models

| Model | What |
|---|---|
| `happyguage` | The thermometer of the park's mood, full |
| `face` | Its faces, `face1` to `face6`: unhappiest to happiest, then the red no-entry sign |
| `comp` | The compass's plate, clover-shaped |
| `comp_rotatel`, `comp_rotater`, `comp_zoomin`, `comp_zoomout` | Its four buttons, each a quarter of it, in the six states |
| `compgizn`, `compgizn2` | The compass's dial (N, E, S, W, a red needle north); a small green arrow |
| `compgiz` | The compass whole, five times: its states, it seems *(guessed)* |
| `datebox` | The date's box: its black field and a round end |
| `toolbox`, `tb_pointer`, `tb_path`, `tb_delete`, `tb_camera`, `tb_postcard` | The bar down the left and its tools, in the six states |
| `gizmo`, `giz_message`, `giz_attract`, `giz_staff`, `giz_visitors`, `giz_finance`, `giz_park`, `giz_maps` | The bar along the bottom and its buttons, in the six states |
| `researchbulb` | A light bulb, three times: the research's, under the date while research is going (15:00, "9%"; 20:00, "33%") *(inferred)* |

These, like the states, are laid out beside each other, or about (0, 0)
(the compass's); the park's screen places them
([park.md](../systems/park.md#the-hud)).

The textures' alpha is drawn: the window's glass is see-through
(`newpanel1s`' middle at alpha 0xE2), its shadow half black.

## In Reluva

`drawUiMesh` (`src/engines/tpw/ui/ui_model.cpp`) draws a mesh of a UI
model at its place, or stretched into a rectangle, its textures' alpha
blended, v running up the texture as on the other models. The
Personnel File ([personnel-file.md](../systems/personnel-file.md)) is
drawn with it. The unit tests (`tpw.unit.ui`) stretch a mesh built in
the test.

## Sources and checks

- **Where the facts come from:** `ui.wad`'s models, their positions and
  texture coordinates; `b_okay.MD2`'s mesh names; the recording's
  Personnel File at 640 × 480.
- **How it was checked:** the Personnel File drawn from these set beside
  the recording's frame: the page's ruled lines within a pixel of the
  recording's, the window, tabs and buttons where it has them.

## Open questions

- How the game places the templates that aren't where it draws them:
  sizes and places from its program, most likely.
- The blue buttons are darker in the recording (about #00509B) than
  `b_header`'s blue (#1862B4): a tint or a light the game draws them
  with, not worked out.
- The models' other states: when a control is `disabled` or held down.
