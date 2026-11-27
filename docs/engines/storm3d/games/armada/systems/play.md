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
- **When it shows** *(from the code)*, 0x496347 and 0x4977a6: over the
  unit under the pointer; and once the pointer has rested on one
  player's units, frame after frame, half a second, over every one of
  that player's units, fading in over the next half second, gone as soon
  as the pointer leaves them. Selection alone gives none: the original's
  starbase, selected, had none once the pointer was on the panel. Its
  other way to draw them all answers to debugging settings
  (`DEBUG_SHOW_OBJECT_BOUNDING_BOX` and the like).

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

### The minimap

Over the map's picture (`minimapBackgroundSprite`), the original's
draws put, in turn: a 128 × 128 layer added on, with the units in it;
a 5 × 5 sprite, blue, near the starting units: the dilithium moon's
mark, its definition's `mapIcon` (`map_dmoon`, of `gui_map.spr`), about
its place, added on, though the frame has it a row lower than the
draw's corners; the fog and the shroud; and four lines, added on
in (128, 128, 128), round where the camera looks.

- **The units** are in their player's colour: the player's, white,
  `teamcolor.odf`'s 0.9 (230). With the map's 6400 across the 128
  pixels, each construction ship is the pixel its place falls in and
  those up and to the left of it, 2 × 2, and the starbase the 3 × 3
  about its own, to the pixel in two frames, before and after a ship
  moved. Whether it goes by size or by kind is *(guessed)*: stations
  bigger.
- **The view:** its top edge is where the screen's top corners' rays
  meet the plane the camera looks at (y 38.5), to a tenth of a pixel.
  Its bottom edge is a pixel and a half higher, and wider, than the
  screen's bottom corners' would be, on any plane: how the program
  makes it isn't known.

A grey pixel (123, 125, 123) under the starbase's mark isn't
accounted for.

A left click on the minimap moves the camera there: clicked at about
(925, 575), the original's target was (845, 595) after, within the
pointer's error of a pixel or two on the minimap.

### Sight and the fog

The shroud covers what the player's units have never seen, the fog what
they don't see now ([formats/bzn.md](../formats/bzn.md) has their
textures and how they're drawn). The Game Setup screen's shroud list
turns each on or off.

- **Under the fog:** with the shroud off and the fog on, the camera
  taken to the computer player's base (`Camera_4`) showed none of it:
  not its starbase, its ships or its dilithium moon (`mdmoon`, whose
  class is `scrap`, below the plane). The planet there (`MQonos`,
  `planet_obj`) was drawn, under the fog, 70% of `gfog` over it. The
  minimap showed neither the computer's marks nor the moon's icon there.
- **The fog's texture,** read from memory a minute in, is `gfog`'s
  picture with the cells the player's units see now in black: 628 of
  them, as a ship had moved off a little.
- **The minimap's fog** is `minimapFogSprite`'s picture (`map_fog`,
  `gfog`'s) 70% over what's under it, in the cells not seen now: its
  pixels, set beside the frame without the fog, fit that to a unit or
  two. Under it the map's nebulae and asteroid fields still show, dimmed;
  its moons', wormholes' and black holes' marks don't.
- **The minimap's picture,** read from memory, is `gminimap` with a
  layer added on, `gui_map.spr`'s "minimap layers": a nebula
  (`mapSprite` `map_nebula3`, `mapSize` 5) shows its pattern, `gmneb3`,
  as large as the minimap, a texel a pixel, where a soft disc about it
  reaches: 0.87 of the texel within a pixel of its place, none past 3.6,
  the most of the discs where they overlap *(inferred: fitted to the
  layer)*. The asteroid fields' (`map_abelt`) fit neither that nor
  their discs added up. The map's `.bmp` beside its `.bzn` (128 square)
  is a finished picture of all of it, marks and all, for the setup
  screen *(inferred)*; the minimap isn't drawn from it.

### The cinematic view

The panel over the control panel's left shows the battle in 3D in its
display (`cinematicDisplayArea`, 113 × 80 at (402, 391)): the scene
drawn again through a camera of its own, background, planets and units,
without the grid. Its two buttons say "COMM" and "MENU", the program's
own words, in black, centred as the other text.

What it shows changes like a director's cuts, from the original's
frames every second:

- **At rest:** a steady close-up of one of the player's construction
  ships, the same in every run, from the start of play.
- **While a ship flies:** shots of it that cut every second or so, from
  behind, from its side, from further off, with the planets behind it.
- **After,** the starbase close up, though the ship was still selected.

Over a longer run (frames every two seconds, 42 to 250 seconds, a
shipyard and a Defiant built and the Defiant sent off):

- **With a unit selected,** it stays on it, cutting every few seconds
  between a close-up and a wider shot (the shipyard going up, 58 to 118
  seconds).
- **With none,** it goes from one of the player's units to another, the
  starbase, the shipyard, either construction ship, the Defiant, each
  for 2 to 6 seconds, in no order seen; the ship flying in wider shots.
- **In a fight,** the enemy starbase the Defiant fought, close up.

The events' `CinematicPriority` and `CinematicTime` in `events.odf`
(a base under attack's 5 for 10 seconds) are commented out, so the
retail game doesn't cut to them.

The steady shot, fitted to the original's draws: the construction
ship's hull in the panel, against the same hull placed back in the world
from the main view, aligned by its nearest points (no vertex is drawn
in both), comes within 0.7 units with a focal length of 80 pixels (53°
from top to bottom) and a camera 41.8 from the hull's middle, looking
at it, along (−0.38, 0.857, −0.348) from it with the ship facing +z:
above, behind and to its left, with the world's up. The starbase's shot
fitted less well (4.7 units): from its left, 54° down, 84 to 100 away.

### The crew

In a skirmish the crew counter grows on its own. Read from the
original's frames every second of a skirmish's first minute, with the
player's one starbase: 3502 at 21.0 seconds of its clock, then 3 or 4
more each second, steadily, to 3607 at 51.0: 3.5 a second. The
starbase's own crew stayed 800 of 1500. `RTS_CFG.h` gives the most,
`cfgMaxCrew` (99999), but no rate. That it's the starbase's, a "Command
Center for additional personnel" as its tooltip says, and so 3.5 for
each, is *(guessed)*.

### The menu, surrendering and the debriefing

The cinematic view's MENU button opens the game's menu, a dialog of the
program's own over the paused game: the Options screen
([front-end.md](front-end.md#the-options-screen)) with six buttons, 40
apart from y 152, `label.map`'s `escape_menu` words: Surrender, Graphics
Settings, Sound Settings, Game Settings, Abort Mission, Return to Game;
the bottom bar's three plates empty.

Surrender went straight to the debriefing, no word of defeat between,
within a second. Abort Mission asks first: a box over the menu
(`bitmaps/misc/DBsmall2BG.bmp` at (140, 150)), "Warning:" at (232, 151)
and "Are you sure you want to abort the mission?" centred on it, in the
debriefing's gold (`label.map`'s `abort_mission_popup`), Abort Mission
left pressed beneath; Yes on Previous Menu's plate (`navback`) at
(163, 250), No on the right one's (`navmain`) at (357, 250), their words
12 pixels. Yes went to the debriefing, as Surrender does; No puts the
box away. Over `bitmaps\credit\creditsBG.bmp`, its title
"DEBRIEFING" where the Options screen's is, it has a row for each
player and eight columns, `label.map`'s `multiplayer_debriefing` words
in Arial Bold, as measured on its frame:

- **The columns:** "Dilithium" (Collected, Spent), "Created", "Lost" and
  "Destroyed" (each Ships, Stations), in (255, 190, 0): the headings 16
  pixels, their lines' tops at y 64, centred over their two columns; the
  columns' words 12 pixels at y 80, centred on x 215, then each 48 on.
- **A row** for each player, 40 apart from y 100, in their colour (the
  player's white, 230; the computer's red): their name ("Player", the
  name typed; a computer's in the program's own words, "Computer
  Medium", not the setup's "Computer AI:Medium") and side
  ("Federation") on two lines 16 apart, centred on x 108, and their
  numbers, 16 pixels, at y 108, centred on their columns. All were 0
  in a game a minute old, its starting units not counted as created.
- **Continue** on the bottom bar's middle plate (`mpsstart`, lit under
  the pointer) went back to the Multiplayer Connection screen, and
  **Quit To Windows** is on the right one (`navmain`), its words wider
  than the plate.

"Allied Victory", on the communications panel, says an ally's victory
is enough to win (`Dynamic_Localized_Strings.h`), and the program has
"%s surrendered." and "eliminated or quit" among its words.

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
  no view. The minimap has the units and the view as above, the bottom
  of the view on the screen's bottom corners. The cinematic view shows
  the unit selected first, or the player's first ship, in the
  construction ship's steady shot: 1.53 of its ring's half side from its
  box's middle, turned with its heading *(inferred)*. With nothing
  selected it starts on the player's first ship, then moves to one of
  the player's units after another, chosen at random, each for 2 to 6
  seconds *(inferred)*, from the frames above; its wider shots, and the
  fight's, aren't made. In a skirmish the crew
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
  *(guessed)*; their special weapons once each.
- **Health bars** as the original's, above: the unit under the pointer's,
  and all of a player's once the pointer has rested on their units half
  a second, drawn whole rather than fading in.
- **Moving:** the right button sends the ships selected to a point, as
  [movement.md](movement.md) has it.
- **Building:** the build button opens a unit's build menu, as
  [building.md](building.md) has it.
- **Sight:** each frame the player's living units' places and
  `rangeScan`s give the cells seen now, by the starting shroud's rule; the
  shroud lifts where they've been, and the fog lies where they don't see
  now. Other players' units and the map's objects but its planets and
  suns (`planet_obj`, `background_obj`) aren't drawn where they're
  covered, nor marked on the minimap, nor picked; a unit selected that
  goes under the cover leaves the selection. The planets and suns are
  drawn first, then the fog, the grid, the shroud, and the rest, as the
  original's order. Whether the asteroid fields and nebulae hide under
  the fog as the moon did is *(guessed)*. The minimap's layer is drawn
  as above; the asteroid fields there are their sprite drawn small,
  `mapSize` square, added on *(guessed)*, and the marks at their
  sprite's own size (a wormhole's 8).
- **Ending a skirmish:** the MENU button, or Esc, opens the menu over the
  paused game, as above; its settings open the settings screens, and
  Return to Game or Esc goes back. Surrender opens the debriefing, as
  does the end: the player lost once they've no unit left, or won once
  no player on another team has any *(guessed: the original wasn't
  played to an end; whether it counts freighters, or Allied Victory's
  part, isn't known)*. Abort Mission asks first, in the original's box,
  and its Yes opens the debriefing, as the original's did. The debriefing
  counts each player's dilithium brought in by freighters and spent
  (given back as a build is cancelled), the ships and stations they
  built, those they lost, and those of other players their shots
  destroyed.
- **A skirmish** (`MissionView::startSkirmish`) starts from the Game
  Setup screen's slots: each player's race, their colour and their
  start, rolled at launch as the original rolls them (0x43e7ee,
  0x43e8ad) *(from the code)*: with Random Placement, the setting it
  starts on, each player in slot order takes a start at random among the
  map's `Camera_<n>` paths not yet taken, and its number is its start's
  (the original's computer was player 4, at `Camera_4`); a side left
  Random becomes one of the four at random, the game's Federation,
  Borg, Romulan, Klingon. The game seeds its generator from the clock,
  so every game differs; Reluva's too, but for a `seed` setting, which
  `engine-shot` sets to 86 to roll as the original's runs in the
  emulator did. With Fixed Placement slot *n* starts on `Camera_<n+1>`
  *(guessed)*. The scene draws each player's units in their own colour.

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
- Where Fixed Placement puts each player.
- How the original ends a skirmish that's won or lost, rather than
  surrendered: what it shows first, and when a player counts as out.
  In a run of it with the computer's starbase killing both the
  player's construction ships (by 124 seconds), the player kept their
  starbase: Shift+D, `Input.map`'s `decomm`, pressed with it selected at
  118 seconds, hadn't taken it by 200, nor had the computer's ships come,
  so no end was reached.
- Whether the fog, over what's been seen, hides the asteroid fields and
  nebulae (the shroud hides both: the original's view over a field and
  a nebula never seen was its grey alone, as Reluva's), and what of an
  enemy's station once seen (a last sight of it, or nothing).
- The minimap's grey pixel, and how the view's bottom
  edge is made.
- The cinematic view's director: how it picks the next unit and how
  long, its wider shots, when it turns to a fight, and the starbase's
  own shot.
- What makes the crew grow: one starbase gave 3.5 a second; more, or
  none, haven't been seen.
- Which point of a unit the box has to hold, and how a group's orders
  are lit when its ships can do different things.
- How the info panel shows more than eight ships.
- What the ring's height is: near the plane for both, but not the same.
- When a ship's repair order is lit, and what the display shows of
  other ships and of a group.
