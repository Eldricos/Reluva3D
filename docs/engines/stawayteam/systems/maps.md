# Star Trek: Away Team: maps

> **Status:** partial · **Code:** `src/engines/stawayteam/game/map.cpp`, `src/engines/stawayteam/ui/map_screen.cpp`, `src/engines/stawayteam/ui/game_hud.cpp`, `src/engines/stawayteam/ui/animation.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

A mission is played on maps (`Levels/Mission NN/<name>.zax`, 33 of
them; the mission's `Map Name` is its first). A map is an object in
text ([formats/text-object.md](../formats/text-object.md)), a
`CLayerSaveData`: its size, where the camera starts, a ground of
textures blended by a grid of values, and its parts: models placed on
it, polygons laid on the ground, and what the game's AI and scripts
use. The game shows it in a view of the screen, `Rules.txt`'s `Screen
Pos=75,0` and `Screen Size=565,423`, the interface around it.

## How it works

### The map's file

At the top level of the `CLayerSaveData`:

- **`Width`, `Height`:** its size in pixels (Mission 01's Klingon
  Outpost: 3500 x 2700).
- **`View Position`:** where the camera is *(inferred: the view's
  middle)*.
- **`Custom Map Properties`** (`CStarTrekLayerSaveData`): `Minimap Image`
  (`Cache/Levels/<name>.frm16`), `Map Song`, and the camera's scripts
  played as the mission starts.
- **`Plasma Ground`** (`CPlasmaTileMap`), the ground, below.
- **`Tree List`** (`CSortList2D`), its parts, each a `Level Part`, below.
- `Path Data` (its waypoints, in the map's `.way` file,
  [formats/binary-object.md](../formats/binary-object.md)), `Team Info`
  (where the team beams in), the sound's environment, and the rest the
  game's state saves.

### The ground

`Plasma Ground` (read at `0x536d70`, drawn at `0x537450`):

- **`Num Textures`, `Texture N`:** the textures, of `Cache/Textures`
  (`Qo'nos Exterior/Grass Dead 3`: 128 x 128 frames, a `CGroundTextureFrame`
  each giving its surface for footsteps).
- **`Elevations Row N`:** a value from 0 to 255 for each corner of the
  ground's cells, two hex digits each (Mission 01: 55 x 43). Cells are 64
  pixels square (`0x4ef471`); the map's cells are those between its
  corners (`0x53753e` keeps to the grid's size less 2).
- **`Seeds Row N`:** a number for each corner, the blending's
  randomness (mostly 0).
- **`Blending`:** how much of a texture's band blends into the next
  (0.25 unless the map says; Mission 01: 1).

What a value draws is in a table of 256 (`0x538d20`):

- **Its texture:** value x textures / 256, its whole part; the last
  texture blends into none.
- **The next texture over it:** past 1 - `Blending` into its band, as
  far as it is into the rest, from none to all. In 16-bit colour, the
  game's, a weight of 0 to 255 (`0x539259`); in 8-bit colour, the
  palette's 20/80 and 40/60 blends in steps of 0.2 (`0x538dbb`).

Each cell is drawn once into a picture of its own, kept, and put on the
screen as the view passes over it (`0x538060`, `0x4dd4e0`). A cell whose
four corners all draw one texture is that texture; otherwise its values
are spread over its pixels from its corners, roughened by a fractal
seeded by the map's seeds (`0x538310`, `0x4abec0`), and each pixel drawn
from the table. The flat polygons over the cell are drawn into it too
(`0x4ef460`). The textures repeat across the map.

### Its parts

Each `Level Part` is an object of its class: `CEntityBase` (most:
walls, trees, crates), `CEntityAnimated`, `CEntityWalking` (characters),
`CRenderablePolygon`, `CFreeRangePoly`, `CWayPointsPolygon` and
others. They share `Visible`, `Collideable` and the rest; then:

- **A model:** `Model` (`Cache/Models/<name>.mdl16`), `Position X`,
  `Position Y`, `Rendering Height`, and `Cur Sequence` (`Idle`), drawn by
  its frames' origin at its place, as a sequence's *(inferred)*. A
  character's `Facing Angle` is in radians, 0 to 2 pi, its rotation
  sequence's `Rotational Range` turning through its directions (4.69 is
  about direction 9 of 12) *(inferred)*; some have an `Overlay Model`, a
  head drawn over them (`Characters/Head Fed Generic 01`, 157 in all).
- **A polygon:** `Polygon` (its corners, the first again at the end),
  `On Ground`, `Render Type` (`Textured` 872 in all the maps, `Black`
  1,285, `Shadow` 172, `Trans Blue`, `Trans Green`, `Trans Red`,
  `Warped` 29), `YSort`, and a textured one's `Static Texture`, an
  `Animated Texture` and a `Warp Effect` (water).

### The interface

Around the view, each at its place in Rules.txt's Custom Rules (a
`CStarTrekCustomRules`, read at `0x599b00`), its pictures in
`Interface/Screens/<part>`:

- **The team bar** (`0x5e18f0`, drawn at `0x5e3290`) at `Team Pos`: for
  each officer, `Team Icon Spacing` apart from `Team Icon Pos`, the
  `Member Background`; their name, first and last, in their field's
  colour (Command and Security Trek Small Red, Engineering Trek Small
  Gold, Medical and Science Trek Small) at `Team Field Offset`; their head
  icon centred on `Team Icon Offset`; and `Health Bar Foreground` over
  its background at `Team Health Offset`, filled from the bottom as far
  as their health (`0x5e2f50`).
- **The button box** at `Button Box Pos`: the move, vision, sound,
  select-all, objectives and options buttons, and CANCEL ORDERS.
- **The info bar** (`0x4de730`) at `Info Pos`, under the screen's bottom,
  `Info Bar Visible` (60) rows of it showing, sliding up over `Info Bar
  Popup Time` (0.25 s) to show the rest (`0x4de991`): its open button,
  and the minimap (`0x4dddc0`) on `MiniMap Background` at `Info MiniMap
  Pos`, the map's minimap picture centred on `Info MiniMap Map Center
  Pos`.
- **The inventory** at `Inventory Pos`: the chosen officer's items along
  its bar, `Inventory Icon Size` and `Inventory Icon Spacing` apart from
  `Inventory Bar Offset`, between its previous and next buttons.
- **The pause button** at `Pause Pos`.

### The team on the map

- **Where they start:** the map's spawn points, `Model=Editor/Spawn
  Point` parts named `Away Team Spawn Point 1` to `6`, hidden, each with
  a `CSpawnPointAI` facing them (`Facing Angle=225`).
- **What they look like:** the officer's portrait's definition names a
  `Body Model` and a `Head Model` (`Characters/Generic Male Body`,
  `Characters/Head Cpt Marcus Refelian`). Each has the same sequences
  (`Idle`, `Walk`, `Run`, `Crouch`...), 12 directions each; the head is
  drawn over the body frame for frame (the body's entity's `Overlay
  Model`), a `<sequence> Shadow` under them. Direction 0 faces left, and
  they turn anticlockwise on the screen: 3 faces the viewer, 6 right, 9
  away, as the frames show.
- **Walking:** a moving sequence's frames say how far it goes
  ([formats/binary-object.md](../formats/binary-object.md)): the
  Generic Male Body's walk 26 pixels across, or 19 down, a 10-frame cycle
  at 14 frames a second.
- **Walking, running, crouching:** the actions bound to Run, Crouch and
  Walk set the officer's move state (0 crouching, 1 walking, 2 running;
  the body's entity starts at 1), and Cycle Movement presses the button
  box's move button, which goes round them (`0x476b90`). Crouching moves
  by `Crouch` and stands by `IdleCrouch`; running by `Run`.
- **The waypoints** (`<map>.way`, a `CWayPointMap`): points
  `MinDistBetweenWayPoints` (15) apart over where walkers can go, lines
  joining those within `MaxDistToConnect` (29).

### The camera's scripts

`Custom Map Properties`' `Camera Script Manager` (a
`CCameraScriptManager`) holds steps (`CCameraScript`) played one after
another as the map starts:

- **Panning** (`0x477560`, `0x4776a0`): from the view's middle to the
  part named `Where To Pan`, over `Time To Pan` seconds, eased in and
  out, (1 - cos(pi t)) / 2 of the way at t of the time; without `Pan To
  Position` there at once, though the step still takes its `Time To
  Pan`.
- **Then** its `Destination Action`, and `Time To Wait` seconds before
  the next step.
- **Mission 01's actions:** `CMultipleActionsAction` (its `Action`s at
  once), `CDelayAction` (its `Next Action` after `Delay` seconds),
  `CInfoDialogAction` (a line in the info dialog), `CAddEffectToTeam`
  (the team beams in: `Warp Sound`, and a `CStarTrekTransporterEffectBehavior`
  of `Warp Time` 1.75 s with the `Transporter Effect` and `Transporter
  Overlay` sequences), `CAddCameraScriptAction` (more steps), and the
  relays and activations of the map's triggers.
- **The team is hidden** till then: each spawn point's `CSpawnPointAI`
  makes who spawns there invisible (`CMakeVisibleAction`).

### The map's triggers

Parts with AIs (`Activity`) make the map's events happen: hidden
markers (`Editor/Trigger Icon, Proximity`, `Editor/Relay`) named for
scripts to find. Mission 01 has 103 `CRelayAI`, 31
`CTouchingOvalTriggerAI`, 3 `CTouchingPolygonTriggerAI`, 5
`CRepeatTimerTriggerAI`, and others (`CPassThroughTriggerAI`,
`CollideTriggerAI`, `CProcessListAI`...). Their fields:

- **Every part:** `Active` (the arrows and goto-spot markers start
  inactive, and are switched on and off to blink) and `Visible`.
- **A touching trigger** (`CTouchingTriggerAI`'s kinds): an oval about
  the part, `X Radius` across and `X Radius` x `YX Ratio` down, or the
  part's polygon; who sets it off (`Triggered By Name`, `Players`,
  `Enemies`, `Anything`); `Enter Action`, `Exit Action`, and `Repeat
  Action` every `Repeat Every` seconds while someone's there; `Trigger
  Only Once` and `Auto-Flip Switch` (`CTriggerAI`).
- **A relay** (`CRelayAI`): its `Action`, and `Trigger Only Once`; a
  `CTriggerRelayAction`'s `Relay Name` (a list, commas between: "Sniper
  Rom Pauser for Movie, 1st Rom Pauser for Movie") runs the relays of
  those names.
- **A repeat timer** (`CRepeatTimerTriggerAI`): its `Action` every
  `Delay Between Actions` seconds, give or take `Plus or Minus`.
- **The actions** they run (over 50 kinds in Mission 01): most often
  `CMultipleActionsAction`, `CTriggerRelayAction`, `CDeactivateAction`,
  `CDelayAction`, `CActivateAction`, the AIs' messages, `CTricorderDialogAction`,
  `CPlaySoundAction`, `CInfoDialogAction`; `$Instigator` names whoever
  set the trigger off.

The program's trigger code isn't traced yet: how these fields act is
read from the maps.

### The enemies

A character with a `CBorgAI` (the enemies' AI, whatever their race) is a
state machine: `Patrol`, `Alert`, `Attack`, `Pursue`, `Investigate`,
`Stun`, `Dead`, `Retreat` and others, each a state object
(`CBorgStatePatrol`...) holding a tree of small AIs, its `Start Action`
and `End Action`, and `Current State` the one it starts in (`Patrol`).

- **Sight** (the AI's `Targeting`, a `CBorgSetClosestTarget`, whose
  fields are written into the AI itself in the maps): `Max Distance`
  (375 in the Romulan's model, 430 in Mission 01), `Vision Cone` (125
  degrees) and `Valid Targets` (`Player,Team Member`).
- **The patrol's AIs:** `CSeriesAI` (its `AIs` one after another),
  `CLimitedTimeAI` (`What To Do` for `Max Time`), `CIdleAI` (its `Idle
  Behavior`'s `Num Seconds`), `CFollowPathAI` (along the polygon of the
  part named `Path Poly`; `Order` `Go To End`, `Loop`, `Back and forth`
  or `Forth and back`; its `Moving`'s `Play Ratio`), `CScanAreaAI`
  (turning from `Starting Angle` to `Ending Angle`, degrees, at `Turning
  Speed`), `CGoForwardAI`, `CGoHomeAI`, `CRandomWaitAI` and others.
- **Alert:** `CTurnToTargetAlertAI` for `Alert Time` (2 s), its `Start
  Action` relaying to the mission's voices (`Attack Voice Relay`).
- **Attack:** `CStarTrekStandGroundAttackAI`, shooting with
  `CShootWeaponBehavior`, aiming at the target.

The program's AI code isn't traced yet: how these act is read from the
maps.

The info dialog (`0x435db6`) is `Dialog Window/Background` at Rules.txt's
`Dialog Pos`, the speaker's icon at `Dialog Icon Pos`, the line from its
script in `Dialog Font` at `Dialog Text Pos`, wrapped to `Dialog Text
Width`. Its voice-over plays with the options' voice-overs on; it's shown
while that plays, or, with them off, for the voice-over's length
(`0x4362f1`), and the time given it besides (`0x435f9c`).

## In Reluva

`loadMap` (`game/map.cpp`) reads a map's size, camera, minimap and song,
its ground, and its parts' models, places and polygons. `MapScreen`
(`ui/map_screen.cpp`) draws it in `Rules.txt`'s view:

- **The ground** by the program's table, a cell at a time into a kept
  picture, its values spread straight across from the corners (not the
  program's fractal, which isn't decoded yet).
- **Polygons on the ground:** `Textured` with their texture repeated,
  `Black` black, `Shadow` halving what's under it *(inferred)*; the
  `Trans` colours half see-through over the ground while they're active
  *(inferred)*; `Warped`, the waypoints' and free-range ones aren't
  drawn.
- **Models:** each part's model's sequence, its first direction's frames
  in time, back to front by where they stand (with `Rendering Height`)
  *(inferred)*.
- **The camera:** starting at `View Position`, kept so the view stays
  on the map *(inferred)*, scrolled by the keys bound to Scroll Map
  Left, Right, Up and Down (the arrows) and by the mouse at the view's
  edges, at the options' Scroll Speed in pixels a second *(inferred)*.
  Esc asks before going back to the main menu.

The interface (`GameHud`, `ui/game_hud.cpp`) around it, its parts at
Rules.txt's places: the team bar with each officer's slot, name, head and
health (whole, as nothing hurts them yet), the chosen one's slot lit
*(inferred)*; the button box's buttons as they start *(inferred)*; the
info bar rising while the mouse is over it *(inferred)*, its minimap with
the view outlined, a click on it moving the view there *(inferred)*; the
chosen officer's items in the inventory; and the pause button.

The map's other characters face their `Facing Angle`, their shadow under
them and their head over them, as the officers are *(inferred)*.

The camera's scripts play as the map starts, the view panned by them and
not by the player meanwhile *(inferred)*.

The triggers (`ui/map_triggers.cpp`): a part is drawn while it's visible
and active, its triggers acting while it's active *(inferred)*. A
touching trigger answers to any officer for `Triggered By Players` or
`Anything`, or to the officer named; its Enter Action runs as one comes
in, its Repeat Action then and every Repeat Every while they stay, its
Exit Action as they leave; with Trigger Only Once it's done after the
first that runs, and with Auto-Flip Switch its part is then turned off
*(inferred)*. Relays, repeat timers, and these actions are run: several,
random, series (each time the next), delays, info and tricorder lines
(the tricorder's in the info dialog), relays, activating, deactivating,
showing, hiding, deleting and destroying parts by name, sounds, checks
whether a part's there (other checks fail), the team beaming in, and
camera scripts *(inferred)*. Names compare without regard to case
*(inferred)*. The team beams in fading in under the
`Transporter Overlay`'s sparkle *(inferred)*. A line
stays a second and a half after its voice-over *(inferred)*, four
seconds without one *(inferred)*. Without scripts, the view starts on the
team.

The team, the team selection's or, without one, the officers the mission
says start, at the spawn points, the first at point 1 *(inferred)*.
Each is drawn from their body, its shadow and their head,
standing (`Idle`, by the time) or walking, in front of and behind the
map's models as they stand; the one chosen has the selection's marker
(`Interface/Selection`) under them, their slots lit. A click on an
officer, or their slot on the team bar, chooses them alone, with Shift
adds them or takes them away, a double click on the slot shows them; a
box dragged on the map chooses those in it (with Shift, as well); Select
Entire Team's key (A) and the select-all button choose everyone; a click
elsewhere on the map sends those chosen walking there, the first onto
the place and the rest about it a figure's width apart *(inferred: the
game's mouse and grouping aren't traced)*, each by the waypoints (`loadWayPoints`, the
shortest way along their lines by A*, then straight to the place
*(inferred)*), facing the way they go, at their walk's own pace, its
frames by how far into its cycle they are *(inferred)*. The keys bound to
Walk, Run, Crouch and Cycle Movement, and the move button, set how the
ones chosen move, the button going walk, run, crouch *(inferred for its
order)*, and the button shows it.

A new game reaches it after the briefing (mission 1) or the team
selection's START MISSION.

The enemies (`ui/map_enemies.cpp`), while their parts are active: on
patrol, their patrol's AIs as above, a path straight from corner to
corner, a scan's turning in degrees a second, the rest done at once
*(inferred)*; seeing an officer (in Max Distance and half the Vision
Cone either side of their facing, walls not yet hiding anyone), alerted:
the Alert's Start Action, turning to them, and after Alert Time
attacking: the Attack's Start Action, facing them; out of sight five
seconds, back to the patrol *(inferred)*.

Not there yet: the interface's other buttons, the map's song, the
triggers set off by enemies, passing through or colliding, the AIs'
messages and tasks, objectives, walkers going round each other, sight
blocked by walls, and the enemies shooting, pursuing, investigating and
the rest of their states.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; the maps and textures in
  `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, Mission 01's
  Klingon Outpost at its start and scrolled: the paving, dirt and dead
  grass blending, walls, buildings and crates in place and in front of
  one another as they stand; the whole ground drawn small, beside the
  map's minimap; the interface around the view, the info bar rising under
  the mouse with the minimap; the team at their spawn points, Marcus
  Refelian chosen and walking where clicked, round the walls; Mission
  01's opening, the view panning over the crates with Data's lines and
  home, the team beaming in and Marcus Refelian's line; the Klingons and
  Romulans facing their ways, the fallen lying as they fell. Not yet
  against the original running. Unit tests (`stawayteam.unit.maps`) read
  a map made in the test, and check a cell's textures blended by its
  values, a polygon on it, the camera kept on the map, a way found along
  the waypoints' lines, an officer at their spawn point walking where
  sent, a camera's script panning, eased, showing a line and beaming
  the team in, the team chosen by key, Shift and box and sent together,
  a guard walking its route, looking about, seeing an officer, alerted
  and attacking, and an officer walking into a trigger that relays to an
  arrow, on and a second later off, once only. Mission 01 run on: the
  opening's delayed relays bringing Data's lines that teach selecting and
  moving the team.

## Open questions

- **The fractal** the program spreads a cell's values with, and its
  seeds.
- **The camera:** whether `View Position` is the view's middle, the
  bounds Rules.txt's `Camera Bound Width` and `Height` (142, 106) set,
  and the scrolling's speed and acceleration.
- **Drawing order:** how the program sorts the models, and what
  `YSort` and `Rendering Height` do.
- **Shadows**, and how the `Trans` colours are drawn.
- **The triggers:** the program's own rules for all of the above, which
  are read from the maps' fields, not from its code.
- **The enemies:** likewise, the AIs' rules; and how walls block sight.
- **Choosing:** the game's mouse for a box and Shift, and where officers
  sent together stand.
- **The team:** which spawn point each officer takes, `Facing Angle`'s
  degrees as the directions' (90 the viewer's way), the game's path
  finding, its mouse (which button chooses and which sends), the info
  bar's rising, and the frames' movement as a walk's pace.
- **The scripts:** whether the player can move the view while they play,
  how long a line stays, and how the beaming in looks.
