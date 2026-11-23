# Star Trek: Away Team: maps

> **Status:** partial · **Code:** `src/engines/stawayteam/game/map.cpp`, `src/engines/stawayteam/ui/map_screen.cpp`, `src/engines/stawayteam/ui/game_hud.cpp`, `src/engines/stawayteam/ui/map_interface.cpp`, `src/engines/stawayteam/ui/animation.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

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
- **`View Position`:** where the camera is: the view's middle, its
  corner half the view up and left (`0x4f9433`).
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
screen as the view passes over it (`0x538060`, `0x4dd4e0`). Its values
are spread over its pixels from its corners by a fractal seeded by the
map's seeds (below), and each pixel drawn from the table (`0x5387a0`). In
8-bit colour only, a cell whose four corners draw one texture, unblended,
is that texture (`0x537d1d`). The flat polygons over the cell are drawn
into it too (`0x4ef460`). The textures repeat across the map from its
corner: a pixel's place masked to the texture's size, a power of 2
(`0x54006e`).

### The fractal

A cell's values are a picture of 65 x 65, its corners the four corners'
values (`0x538310`), filled by midpoint displacement with random numbers
from R250:

- **The random numbers:** each the XOR of those 250 and 147 back
  (`0x4abf1f`). As the map starts, 256 generators are made, the nth
  seeded with 946415 (n + 1) (`0x536991`): its 250 numbers by a linear
  congruential generator (x 1103515245 + 12345, bits 16 to 47 of the
  product), filled last first, then the 4th and every 7th after given a
  bit each, the top one first (`0x54c040`). A corner's seed picks a
  generator, a fresh copy each time (`0x54c0e0`).
- **The edges:** the top edge by its left corner's seed plus 0x55, the
  bottom by its left corner's plus 0x55, the left by its top corner's,
  the right by its top corner's; so the cells beside one another make
  their shared edge alike. Halved down to every pixel, each middle the
  average of its ends moved by a random amount from -m to m - 1, m
  starting at an eighth of the edge (8) and halving each time, until 0
  (`0x4abec0` along a row, `0x4ac310` down a column).
- **The inside:** by the cell's seed less 0x56. Square by square, each
  middle the average of its four corners, rounded; then each side's
  middle not made yet (0) the average of its ends, moved likewise, in
  the order right, left, top, bottom (`0x4abfd0`).
- **Moved values** past 255 are 255, and under 0 are 1, 0 meaning not
  made yet (`0x4ac290`).

### Water

A polygon of `Render Type` `Warped` (29 in the maps, in Missions 03, 04,
08, 10 and 14) is drawn as a textured one is (`0x54eb66`), its texture
its `Warp Effect`, a `Water Warp Effects/<name>.txt` (a
`CWaterWarpEffectFrame`): a `Static Texture` warped afresh each time the
game's time moves on (`0x5f5450`), into a picture of 128 x 128:

- **Its waves:** `V1 Wave`, `V2 Wave`, `H1 Wave` and `H2 Wave`, each a
  `CWaterWaveFormData` of `Num Waves`, `Amplitude` and `Pixels Per
  Second`. Each is a table of 128 whole pixels, the cosine of `Num Waves`
  turns across it times the amplitude, rounded (`0x5f3cf0`); at a time it
  has moved the time by its speed, rounded, round the 128 (`0x5f46ae`).
- **Its `Render Type`:** `Horizontal` moves each row along by its place
  in H1 and H2 together (`0x5f46a0`); `Vertical` each column down by its
  place in V1 and V2 (`0x5f4790`); `Horizontal Then Vertical`, the
  maps', a row along, then the column it took the pixel from down
  (`0x5f4890`); `Vertical Then Horizontal` the other way (`0x5f4ae0`);
  `Horizontal Then Vertical Animated` as `Horizontal Then Vertical`, on
  the frame of its `Animated Texture`.
- **The files:** `Generic Water` (`StarFleet Earth/waterFractal`),
  `Planet 01 Water` (`Planet1/Water 5`) and `!None`.

### Its parts

Each `Level Part` is an object of its class: `CEntityBase` (most:
walls, trees, crates), `CEntityAnimated`, `CEntityWalking` (characters),
`CRenderablePolygon`, `CFreeRangePoly`, `CWayPointsPolygon` and
others. They share `Visible`, `Collideable` and the rest; then:

- **A model:** `Model` (`Cache/Models/<name>.mdl16`), `Position X`,
  `Position Y`, `Rendering Height`, and `Cur Sequence` (`Idle`), drawn by
  its frames' origin at its place, lifted by its `Rendering Height`
  (below). A
  character's `Facing Angle` is in radians, 0 to 2 pi, its rotation
  sequence's `Rotational Range` turning through its directions (4.69 is
  about direction 9 of 12) *(inferred)*; some have an `Overlay Model`, a
  head drawn over them (`Characters/Head Fed Generic 01`, 157 in all).
- **Its place in the drawing:** below.
- **A polygon:** `Polygon` (its corners, the first again at the end),
  `On Ground`, `Render Type` (`Textured` 872 in all the maps, `Black`
  1,285, `Shadow` 172, `Trans Blue`, `Trans Green`, `Trans Red`,
  `Warped` 29), `YSort`, and a textured one's `Static Texture`, an
  `Animated Texture` and a `Warp Effect` (water).

### The drawing order

The models are drawn back to front by a key for each picture, or each
strip of one (`0x4913c0`):

- **Where it's drawn:** at its `Position X` and `Position Y` less its
  `Rendering Height` (the window, trim and roof pieces on the StarFleet
  maps' walls: 200, 83, 76...), but for a sequence with `Render On
  Ground`, at its place; a sequence that `Is Shadow` is cast 0.7 of the
  height left and half of it down (`0x491444`).
- **Its strips:** a sequence's `Cutting Lines` (1,493 of the 7,480
  sequences in the models) are `CVerticalCut`s, each a `Cut Offset`, an x
  from the part's place, and a `Strip Sorting Y Offset`. A frame is cut at
  each into strips, the first from its left to the first cut, the last
  from the last cut to its right (`0x5518d0`).
- **The key:** where it stands (its y, not lifted), plus its model's
  `Sorting Y` (a model's, `0x4930f0`; for a model drawn over another, as
  a head, the two added, `0x51065e`), plus the strip's offset: the
  sequence's `Sorting Y Offset` for the first strip, the cut's `Strip
  Sorting Y Offset` left of it for the others (`0x5512c0`); that times 8,
  plus the layer drawn, the first 0 (`0x4915d2`). A wall cut so lets
  someone stand in front of one end and behind the other.

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
  which Toggle Minimap (M) presses (`0x4dee47`), and the minimap
  (`0x4dddc0`) on `MiniMap Background` at `Info MiniMap Pos`, the map's
  minimap picture centred on `Info MiniMap Map Center Pos`. On the
  minimap (`0x4ddf50`), dots of a pixel, (128, 128, 255) or red (255, 0,
  0), and the points of interest, the parts with a
  `CMiniMapPointOfInterestAI` (187 in the maps, some given by a
  `CAddAIAction`), as squares of (64, 64, 255) whose half-width is 4 less
  a phase going up 1.5 a second and round at 4 (`0x4de220`). The info
  bar's other pictures (its objective, character and info windows) aren't
  named by the program.
- **The inventory** (`0x41ad70`, a strip of icons) at `Inventory Pos`:
  a place for each item type, `Inventory Icon Size` and `Inventory Icon
  Spacing` apart from `Inventory Bar Offset`, between its previous and
  next buttons. A place with none is darkened, (10, 10, 40) half
  see-through (`0x423fe2`); else one of its items in its `Item Frame`,
  on the `Selected Icon Background` chosen, with the `More Indicator`
  (`0x41ae65`). Inventory Next (`]`, the wheel down) goes along the
  chosen item's place, and past its last back to its first and on to the
  next place with any, its first (`0x423da0`); Inventory Prev back the
  same way, to the place before's last (`0x423e90`). Either shows the
  item's name for 1.5 seconds (`0x423e52`), in the strip's Trek Small at
  `Inventory Title Pos`.
- **The pause button** at `Pause Pos` (`0x5c3cb7`): `Pause Time
  Button/Pause Time Default`, `Hover` and `Press`. With time stopped, the
  `Pause Indicator` sequence is drawn at the view's top left and "Pause
  Time Activated" in `Trek Title Red` 20 under its top, its brightness
  going up and down (`0x5be085`).

The keys press the box's buttons (`0x476b90`): Cancel Orders
(Backspace) CANCEL ORDERS, Toggle Vision Cones (V) the vision button,
Select Entire Team (A) the select-all button, Display Objectives (O) the
objectives button. The vision button has three states, `Vision
Default`, `Vision Select` and `Vision Press`, a press going on to the
next and round (`0x51a600`); V from the second goes back to the first
(`0x476c1c`). Choosing, the cursor is the `Vision Cursor` (`0x5c5e4e`).

The mouse's pointers are Rules.txt's `Mouse Cursors`, the sequences of
`Interface/Game Cursors` (`0x5c5d2f`), each's origin its hotspot; the
usual one `Interface/Cursors/Pointer` (`0x4f566a`). At the view's edges
the `Move Up`, `Move Up Left`... cursors, with which way each scrolls
(`0x5c25ee`). An item's cursors are named by its definition: its
Process's `Process Cursor` and `Can't Use Cursor` (`0x546d9e`), its
`Entity Hovering Cursor` (`0x5d8e10`), and its `Obscured Cursor` with
`Show Obscured Cursor` (`0x5d8fed`): the phaser's are `Phaser`, `Phaser
Active`, `Phaser Inactive` and `Phaser Obscured`. A part's processes have
their own (`Generic Use` and `Generic Can't Use` for 253). `Out Of Range
Cursor` is an old key: read from an item file that has it, it's taken as
a number into the definition (`0x5dacf0`); no file has it, and no cursor
is shown for a place out of range.

Zoom In (keypad +), Zoom Out (keypad -) and Zoom Reset (keypad *) are
sent to the view as 1, 2 and 4 (`0x5bf25c`), which scales it: its scale,
the map's pixels to the screen's, 0.01 less each frame Zoom In is held,
down to 0.5, and 0.01 more for Zoom Out, up to 1.5; Zoom Reset 1; held
neither, a scale within 0.125 of 1 eases back to it at 0.1 a second
(`0x4f6f00`). The scale's held so the view shows no more than the map,
its size less one rounded down to 128 (`0x4f97a0`). Which of 1 and 2 is
which key is by their order *(inferred)*.

### The camera

The view's corner is kept from 0 to the map's size less one, rounded
down to 128, less the view's, the second winning on a map smaller than
the view (`0x4f1f10`). It scrolls with a velocity (`0x5c0de0`): each
frame a way's held, by the keys or the mouse at the view's edges, the
options' Scroll Acceleration is added to it that way, and it's held to
the Scroll Speed; held none, it's slowed by a factor from 0.95 at the
least acceleration (7.5) to 0.5 at the most (75), 0.5 + 0.45 (1 - (a -
7.5) / 67.5) (`0x5bc6a6`), each part under 0.05 stopping; the view moves
by it times the frame's time. Rules.txt's `Camera Bound Width` and
`Height` are for following a character (`0x4f93fe`).

The options button opens the "Esc Menu" (`0x5ba9bf`), on `Escape
Menu/Escape Menu Background`, its buttons of `Button Background` in
`Placar Condensed`, a column 69 in, the first 26 down and each 8 under
the last: CONFIGURE, SAVE GAME, LOAD GAME, RESTART MISSION, END GAME
(saving and loading left out in a network game), and RESUME 27 above
its bottom. RESTART MISSION asks "Are you sure you want to restart this
game?", END GAME "Are you sure you want to quit the game and exit to
the main menu?" (`0x5bb190`).

### The team on the map

- **Where they start:** the map's spawn points, `Model=Editor/Spawn
  Point` parts named `Away Team Spawn Point 1` to `6`, hidden, each with
  a `CSpawnPointAI` facing them (`Facing Angle=225`). Each officer, in
  the team's order, takes a spawn point given for them, else the one
  named `<their name> Spawn Point` (`Sheila Thatcher Spawn Point` in
  Mission 13, where the others' points take them hostage), else the
  first numbered one no one's taken (`0x5bc2b0`, `0x5bc160`). The
  program tries a numbered point named after something of the officer's
  between the last two, not worked out.
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
- **Going round one another:** a walker's moving behaviour has a `Path
  Collide Checker` (`CPathCollideChecker`), and a wanderer's AI a
  `TimeOfLastChangeDirectionDueToCollide`; how they keep apart isn't
  traced.
- **Joining the team:** `CAddToTeamAction` (`Who To Add`: Data, Ambassador
  Worf, Brexen Ijoula, Ivan Verov, and `$Trigger` 19 times) makes a
  character of the map one of the team, given a `CPlayerAI` as well;
  `CRemoveFromTeamAction` (17) takes one off it. Data and Worf are in the
  roster (`Character/Starting Roster`).
- **Rescues:** a prisoner is a character with an enemy's AI whose `Valid
  Targets` are none (Mission 01's scientists). The map's actions rescue
  them: in Mission 01, Computer Systems on a panel lowers the force field
  (a door), gives the scientist a touching trigger, and an officer
  reaching them sets off a dialog, a camera script (`Pause Actors`) with
  `CPauseActorAction` and `CUnpauseActorAction`, the scientist beamed out,
  and the objective's count. Who speaks is chosen by `CCheckForClassAction`
  (116), the officer's field (`Class` Security, Science, Medical).

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

### A mission's maps

Six missions have more than one map (02, 04, 05, 06, 09 and 15; Mission
04 five). The team goes from one to another by a
`CStarTrekRelocateTeamAction` (43 in the maps; a `CSwitchMapActionBase`):
`New Map Name` ("Mission 02/02 Security deck.zax", its case not the
file's; Mission 09's without `.zax`), `New Location`, `Who To Switch`
(`$Instigator`), `Touching Poly` (the officers in it go), `Make this map
active` (1 for 51 of 54), `Only on this map`, and `Only Relocated
Selected Members` (13). The places they arrive at are parts numbered for
each officer ("to lower bay 1" to "to lower bay 4" on the Security
Deck), or a list (Mission 15's "space1,space2,space3,space4").
`CStarTrekRelocateAction` (7) moves `Who To Switch` alone, `Relative
Position` or not; one of Mission 16's names no map, moving them on the
same one. `CStarTrekRelocateGroupAction` (4) moves a `Group Name`.

### The music

A mission's `.MDS` has its `Primary Song` and `Random Song`, an array
(of `Music/`), under its `Music` group (`0x507014`): "Mission 01 Full
Mix.ogg" and its two "Sub Mix"es, for Missions 01, 02, 03, 04, 05, 07
and 17. Each map has its own `Map Song`: "Scavenger Briefing.ogg",
"Stasis Briefing.ogg" or "Munro's Briefing.ogg" for the rest, and
`!None.ogg` for Missions 16 and 18. The program's choosing between them
isn't traced.

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

A part's **processes** (`CProcessListAI`, on 337 parts in the maps, all
but 5 of them with a model) are what an officer does with it: its
`Process` array of `CProcess`, each with a `Process Type` (`Scan` with
the Tricorder 80, `Use` with nothing chosen 69, `Item use` with Computer
Systems 54, `Medical Scan`...), `Required Selected Item`, `Required Item
In Inventory`, its cursors, `Default Use Sound`, and its `Process
Activity`, the officer's AIs one after another: `CGoToAI` to its
`Destination` (`$trigger` the part), `CGotoRadiusAI` within its `Radius`,
`CGotoTargetsAI` to its `Target` ("leave #1-4#"), and `CActionAI` with
its `Action`, most often a `CTricorderDialogAction` (89), whose line
shows on the tricorder's panel. `CAddAIAction` gives a part an AI (59
processes, 28 points of interest, 8 triggers), and `CRemoveAIAction`
takes one away by its class's name.

The 5 without a model are polygons: Mission 16's four gas clouds, each
with a tricorder `Scan` ("Gas in Area") and a `Medical Scan` ("Dead
Borg"), and Mission 10's `Holocontrol activate poly`, whose list is
empty: it's the `Hot Spot` of the holo control panel's list. What's
under the pointer is picked by the parts' shapes; one whose list names a
`Hot Spot` is kept only with the pointer in that part's shape
(`0x5c0396`, `0x53c620`), so the panel is used by its polygon beside
it, as is Mission 10's last panel by its own. A process with `Execute if
Hovering` runs with nothing clicked: as the pointer's over its part, the
one for the chosen item, or else the one to `Use`, is started only if
it's flagged so (`0x43cd10`, `0x43d020`); the gas clouds' scans, and
the carpets' in them, which carry the same two, are. A touching trigger's `Triggered by`
lists whom it answers to (`Enemy,Player` on 111 `CAnyTouchingOvalTriggerAI`;
the stasis field's `CEveryTouchingOvalTriggerAI`).

A part's `Default Death Type` names its death behaviours, `Death
Behaviors/<type>.txt` (`Klingon` for 540 parts, `Borg` 206): the
sequences played and actions run as it's killed. A Klingon's give its
body a process for the Teleportation Brig Controller, and set its
categories (`CSetCategoryAction` `Dead Bodies`, `CClearCategoryAction`
`Enemy`). Its `Category` is a list (`Electronic Equipment,Enemy`); a
`CDoOnAllWithinRadiusAction`'s `Who To Affect` picks by it, and each
category's `MiniMap Color` (`Categories/`) colours it.

The other triggers:

- **Anything touching** (`CAnyTouchingOvalTriggerAI`, 116;
  `CAnyTouchingPolygonTriggerAI`, 21): its `Enter Action` once anything
  of its `Triggered by` is in it, after `Enter Action Delay`, its `Exit
  Action` once nothing is, after `Exit Action Delay` (0.5): the doors'
  `COpenDoorAction` and `CCloseDoorAction` (`Door Name` `$trigger`).
- **The team touching** (`CTeamTouchingPolygonTriggerAI`, 3): its
  `Action`, setting objectives.
- **Colliding** (`CollideTriggerAI`, 47): its `Action` as one of those it
  answers to runs into the part (`Triggered By Players`, `Enemies`,
  `Anything`, `Projectiles`), not again within `Disallow Retrigger`
  seconds.
- **Passing through** (`CPassThroughTriggerAI`, 6): its `Action`. A
  collideable part with one lets what runs into it through, and is only
  touched: its AIs are asked whether it lets the mover through
  (`0x4ef966`, `0x492e40`), and that one says yes (`0x43a640`), where a
  `CollideTriggerAI` and the touching triggers say no (`0x43d0f0`).
  With `Triggered By Anything`, a thrown thing crossing it too, the
  instigator; Mission 01's and 08's check it's of `Grenades` (the
  grenade model's `Default Entity` `Category`) and make a bouncer
  polygon active for half a second.
- **Triggered By Enemies** on 66 touching triggers.

Whom a trigger answers to (`0x417f10` for the touching ones, `0x562ee0`
for colliding and passing through): its `Triggered By Name`, then
`Anything`, then `Players` and `Enemies` by the part's flags, then
`Projectiles`: anything with a `CShotAI` among its AIs or its model's
(`0x417fb3`). Only an energy weapon's shot has one (`0x5c95d0`, from
`CShootEnergyWeaponAction`); a thrown thing's AIs are its
`CThrowGravityAI` (a `CGravityParticleAI`) and a `CIgnoreParentAI`, so
it's answered to only by a trigger `Triggered By Anything`. A touching
trigger looks at every `Active` entity in its oval or polygon
(`0x417670`, `0x4f1d30`), so a thrown thing among them, flying or
lying. No touching trigger in the maps is `Triggered By Anything`, and
the one `Triggered By Projectiles` (Mission 15's Borg beamer) answers to
the officers too. That one starts inactive and nothing in the mission
makes it active (no action names it, and the radius actions reach only
`Enemy`), so in the game as it was sold it never goes. The energy weapons' shot is a `CPhaserShot`, a bolt
whose head goes on from the muzzle along its way each frame, up to its
range, meeting what's in its way as it goes (`0x535c00`).

**Doors** (`CDoorAI`, 237 parts): `Is Locked` (12), and actions run
before and after they open and close (`Before Open`, `After Opened`,
`Before Closed`, `After Closed`: some relocate the team). A door's model
has `Closed`, `Opening`, `Open` and `Closing` sequences, and its
`Activate Sound` and `Close Sound`. `CLockDoorAction` and
`CUnlockDoorAction` lock and unlock them.

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
  (300 unless it says, `0x5dd316`; 375 in the Romulan's model, 430 in
  Mission 01), `Vision Cone` (125 degrees) and `Valid Targets`
  (`Player,Team Member`). Its target is the nearest of its Valid Targets
  nearer than Max Distance and within half the cone either side of its
  facing, a cone of 0 or of 360 degrees or more seeing all round
  (`0x5dd690`, `0x4fe710`); the finding has no test of sight past what
  stands between.
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

The program's AI code is traced only in part: how its state changes
are made (below), its choice of a target ([The enemies](#the-enemies)'
Sight), and the Run Away rule; the rest is read from the maps. In Mission 01's 32 enemies, `Attack` is a
`CStarTrekStandGroundAttackAI` in all, `Pursue` a `CChasePursueAI` in
29; their `Target If Hit` is 1 in 30 of them; a `Damage Action` and a
`Stun State` (`Stun`) are given too.

In all the maps' 776 enemies:

- **Pursue:** a `CChasePursueAI` in 761, a `CGoToAI` in 6; the state's
  `Move Mode` `Run`.
- **Retreat:** `Retreat At Percent` of their health, 0 for 639; 134
  retreat, 48 of them by a `CGoToAI` whose `Destination` is an alarm or
  a security panel ("alarm center 5"), with a `Done action`. In Mission
  08, a Klingon reaching one sets off the alarm, and the mission's lost.
- **Stun State:** `Stun` for 628, `Attack` for 145 (the Borg's). A map
  can set an enemy's weapon to stun, its inventory item's `Weapon
  Setting` (`Stun` for 34 Borg guns). The `Stun` state's `Next State`:
  `Investigate` for 358, `Recharge` for 130.
- **The states' numbers** (`0x6379c8`): Any 0, Recharge 1, End Recharge
  2, Patrol 3, Got a Target 4, Lost a Target 5, Alert 6, Attack 7, Pursue
  8, DoSomething 9, Investigate 10, Investigate Dead Body 11, Retreat 12,
  Run Away 13, Stun 14. `Got a Target` and `Lost a Target` choose the
  next: `GotATarget`'s `Acquired a Target` (Attack), `Had a Target`
  (Investigate for 755) and `Dead Body` (Investigate Dead Body for 728,
  Patrol for 42); `LostATarget`'s `Had a Target` (Investigate for 756) and
  `Default` (Patrol for 687). The `Alert`'s `Next State` is `Got a
  Target` for 746.
- **Their actions:** Start and End Actions on every state: the Alert's
  relay to a voice (307), the Stun's (184 and 265), the Investigate Dead
  Body's relay, mostly to an alarm (254), the Attack's (300 or so).
- **Transitions** (`Transitions`, a `CBorgTransitionHolder`; 215 enemies
  have some): each a `Trigger` (`Every Time`, `First Time`, `Once Per New
  Target`), `From State` and `To State` (`Any` for either), and an
  `Action`, mostly relays: `Once Per New Target`, Any to Attack, on 154.
- **A change of state** (`0x4347d0`): asked for, it's made as the AI
  next runs: the state left's exit (its `End Action`), then each
  transition from it to the new one not done (`0x426550`), then the new
  state's entry (its `Start Action`), the new one keeping the old as its
  `Last State`. A transition `Every Time` fires each time; one `First
  Time` or `Once Per New Target` is done once fired, the second made
  ready again whenever the enemy's target changes (`0x4352d9`). Every
  matching transition fires, in their order.
- **Valid Targets:** `Player,Team Member` for 350, `Player` for 263,
  none for 84 (prisoners and scientists), `Enemy` for some Klingons.
  `CBorgSetTargetTypeAction` (504) changes them.
- **Run Away If Too Close** (757): with its target within a twentieth of
  its weapon's range, it runs away (state 13) for three seconds
  (`0x42b084`), by a `CRunAwayRetreatAI`.
- **Recharge:** 80 start in it; its `Recharge AI` (a `CSeriesAI`, a
  `CGotoRadiusAI`, a `CRechargeHealthAI` giving back `Hit Points`) and
  `Time To Recharge` (-1 for 521, 2.5 for 130); `End Recharge`'s AI, a
  `CGoForwardAI` (624) or `CActionAI`.
- **Inactive:** a `CBorgStateInactive` on every enemy; its own `Current
  State` the phase it starts in (`Shutting Down`), its
  `PlaySequenceBehavior` a sequence (`shutdown` for 85, `Idle` for 583).
  An AI deactivated (`CSendDeactivateAIMessage`, 15 in Missions 07 and
  16, `$Trigger`: Borg shut down) goes to it and plays it; activated
  (`CActivateAIMessage`), it plays it again and goes back to its `Last
  State` (`0x42c410`, `0x42c4b0`). One enemy starts in it.
- **DoSomething:** 17 start in it, its `DoSomething AI` a
  `CRepeatTimerTriggerAI` running its `Action` every while.
- **Others of its Valid Targets:** a few enemies' `Valid Targets` name
  `Enemy`: Mission 01's trapped Klingons (themselves `Electronic
  Equipment`, freed to fight the Romulans) and two Borg in Mission 06.
- **Shots:** the program's weapons draw no random numbers
  (`CShootEnergyWeaponAction`'s code, about `0x5f66d6`, never calls its
  `0x54c120`): a shot doesn't miss by chance.
- **Investigate Dead Body:** a `CChaseInvestigateAI` (737), to the body.
  A body is a part of category `Dead Bodies`: a Klingon's death
  behaviours make it one.
- **`CSetPatrolAIAction`** gives an enemy a new `Patrol AI`.

They tell one another, by actions:

- **`CSendInvestigateHereMessageAction`** (1,029 in the maps): its
  `Target` is sent to look where the action's instigator is: the message
  is made from it (`0x578060`) and sent to the target.
- **`CSendInvestigateTargetMessageAction`** (213): its `Target` is given
  the instigator as a target (`0x578b70`).
- **`CDoOnAllWithinRadiusAction`** (719; a `CDoOnAllInsideAction`): its
  `Touching Action` for each part of `Who To Affect` (`Enemy`) about its
  `Target Entity` (`$Trigger` or `$Instigator` mostly) within `Radius`
  and `Y To X Ratio` of it, whose name has its `Name Filter`, but `Who To
  Ignore`, with its `Start Action` and `Done Action`. In the alert and
  attack states' Start Actions and the Damage Actions, it calls the
  others near to look (`Touching Action` a
  `CSendInvestigateHereMessageAction` to `$Trigger`) or adds an effect
  to them (`CAddEffectAction`, 330): a transporter's (`CStarTrekTransporterEffectBehavior`,
  `Warp Time` 1.75, `Delete When Finished`), beaming them out, with the
  race's `Warp Sound`.

### Fighting

- **Weapons** are item definitions (`Inventory/Item Definitions`, the
  enemies' in its folders, `Enemy Weapons/Vulcan Phaser`): `Weapon
  Damage`, `Reuse Delay` (seconds), `Item Range` (pixels), `Max Shoot
  Angle` (degrees), `Discharge Action` (`CShootEnergyWeaponAction` for
  phasers and disruptors, `CShootProjectileWeaponAction` for the sniper
  rifles, `CStrikeTargetAction` for a Targ's jaws and the neural
  disrupter, `CThrowProjectileWeaponAction` for grenades, none for
  mines), `Discharge Sequence` (`Shoot1`), `Shot Sound`, `Shot Flash`
  and `Hit Flash` (models: `Enemy Shots/Enemy Shot Flashes/Phaser
  Flash`, one direction, `Idle` at 15 a second), `Shot Duration`,
  `Stun Duration`, and its `Default Inventory Item`'s `Weapon Setting`
  (`Kill`, or `Stun`). The Phaser: 25 damage, every 0.35 s, 375 pixels;
  the Romulan Disruptor: 30, every 0.75 s, 375.
- **Who has what:** a map's part's `Inventory`, its `Inventory List`'s
  items' `Definition Index` (30 of Mission 01's 32 enemies have a
  weapon); a model's `Default Entity` has one too; the officers have the
  roster's.
- **Health:** a model's `Hit Points` (the Romulan's 100); the team
  bar's `Health Bar Background` and `Foreground` at Rules.txt's `Team
  Health Offset` (54, 7) in each officer's slot. The characters'
  models have `Shoot1`, `Death`, `Dead` and `Throw` sequences.
- **What hides what:** a model's `Bounds Poly` is its outline on the
  ground about where it stands, in floats (the Klingon wall's from 47.3,
  118.6 round to -87.1, 101.5); a part's `Collideable` and `Full Height`
  (and `Half Height`) say whether it's in the way and whether it stands
  tall enough to hide anyone *(inferred)*. Triggers turn
  `Collideable` with a `CSetCollidableAction` (12 in Mission 01).

The program's weapon code isn't traced yet: how these act is read from
the fields.

### Noise

The team's footsteps and shots make noises, which the game keeps in a
list (`CNoiseManager`, at the game's `0x334`; added at `0x5194e0`, each a
`CNoise`: where, its radius, and its time left of its age).

- **Footsteps** are in the officers' body models' walking sequences
  (`Walk`, `Run`, `Crouch`): their `Trigger`, an action for each of a
  direction's frames, has a `CAddFootstepAndSoundBySurfaceTypeAction` on
  two of them (frames 3 and 8 of the walk's 10) whose `Footstep Noise` is
  a `CAddFootstepNoiseAction`: `Radius` 100, `Age` 1, `Attribute`
  `Stealth`. The action plays its sound by the ground under the foot
  (`0x4c9a50`), then makes the noise (`0x518748`): its `Default Sound`,
  or one of its `Specializations` (`SyfaceTypeSoundItem`s, a `Surface
  Type` and a `Sound`), the surface being a ground texture frame's
  (`CGroundTextureFrame`'s `Surface Type`; `Surface Types/*.STY`:
  Carpet, Dirt, Generic, Grass, Hollow Metal, Metal, Stone, each with a
  `Max Distance`, 45 to 100, which the noise doesn't use). In all the
  game's 3,738 models it's `!None.ogg` with no specializations: the
  footsteps are silent, and `Sounds/PLAYER/FootstepLeft.ogg` and the
  others there are used by nothing.
- **How loud:** the Radius (`0x518100`) times the maker's attribute's
  quietening, 0.5 + 0.5 x (1 - value / 100) (`0x5e54b0`, its half from
  `0x5180be`, the 100 an attribute's whole at `0x5e5830`; 1 without the
  attribute), times Rules.txt's modifier for how they move (`0x5c9530`):
  `Crouch Noise Modifier` 0.25, `Walk Noise Modifier` 3, `Run Noise
  Modifier` 6. An officer with Stealth 30 walking: 100 x 0.85 x 3, 255
  pixels; running 510; crouching 21.
- **Shots:** a weapon's `Discharge Action` has its own `Noise Radius`
  and `Time` (the Phaser's 250 for 1 s; 14 weapons have 250), with the
  shot's `Forward Position`, `Lateral Position` and `Height`, and its
  beam's `Outside Color` and `Middle Color`.
- **Beams:** a `CShootEnergyWeaponAction` makes a `CEntityAnimatedLine`
  (`0x499b3e`) with its `Outside Color` and `Middle Color` (the line's
  own (205, 20, 25) without them, `0x499b66`): drawn as three lines
  (`0x499470`), the middle from `Start` to `End` in the Middle Color, and
  one each side of it, by its `PerpenNormal`, in the Outside Color. The
  Phaser's (255, 83, 0) and (255, 255, 0), the Romulan Disruptor's
  greens, the Borg gun's blues.
- **Hearing** (`0x519440`): a listener hears the newest noise it's inside,
  its distance up and down counted 1.403 times (the view's
  foreshortening), on the same map, if it's of a side that hears that
  noise's maker. An enemy's AI asks it when its `Investigate Sounds` is 1
  (23 of Mission 01's 32).
- **Investigating:** the `Investigate` state's `Investigate AI`, a
  `CChaseInvestigateAI` in 30 of Mission 01's enemies: `Run To
  Investigate`, `Time To Investigate` (5), path finding on, and its own
  `Investigate AI`, a `CScanInvestigateAI` whose `AI` is a `CScanAreaAI`
  (-30 to 30 degrees).
- **Shown:** each noise is drawn as a ring growing to its radius over its
  age, by sin(time gone / age x pi/2) (`0x5197c0`). The button box's
  sound button, and the key bound to Toggle Sound Cones (S), press it
  (`0x476c3f`); Toggle Vision Cones (V) presses the vision button
  (`0x476c01`).

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
  picture, its values spread by the program's fractal
  (`game/ground_fractal.cpp`); the 8-bit colour's shortcut isn't there,
  as the game draws in 16-bit.
- **Polygons on the ground:** `Textured` with their texture repeated,
  `Black` black, `Shadow` halving what's under it (`0x542378`); the
  `Trans` colours, while they're active, adding 10 steps of 16-bit
  colour to their channel, red and blue of 31 and green of 63, at most
  full (`0x5429e4`, `0x53fe85`, `0x53f985`); `Warped`, water, over the
  ground each frame, warped by `game/water_warp.cpp` to the game's time
  (which stops while it's paused *(inferred)*); the waypoints' and
  free-range ones aren't drawn.
- **Models:** each part's model's sequence, its first direction's frames
  in time, lifted, cut into strips and drawn back to front as above; a
  part's shadow, body and head together, in its strips; the officers by
  where they stand and their body's `Sorting Y`, uncut. Ties drawn as
  they come *(inferred)*.
- **The camera:** starting at `View Position`, kept on the map as the
  program keeps it, scrolled by the keys bound to Scroll Map Left,
  Right, Up and Down (the arrows) and by the mouse at the view's edges,
  speeding up and slowing as the program's does; the program's frames
  aren't fixed, and Reluva takes 30 a second for them *(inferred)*.
  Esc asks before going back to the main menu.

The interface (`GameHud`, `ui/game_hud.cpp`) around it, its parts at
Rules.txt's places: the team bar with each officer's slot, name, head and
health (the bar's foreground cut to what's left of it, from the
bottom *(inferred)*), the chosen one's slot lit
*(inferred)*; the button box's buttons as they start *(inferred)*; the
info bar rising while the mouse is over it *(inferred)*, or held up by
its open button or M, its minimap with the view outlined, a click on it
moving the view there *(inferred)*, and on it the officers on the map
and the enemies standing *(inferred)* as dots and the points of interest
pulsing; the chosen officer's items in the inventory's places, the
places in the team selection's order, the last type's first
*(inferred)*, Inventory Next and Prev, and the previous and next
buttons *(inferred)*, going along them as above, the More Indicator on
a place with more than one, over its frame *(inferred)*, and the item's
name centred on Inventory Title Pos *(inferred)*; and the pause button.

The mouse pointer (`ui/map_interface.cpp`) is the game's own, as the
mouse is: the Pointer over the interface and in the menus, the edges'
arrows, the Vision Cursor choosing; over the map, as a click there would
go *(inferred)*: with an item chosen, its Entity Hovering Cursor over
what it's used on, its Obscured Cursor where it's out of sight, its
Process Cursor elsewhere and its Can't Use Cursor where it can't be;
over a part with a process for the officer, the process's cursor; over
an enemy, their weapon's. Zoom In and Out, held, scale the view as the
program does, a step each of its frames, taken as 30 a second
*(inferred)*; Zoom Reset back to its size.

A click on a part with a process for the chosen officer, one for their
chosen item first, else one to Use with nothing (carrying its Required
Item In Inventory), sends them through its Process Activity: walking to
each place, `#1-4#` the first, then running its actions, they the
instigator and the part `$trigger` *(inferred)*; one for their item from
within its Item Range, as items are ([items.md](items.md#using-one))
*(inferred)*. A
`CTricorderDialogAction`'s line is typed on the tricorder's panel.
`CAddAIAction` gives its parts the AI, a trigger watched from then on;
`CRemoveAIAction` takes away theirs of that class, stopping its triggers
*(inferred)*. The actions run as the gadgets need them
([items.md](items.md#in-reluva)): a radius's action picks by Who To
Affect, the officers too when it names Player or Team Member, its
$instigator in its Touching Action the one it's on and the place it's
about where they're sent to look *(inferred)*. A killed enemy's death
behaviours' actions run; an enemy's categories and Default Death Type
are its model's where the map leaves them out, Enemy without either
*(inferred)*.

The buttons (`ui/map_vision.cpp`, `ui/escape_menu.cpp`): the pause
button or Pause (Enter) stops time, the view still moved and orders
still given, till it's pressed again *(inferred)*. CANCEL ORDERS or
Backspace drops the chosen officers' walking, attacking and items'
tasks *(inferred)*. The vision button goes off, choosing, showing: a
click on an enemy while choosing shows its sight, or pressed on to
showing, the enemy nearest the view's middle *(inferred)*; Cycle Vision
Cones (Tab) the next enemy in the view *(inferred)*. Its sight, in its
Max Distance and Vision Cone and in sight past what stands in the way,
is tinted over the ground in squares *(inferred)*.
The options button, or Esc, opens the escape menu, holding the game
still *(inferred)*: CONFIGURE the settings over the map, RESTART MISSION
and END GAME as asked, RESUME or Esc back; SAVE GAME and LOAD GAME are
greyed out, as saved games aren't there yet.

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
says start, at the spawn points as the program chooses them, each
running its point's Spawn Action.
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
*(inferred)*; seeing an officer (the nearest in Max Distance and half
the Vision Cone either side of their facing, as the program finds it,
and nothing standing full height between *(inferred)*), alerted: the Alert's Start Action, turning to them, and after
Alert Time its Next State, through GotATarget's Acquired a Target for
Got a Target *(inferred)*: mostly attacking, the Attack's Start Action,
facing them; their
target fallen or stunned, or out of sight five seconds with nowhere left
to go after them, back to the patrol *(inferred)*. Attacking, one whose
Pursue AI chases goes after a target out of sight by the waypoints to
where it saw them last, at a run with its Pursue's Move Mode Run, and
straight at one it sees out of its weapon's range *(inferred)*. Hurt to
its Retreat At Percent, one whose Retreat AI goes somewhere runs there by
the waypoints, runs its Done action, and holds the place, fighting
*(inferred)*. A state's actions have the enemy as their `$Trigger`, and
the officer it's after as their instigator *(inferred)*.

Each change of an enemy's state runs the state left's End Action, the
transitions that match it (Every Time; First Time once; Once Per New
Target when its target's another), and the new state's Start Action,
in that order, as the program does. One whose Valid Targets don't name Player or
Team Member doesn't see the officers. Losing its target, it goes by its
LostATarget's Had a Target: Investigate, looking about where it saw them
last *(inferred)*. Up from a stun, its Stun's Next State: Investigate,
looking about where it is *(inferred)*, or Recharge. Recharging, it runs
its Recharge AI (a CRechargeHealthAI its Hit Points back a second,
*inferred*), for its Time To Recharge if it has one, then End Recharge's
AI and its patrol; it sees the team as on patrol. A target within a
twentieth of its weapon's range, one that Runs Away If Too Close runs off
from them three seconds, then back at them; where to, its weapon's
range away, *inferred*. On patrol, looking about or recharging, a dead
body it sees and hasn't before: its GotATarget's Dead Body, Investigate
Dead Body, going to look at it, then back on patrol *(inferred)*. A
CSetPatrolAIAction's AI is its patrol from then. With no officer to
go after, one whose Valid Targets name another enemy's category fights
the nearest it sees as it would an officer: alerted, then shooting in
range and going after them out of it, giving up out of sight; the one
shot fights back if its own Valid Targets name the shooter *(inferred)*.
Deactivated, it shuts down (Inactive), its sequence played once and
held, seeing nothing, till activated, back in the state it was in.
Doing something, its repeat timer's Action every while, give or take,
watching as on patrol *(inferred)*. A CForceStateChangeAction puts it in
its State, Investigate looking about where it is. Beamed out (an effect
naming it, Delete When Finished), it's held still and fades over the
warp's time, then is gone *(inferred: the transporter's pictures aren't
drawn)*.

Walkers go round one another (`ui/map_screen.cpp`): a step that would
bring them within 14 pixels of an officer or enemy ahead turns 30, 60 or
90 degrees either way to pass; with no way past, they wait, and after a
second go on; near where they're going, whoever's on it is stopped short
of, not gone round *(inferred)*.

A character added to the team (`ui/map_mission.cpp`) becomes an officer
where they stand: the roster's of their name, or one made of their part,
its model their body *(inferred)*; their part's gone, its AI with it,
and they're on the team bar, and on the mission's other maps as away
from them *(inferred)*. Taken off the team, they stay on the map, a
part of it in their place: their name, body and head, where they stood
and facing their way, with the AIs given them before (`$Instigator`
of the action taking them off is that part after). So Mission 13's
hostages, laid out by a temporary task's `dead`, have a process each for
a hypospray; used on one (the hypospray, on no officer, goes to a part's
process), the officer walks over, uses one up (`CUseInventoryItemAction`)
and adds them back (`CAddToTeamAction` on `$trigger`): the same officer,
where the part was, the part gone *(inferred: the program keeps the one
character)*. `CPlayAnimationAction` plays its `Animation` once, then
its `End Sequence Name`. A polygon with processes is picked with the
pointer in it, when there's no model in reach or the one there has none,
and an item's range is measured to where it's pointed at; a process run
on hovering starts once as the pointer comes onto its part *(inferred)*.
A click in reach of a part with processes is
on it before any other *(inferred)*. A paused actor's held still till
unpaused. A touching
trigger on a character goes where they go. A class check is of the
officer's field.

The triggers (`ui/map_triggers.cpp`): the anything-touching, team,
collide and pass-through ones as above *(inferred)*; the touching ones
Triggered By Enemies, or Anything, answer to the enemies too, each as an
officer, its instigator *(inferred)*. A thrown thing sets off one
Triggered By Anything as it comes in, its Enter Action, and as it leaves
or goes off, its Exit Action; not its Repeat Action *(inferred)*. A
collide trigger's part is its footprint, or its polygon, or 20 pixels
about it *(inferred)*; a thrown thing sets off a collide or pass-through
one by its polygon only. An energy weapon's shot is its line from the
shooter to the one shot, at once: a touching trigger Triggered By
Projectiles it crosses runs its Enter and Exit Actions, the shooter
their instigator *(inferred: the program's bolt flies, and its shape
against the trigger's isn't traced)*. A door
(`ui/map_gadgets.cpp`) opens unless locked: its Before Open, its Activate
Sound, Opening played and then Open, out of the way at once, and its
After Opened as it's open; closing likewise, back in the way *(inferred:
when it stops blocking isn't traced)*. The messages
above are acted on: told to look, one on patrol or looking goes there;
given a target, it attacks them; a radius's action reaches the enemies in
its oval, whose name has the filter in it *(inferred)*.

Fighting (`ui/map_combat.cpp`): sight is blocked by the outlines (`Bounds
Poly`) of the parts shown that collide and stand full height, about
where they stand, a line crossing one's edge *(inferred)*; the enemies
themselves don't block it. Each character has their model's Hit Points,
or 100 without *(inferred)*, and the first weapon of theirs that shoots,
or else one that strikes *(inferred)*. An enemy attacking shoots its
target while it sees them in its weapon's range, aimed within its Max
Shoot Angle, each Reuse Delay; one it sees further off it walks straight
at *(inferred)*. A click on an enemy sends the officers chosen to shoot
it: by the waypoints till it's in range and sight, then each Reuse Delay
*(inferred)*; walking elsewhere, or the
enemy fallen, they stop. A shot always hits *(inferred)*: its Shot Sound,
its Shot Flash on the shooter and its Hit Flash on the one hit, the
shooter's `Shoot1` played through, the Weapon Damage taken off. A beam
weapon's beam is drawn from its muzzle (Forward Position along the way
to the target, Lateral Position across, Height up) to the target as high
up *(inferred)*, as above, for its Shot Duration, fading *(inferred)*.
A footstep plays its Default Sound where it has one (none do); the
surfaces' sounds aren't looked up, none being given. An enemy
hit runs its Damage Action, and with Target If Hit attacks whoever hit it;
a weapon set to stun puts it down for the Stun Duration, then back on
patrol, unless its Stun State is Attack, when it attacks *(inferred)*.
An enemy's weapon set to stun puts an officer down for its Stun
Duration unhurt, unable to move or shoot, and unseen as a target
meanwhile *(inferred)*. With no health left a character plays `Death` once
and lies `Dead`; a fallen officer can't be chosen or sent.

Noise (`ui/map_noise.cpp`): an officer's walk makes its footsteps'
noise as its frames pass them, as loud as the program reckons; an
officer's shot makes its weapon's. An enemy with Investigate Sounds, on
patrol or investigating, that hears the newest noise it's inside goes
there by the waypoints (running with Run To Investigate) and looks about
by its own AI for Time To Investigate, then goes back on patrol where it
left off; another noise heard meanwhile sends it there instead; seeing an
officer meanwhile alerts it *(inferred)*.
Walls don't stop noise *(inferred)*. The sound button, or S, shows the
noises as yellow rings, squashed up and down as they're heard, the button
held down meanwhile *(inferred)*.

A mission's maps (`ui/main_menu.cpp`): each is loaded as the team's
first sent there, its parts as they start, and kept while the mission
lasts, those the team isn't on going on meanwhile *(inferred)*. A
relocation's officers are those in its polygon, or else Who To Switch,
only the chosen ones if it says; each leaves their map and arrives at
their place, the place's number as they go, or the list's next, or
beside the place itself, as they were but with nothing under way
*(inferred)*; the map's shown, them chosen, if it says. An officer on
another map is out of this one's: not drawn, chosen or seen. Their slot
chosen on the team bar shows their map *(inferred)*. The mission's
objectives, end and time are the same on all its maps, the time counted
while one's shown.

The music (`ui/main_menu.cpp`): on a mission's maps, its Primary Song,
then one of its Random Songs by chance after another; without them, the
map shown's Map Song, over and over; stopping as the maps are left
*(inferred)*.

Not there yet: the AIs'
other messages and tasks (CSendMessageAction's to animations; the
particle systems' are there, [particles.md](particles.md)), walkers going round each other, the enemies' own noises,
thrown and laid weapons, the doors opening
by themselves (CDoorAI's own Triggered By flags), the camera scripts'
Pause Actors, and those off the team still on its bar.

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
  and attacking; a wall hiding an officer from a guard, the guard
  shooting her down once she's past it, and an officer sent to shoot it
  walking into sight and killing it; footsteps as loud as Stealth and
  the move make them, a noise too far up not heard and one near heard,
  the guard going to look, looking about and back on patrol; a guard's
  alert calling another in its radius to look, but not one out of it or
  ignored, its stun gun downing an officer unhurt, a hurt guard retreating
  to its alarm and setting it off, and one sent after an officer going
  after him unseen; and an officer walking into a trigger that relays to an
  arrow, on and a second later off, once only. Mission 01 run on: the
  opening's delayed relays bringing Data's lines that teach selecting and
  moving the team; the four officers sent at the nearest Romulan, round
  the walls till they see it, killing it with their phasers, one of them
  hit and his health bar down, the shots' flashes on them and on it, and
  it falling; the team running towards a Klingon that listens, the
  rings of their footsteps shown, it coming to look, and back on patrol
  seven seconds later; Mission 01's enemies' Valid Targets read (one, a
scientist, none; three Klingons Enemy), a Romulan attacking the team,
losing them and going to look where it saw them last; the inventory's six places, Inventory Next going
along them and the item's name shown, the pointer at the edges and over
a Romulan out of sight (`Phaser Obscured`), Ivan Verov's tricorder on
the first Romulan walking into range and showing its own line ("Romulan
centurion with trace signatures of a bio-chemical imbalance."), the
minimap held up with the team, the Romulans and the crates' squares,
and the view zoomed out and in. Unit tests also check the bar's places
and going along them, the pointer, a process walked to and run, a point
of interest added, and the zoom; a prisoner without Valid Targets not
attacking, a stun's Start Action, its transition and Next State, a
guard investigating a body once, a recharge's time and its End Recharge
going forward, an enemy beamed out and gone after its warp time; a door
opening for an officer near and its After Opened, closing after she's
gone, a crate bumped into, a wire set off by an enemy sent to it, and
the team in a square; an officer walking round another and stopping
short of one on her spot, a prisoner joining the team, an actor paused
and let go, and a class check. Mission 01: Ivan Verov using Computer
Systems on the panel (the pointer its Generic Use), the force field
lowered, walking to the first scientist, and the scientist beamed out
after the dialog and camera script; a trapped Klingon joining the team
and walking where sent. The team firing on the first Romulan, the
phasers' beams drawn from an officer's muzzle to it. Every model's
footsteps read: none has a sound. A unit test checks a beam's ends and
colours and its going after its Shot Duration. Every relocation in the maps checked: its map
  found and its places on it. Mission 08 run on: the team charging the Klingons,
  one hurt retreating to its alarm, and the mission lost to it.
- **The fractal and the water:** traced in the program, then Reluva run
  on the retail files: Mission 01's and Mission 03's ground with ragged
  blending and no seams between cells, cells side by side checked to
  share their edges; Mission 03's pond rippling from frame to frame.
- **The polygons' colours:** the span fillers each `Render Type` calls
  (`0x53ec66`) read for what they do to a 16-bit pixel.
- **Every mission's maps:** all 41 maps of the 18 missions loaded on
  the retail files and played three minutes with the team standing
  still: none failed to load or stopped; Mission 12's and 18's officers
  shot where they beam in, a patrol seeing them across open ground, as
  the program's would; Mission 13's team, all taken hostage at their
  spawn points, till Sheila Thatcher's own point was found to come
  first; then, with the Borg shut down, Sheila walking to Yraxys,
  using a hypospray, and Yraxys back on the team where she lay; a unit
  test of a hostage revived.
- **The drawing order:** traced in the program; a unit test of a cut
  wall and a lifted crate; Mission 05's StarFleet HQ on the retail files,
  its roofs' trim, windows and canopies up on the buildings where they'd
  lain on the pavement, and Mission 01 as before.
- **The camera, the zoom and enemies' targets:** traced in the program;
  unit tests of the scrolling's speed and slowing, the zoom's steps and
  easing, and the nearer of two officers chosen; Mission 01's fight run
  again on the retail files.

## Open questions

- **The interface:** what the program marks on the minimap, and what
  the inventory's places and the cursors' choosing
  are.
- **Processes:** how the program chooses one, walks to it, and when the
  officer must be in range.
- **Frames:** how many a second the program's camera and zoom took
  their steps at.
- **Drawing order:** how the program breaks ties between equal keys,
  and where the polygons' `YSort` puts those drawn standing.
- **Enemies' sight:** whether anything stops an enemy seeing past what
  stands between (the program's finding of a target doesn't).
- **Water's time:** whether the program's clock the water moves by
  stops while the game is paused.
- **The triggers:** the program's own rules for all of the above, which
  are read from the maps' fields, not from its code.
- **The enemies:** likewise, the AIs' rules: when they pursue and give
  up, what holding a retreat's place is, what `$Trigger` and the
  instigator are in their states' actions, and how a radius's action
  chooses whom it reaches.
- **The states:** when the program changes them, and how Got a Target
  and Lost a Target choose between their choices; what makes a
  low-health enemy run
  away (`0x42b0e2`), and when it looks for bodies.
- **Walking:** how the program's walkers keep apart, and how a joined
  character's made an officer.
- **Doors:** when one stops blocking, and what their own Triggered By
  flags do.
- **Noise:** which sides hear which noises, what `0x497d40` checks
  before a noise is heard (perhaps a way there), how the rings are drawn,
  and the investigating AI's rules.
- **Fighting:** the program's rules for sight (whether the footprints
  are what blocks it, `Half Height`), for aiming and missing, which
  weapon a character uses, how long a beam's shown and how high it ends,
  where on a character the
  flashes go, stunning, death, and the mouse for attacking.
- **Choosing:** the game's mouse for a box and Shift, and where officers
  sent together stand.
- **The team:** the numbered spawn point named after something of the
  officer's, `Facing Angle`'s
  degrees as the directions' (90 the viewer's way), the game's path
  finding, its mouse (which button chooses and which sends), the info
  bar's rising, and the frames' movement as a walk's pace.
- **The music:** which song the program plays when, and whether the Map
  Song plays with a mission's own.
- **The buttons:** whether orders can be given with time stopped, what
  CANCEL ORDERS drops, how an enemy's sight is chosen and drawn, and
  whether the escape menu holds the game.
- **A mission's maps:** whether those the team's left go on, which place
  each officer arrives at, what Only on this map does, and the other
  relocations.
- **The scripts:** whether the player can move the view while they play,
  how long a line stays, and how the beaming in looks.
