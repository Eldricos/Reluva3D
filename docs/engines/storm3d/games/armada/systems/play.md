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

### Selecting

The game draws its own cursor over the frame, not through Direct3D:
`cursor.spr`'s `c_arrow`, or `c_select` over a unit, from `curs_24`,
black its colour key, its tip at the pointer; its four frames step
along a row, a second for the four (`tex_anim.spr`'s `cursor4x1`). In
play its cursor follows the mouse's movement, not its place (see
[hosting.md](hosting.md)), so the host's runs click with its keys too:
`N` (`select_next_ship`) selects the next ship.

A unit selected gets two marks, from the original's draws of the
frame after a click on the starbase, and after `N`:

- **The ring:** `Gshadow.tga`, the texture in the program's memory
  that the draw used, on a square lying flat under the unit, added on
  in (229, 229, 229), turned a quarter. Placed back in the world, the
  starbase's square is 105.2 across and a construction ship's 54.5:
  each its model's box's diagonal across (x and z), twice. Both lie near
  the map's plane, at y 3.2 and 7.0.
- **The health bar:** the info panel's `large_shield_bar` sprite, 4
  high, in dark grey (51, 51, 51) and then in green (0, 128, 0) as far as
  its health. It spans the unit's box as it falls on the screen, all
  eight corners: the starbase's bar from 381 to 490, its box from 381.4
  to 489.4. Its top is 6 pixels above the box's top there.

### The selected unit's display

With one unit selected, the original drew its display over the info
panel, its orders on the control panel and its special weapons on the
rail above ([gui-config.md](../formats/gui-config.md) has where and in
what colours). What each shows, from its definition, as the starbase's
and a construction ship's draws have them:

- **Its words:** its `unitName` and a name of its `possibleCraftNames`,
  a different one each game.
- **Its crew:** `crewCost` of `maximumCrew`, "800/1500"; a ship without
  `maximumCrew` shows its `crewCost` twice, "500/500". Its officers,
  `officerCost`, on a line only when it costs some: the starbase has
  none.
- **Its systems:** an icon lit for each it has; the starbase's engines
  are dark. A system is lit when its `<system>CrewLoss` is more than 0,
  as the starbase's engines' 0 is the one it lacks *(inferred)*.
- **Its orders:** move, lit when it has engines (`enginesHitPoints`);
  attack, lit when it has a weapon without a button of its own; stop,
  orders and stand down, always lit. A ship has repair too, dark on the
  construction ship. The starbase has recrew, its race's officers'
  button (`b_fedoff`, with the border `bb_build`) and the rally point;
  a unit that builds (`buildItem0`) has the build button.
- **Its special weapons:** each weapon with a button sprite of its own,
  `b_<weapon>` (the construction ship's `gtracbm`, its tractor beam), on
  the rail.

Half a second after the starbase's, the original drew bars over the
two construction ships too, unselected. A ship chosen with `N` had its
ring but no bar in that frame, and neither had units a box selected.

### Selecting with a box

Dragged with the left button, the original draws the box over the map,
under the panels: four lines, white, a pixel wide, from a pixel up and
left of where the drag started to the pointer, within a pixel (its
pointer moves in half pixels). As it's dragged, what it holds is
selected already: the rings and the display change before the button
is let go, and stay as they are after.

What a box selects, from three drags in a skirmish's first minute:

- Around the starbase and both construction ships, the ships alone:
  the starbase was left out. Around the starbase alone, the starbase.
  So a box holding units that move keeps only those *(inferred)*.
- A unit is in the box when its middle falls in it on the screen
  *(guessed)*: each unit in these drags was well inside or outside.

The info panel shows a group as the layout's `infoMulti…` places say
([gui-config.md](../formats/gui-config.md)): each ship's wireframe,
its shield bar and its crew dot, side by side, without the single
display's words and systems. The control panel had the construction
ship's orders, but with build dark; the rail had the tractor beam once.

### The crew

In a skirmish the crew counter grows on its own. Read from the
original's frames every second of a skirmish's first minute, with the
player's one starbase: 3502 at 21.0 seconds of its clock, then 3 or 4
more each second, steadily, to 3607 at 51.0: 3.5 a second. The
starbase's own crew stayed 800 of 1500. `RTS_CFG.h` gives the most,
`cfgMaxCrew` (99999), but no rate. That it's the starbase's, a "Command
Center for additional personnel" as its tooltip says, and so 3.5 for
each, is *(guessed)*.

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
  no view; the minimap has no units' icons yet. In a skirmish the crew
  grows by 3.5 a second for each of the player's starbases.
- **Selecting:** a click picks the unit whose middle is nearest the
  pointer, of those whose box reaches it on the screen (the game's own
  choice isn't known); a click where there's none clears it, and clicks
  on the panels don't reach the map. The selected unit has its ring and
  its bar, full; the cursor is the game's. Its display, orders and
  special weapons are drawn as the original's, its name the first of
  its possible names counted on by its place in the scene, as the
  game's random choice isn't known. The orders' buttons don't do
  anything yet.
- **A box** drawn as the original's selects as it's dragged: the
  player's units whose middles it holds, only those that move if any
  do. A group shows each ship on the info panel, and its orders: each
  button any of them has, lit if any of them can, build dark
  *(guessed)*; their special weapons once each. A unit clicked on has
  its health bar; units a box selected don't.
- **Moving:** the right button sends the ships selected to a point, as
  [movement.md](movement.md) has it.
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
  the camera's moves against the program's memory, and the display's
  facts from the starbase's and construction ship's definitions. The
  starbase's and a construction ship's displays, drawn by `engine-shot`,
  were set beside the original's frames: the same in every part but
  their names, a few pixels' colours at the sprites' edges apart. Boxes
  dragged by `engine-shot` (`down`, `move`, `up`), set beside the
  original's: the box, the rings, the group's display, orders and rail
  where the original's are.

## Open questions

- The camera's exact rates: the key presses were 0.6 seconds long by
  the input's clock, and the frames fall between.
- How long the program waits on its first frame of play, and what the
  cut scene's camera follows.
- How Random sides and Random Placement choose.
- What makes the crew grow: one starbase gave 3.5 a second; more, or
  none, haven't been seen.
- When the health bars show: why the unselected ships had theirs, and
  the ship chosen with `N`, or a box, none yet.
- Which point of a unit the box has to hold, and how a group's orders
  are lit when its ships can do different things.
- How the info panel shows more than eight ships.
- What the ring's height is: near the plane for both, but not the same.
- When a ship's repair order is lit, and what the display shows of
  other ships and of a group.
