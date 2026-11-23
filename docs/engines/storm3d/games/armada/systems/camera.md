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

### The overview on starting

Elsewhere in memory, unchanged between the two frames, the camera play
will start with: (5,751, 450, 743), looking at the mission's `Camera_1`
path point (5,751, 40, 1,043), with 450 and 540.833 after it. So:

- 450 above the plane: `OVERVIEW_INIT_HEIGHT`.
- 300 behind its target in z, facing +z: `OVERVIEW_INIT_ROTATION` is 0.
- Looking straight at the target; 540.833 is its distance to the point
  under the target on the plane, √(450² + 300²).

Drawn from there, the map is as the original's first frame of play
shows it behind the objectives. 300 is 450 over `OVERVIEW_INIT_ZOOM`'s
1.5, which may be the rule, from one case *(inferred)*.

## In Reluva

- The data viewer's Maps page sees 45° from top to bottom; G looks as
  the overview starts, at whatever the view is on (P moves it to a
  path: "Premonitions"' `Camera_1` gives the original's first frame of
  play). `MapPage::overviewOf` is the rule.
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
  (`storm3d.unit.bzn`, "overview") gives the overview's camera for
  `Camera_1` as memory held it.

## Open questions

- How zooming, turning and moving change the overview: the parabola of
  `OVERVIEW_PARABOLA_FOCUS`, the zoom's and height's ranges.
- The clipping planes, and how the background model is placed.
- The planets' glow, which the original draws and the Maps page doesn't.
