# Star Trek: Armada: a mission in play

> **Status:** partial · **Code:** `src/engines/storm3d/play/`, `src/engines/storm3d/scene/map_scene.cpp` · **Checked against:** the original running "Premonitions" in Reluva's x86 emulator, its frames · **Confidence:** seen, the camera's rates inferred

## Overview

When a mission has loaded, the program plays it: the map drawn in 3D,
seen from the overview camera ([camera.md](camera.md)), the interface
along the bottom, and the units moving as their orders and the rules
say. This page grows with phase 7 of the plan
([PLAN.md](../PLAN.md)); so far, the map is shown in play and the
camera moves.

## How it works

### How a campaign mission starts

Run past the mission panel's O.K. under the emulator, "Premonitions"
loads, then opens on a cut scene: the Enterprise and its escorts flying
past the planet, to `sounds\music\Mfed25.wav`. About 44 seconds of the
program's clock later, play begins: the map from the overview camera
over the mission's `Camera_1` path, 450 above the plane, with the
objectives over it, the interface along the bottom, and the dilithium,
crew and officers at the top right.

By then the mission's script has moved the player's ships: they're at
their `...START` paths, not where the map leaves them (the Enterprise's
`ENTSTART`, (5,749, 28, 1,039), not (5,571, 38, 237)). The scripts
(`missions\Federation1S.dsl` and `R.drl`) are Windows DLLs, run by the
program; see the plan's phase 8.

### The camera's keys

`Input.map` gives the overview camera its keys: the keypad's + and −
(`zoom_factor_plus` and `_minus`), its / and * (`camera_rotate_minus`
and `_plus`), and the arrows (`scroll_left` and the rest), each with
no Control, Shift or Alt. Held, from the camera in the program's memory
before and after each ([camera.md](camera.md#the-overview)):

- **Zoom:** + held 0.6 seconds took the height from 450 to 333.4, which
  is 450 × e^−0.3, so it zooms by e^0.5 a second *(inferred)*. The
  height stays within 10 and 700 (`OVERVIEW_MIN_HEIGHT`,
  `OVERVIEW_MAX_HEIGHT`).
- **Turning:** * held 0.6 seconds turned it 17.3° to a smaller angle,
  about half a radian a second *(inferred)*.
- **Scrolling:** an arrow held 0.6 seconds at 333.4 high moved the
  target 908 along the camera's right. Six (`KEYBOARD_SCROLL_RATE`)
  times the camera's distance behind the target a second is 930
  *(inferred)*.

## In Reluva

- **The map** is drawn by `MapScene` (`scene/map_scene.h`): its
  objects, background, lights, grid and shroud as the viewer's Maps page
  has drawn them since phase 5
  ([formats/bzn.md](../formats/bzn.md)), now shared by the page and
  the game.
- **The mission** (`MissionView`, `play/mission_view.h`) follows the
  loading screen: the map from the overview over `Camera_1`, moved by
  the keys as above. A campaign mission's objects are where the map
  leaves them, as its script can't run yet, and without the shroud,
  which they'd cast from the wrong places. Esc goes back to the
  single-player screen. The interface and the objectives aren't drawn
  yet.

## Sources and checks

- **Where the facts come from:** the original's frames every 2 seconds
  of its clock past the mission panel; `Input.map` and `RTS_CFG.h`; the
  camera in the program's memory ([camera.md](camera.md)).
- **How it was checked:** `engine-shot` past the mission panel set
  beside the original's first frame of play: the planets and the grid
  where the game's are, behind its objectives and interface. The Maps
  page drew the same images before and after its drawing moved into
  `MapScene`. The unit tests (`storm3d.unit.play`) check the keys and
  the camera's moves against the program's memory.

## Open questions

- The camera's exact rates: the key presses were 0.6 seconds long by
  the input's clock, and the frames fall between.
- How long the program waits on its first frame of play, and what the
  cut scene's camera follows.
