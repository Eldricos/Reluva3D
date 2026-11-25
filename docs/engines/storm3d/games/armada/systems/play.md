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

### How a skirmish starts

LAUNCH on the Game Setup screen loads the map, then plays it: each
player's starting units as the game makes them, about their
`Camera_<n>` path's first point ([formats/bzn.md](../formats/bzn.md#starting-units)),
in their colour; the overview over the player's own; and the shroud
over what the player's units don't see. Run in the emulator, "The Very
First Map" opens with the player's starbase and two construction ships
before the lava moon.

### The interface

Over the map, the program draws its interface from the race's layout
([formats/gui-config.md](../formats/gui-config.md)). Along the bottom
are five panels:

- the minimap (0, 334);
- the speed rail (146, 327), over the ship's display (146, 367), black
  inside;
- the cinematic view (393, 356), a live 3D view of a ship, with COMM
  and MENU buttons;
- the control panel (524, 334), black inside.

Along the top right are the dilithium (395, 0), crew (477, 0) and
officers (559, 0). Each is drawn over the frame after the scene, from
256 × 256 textures (`Fguia04` to `Fguif04` for the Federation) at half
their size.

The counters in a skirmish start at the Game Setup screen's amounts:
3000 dilithium, 3500 crew. The officers read "used/most": the starting
units' `officerCost` added up, out of the setting plus their
`officerGain`. Two construction ships (2 each) and a starbase (which
adds 20) on 100 make the "4/120" of the original's frame. Its crew had
grown to 3569 by then.

The minimap shows the map's picture (`gminimap`) where the player's
units have seen, and the shroud's (`gshroud`) where they haven't, a
texel a cell. The circle the original's frame clears about the start is
the same as Reluva's, to the pixel.

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
- **The interface** (`Hud`, `play/hud.h`) is drawn over the map in
  play: the panels, the counters (blank in a campaign mission, whose
  script sets them) and the minimap's shroud. The cinematic view shows
  its panel's own picture, the race's emblem, as the layout has it for
  no view; the minimap has no units' icons yet, and the cursor is the
  system's.
- **A skirmish** (`MissionView::startSkirmish`) starts from the Game
  Setup screen's slots: each player's race (one left Random takes the
  races in turn by its slot *(guessed)*), their colour, and slot *n* on
  `Camera_<n+1>` *(guessed)*, as what Random Placement does isn't known.
  The scene draws each player's units in their own colour.

## Sources and checks

- **Where the facts come from:** the original's frames every 2 seconds
  of its clock past the mission panel; `Input.map` and `RTS_CFG.h`; the
  camera in the program's memory ([camera.md](camera.md)).
- **How it was checked:** `engine-shot` past the mission panel set
  beside the original's first frame of play: the planets and the grid
  where the game's are, behind its objectives and interface. Launched
  from the Game Setup screen, the skirmish beside the original's: its
  starbase, construction ships, the moon and the grid where the game's
  are. The Maps
  page drew the same images before and after its drawing moved into
  `MapScene`. The unit tests (`storm3d.unit.play`) check the keys and
  the camera's moves against the program's memory.

## Open questions

- The camera's exact rates: the key presses were 0.6 seconds long by
  the input's clock, and the frames fall between.
- How long the program waits on its first frame of play, and what the
  cut scene's camera follows.
- How Random sides and Random Placement choose.
- How fast the crew grows.
