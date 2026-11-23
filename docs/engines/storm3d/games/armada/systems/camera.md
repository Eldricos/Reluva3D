# Star Trek: Armada: the camera

> **Status:** partial · **Code:** `src/engines/storm3d/pages/map_page.cpp` · **Checked against:** the original running in Reluva's x86 emulator, its memory and its frames · **Confidence:** confirmed unless marked

## Overview

The game looks at its maps through one camera: during a mission's
scenes it follows the script's paths, and in play it's the overview, a
view from above and behind that the player moves, turns and zooms
(`RTS_CFG.h`'s `OVERVIEW_` settings). Storm3D transforms its vertices
itself, so the camera never reaches Direct3D
([research/startup-trace.md](../research/startup-trace.md#how-it-draws)):
it was found in the program's memory.

## How it works

### The camera in memory

Saved at two frames of "Premonitions"' opening scene (the host's
`-dump`), the program's memory holds the camera it's drawing with as 12
floats: its right, up and forward axes, then its position, in the
game's left-handed coordinates ([formats/sod.md](../formats/sod.md)).
Between frames 6,000 and 6,100 it climbed from (5,771, 221, 813) to
(5,774, 330, 747), along the last stretch of the mission's `ZOOMOUT`
path, (5,772, 255, 792) to (5,775, 450, 678). Its inverse follows it,
then 0.75 (the screen's height over its width) and 0.41421 twice:
tan 22.5°.

### The field of view

45° from top to bottom. Drawn from frame 6,100's camera at 45°, the
planet Barisa and the blue planet beside it land where the original's
frame has them, and as large; at 45° across (34.5° from top to bottom)
both are too large.

### The overview

Elsewhere in memory is the overview camera: its axes and position, then
its height, its distance from the point on the plane under its target,
the angle it looks down at that point, the angle it's turned, 200
(`OVERVIEW_PARABOLA_FOCUS`), the target, and the way it faces on the
plane. Saved before and after each of the camera's keys in play (the
keypad's + and −, its *, a grey arrow: `Input.map`'s `zoom_factor_plus`,
`zoom_factor_minus`, `camera_rotate_plus`, `scroll_right`), it settles:

| | Height | Behind the target | Down | Turned |
|---|---|---|---|---|
| On starting | 450 | 300 | 56.3° | 0 |
| + held 0.6 s | 333.4 | 258.2 | 52.2° | 0 |
| * held 0.6 s | 333.4 | 258.2 | 52.2° | −17.3° |
| → held 0.6 s, the target moved 908 | 333.4 | 258.2 | 52.2° | −17.3° |
| − held 2 s | 700 | 374.2 | 61.9° | −17.3° |

- **The height** is above the plane, whatever the target's: 450 on
  starting (`OVERVIEW_INIT_HEIGHT`), and − stops at 700
  (`OVERVIEW_MAX_HEIGHT`). The target is on the plane, at y 38.5 in
  "Premonitions".
- **On a parabola:** the camera is √(200 × height) behind its target, at
  each height: 300, 258.2, 374.2. So the higher it is, the steeper it
  looks down.
- **Turned** by a, it faces (sin a, cos a) on the plane, and sits behind
  the target that way; 0 faces +z (`OVERVIEW_INIT_ROTATION`).
- **It looks** straight at its target.
- **Scrolling** moves the target along the camera's right, which in the
  game's left-handed coordinates is (cos a, −sin a): (0.955, 0.297)
  turned −17.3°.

On starting, in "Premonitions", it's over the mission's `Camera_1` path
point (5,751, 40, 1,043), at (5,751, 450, 743). Drawn from there, the
map is as the original's first frame of play shows it behind the
objectives.

## In Reluva

- The data viewer's Maps page sees 45° from top to bottom. G looks as
  the overview starts, at whatever the view is on (P moves it to a
  path: "Premonitions"' `Camera_1` gives the original's first frame of
  play); then + and − move it along the parabola and W/A/S/D over the
  map, as the game's do. `MapPage::overviewOf` is the rule.
- `reluva-tool storm3d map-shot <game folder> <map> <out.png> <eye x y
  z> <forward x y z> [degrees]` draws a map from a camera given exactly,
  at the game's 640 × 480, to set beside the original's frames.

## Sources and checks

- **Where the facts come from:** the original's memory at two frames
  (`reluva-tool storm3d host … -dump <frame> <file>`), searched for 12
  floats of three perpendicular unit axes and a position on the map;
  `RTS_CFG.h`'s settings; the mission's paths
  ([formats/bzn.md](../formats/bzn.md#paths)).
- **How it was checked:** `map-shot` from frame 6,100's camera and from
  the overview's, set beside the original's frames 6,100 and 6,200: the
  planets in the same places and sizes. The ships differ, as the script
  has moved them from where the map starts them. The unit test
  (`storm3d.unit.bzn`, "overview") gives the overview's camera as memory
  held it, on starting, zoomed in, and zoomed out and turned. Further
  from the start the original shows only its shroud, the grey of what
  hasn't been explored, so those views can't be set beside its frames.

## Open questions

- How fast the keys move it: + for 0.6 s took the height from 450 to
  333.4, * turned it 0.302 radians, → moved the target 908. How these
  follow from `RTS_CFG.h`'s `ZOOM_SCROLL_COEFFICIENT`,
  `ROTATION_SCROLL_COEFFICIENT` and `KEYBOARD_SCROLL_RATE` isn't known,
  nor what `OVERVIEW_*_ZOOM` does.
- The clipping planes, and how the background model is placed.
- The planets' glow, which the original draws and the Maps page doesn't.
