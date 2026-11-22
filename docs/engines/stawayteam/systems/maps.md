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
maps. In Mission 01's 32 enemies, `Attack` is a
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
  Setting` (`Stun` for 34 Borg guns).

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
  to them (`CAddEffectAction`, 330).

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
  `Stealth`.
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
health (the bar's foreground cut to what's left of it, from the
bottom *(inferred)*), the chosen one's slot lit
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
Cone either side of their facing, nothing standing full height
between), alerted: the Alert's Start Action, turning to them, and after
Alert Time attacking: the Attack's Start Action, facing them; their
target fallen or stunned, or out of sight five seconds with nowhere left
to go after them, back to the patrol *(inferred)*. Attacking, one whose
Pursue AI chases goes after a target out of sight by the waypoints to
where it saw them last, at a run with its Pursue's Move Mode Run, and
straight at one it sees out of its weapon's range *(inferred)*. Hurt to
its Retreat At Percent, one whose Retreat AI goes somewhere runs there by
the waypoints, runs its Done action, and holds the place, fighting
*(inferred)*. A state's actions have the enemy as their `$Trigger`, and
the officer it's after as their instigator *(inferred)*. The messages
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
shooter's `Shoot1` played through, the Weapon Damage taken off. An enemy
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

Not there yet: the interface's other buttons, the map's song, the
triggers set off by enemies, passing through or colliding, the AIs'
other messages and tasks (going to a place, effects added), walkers
going round each other, the vision button, the enemies' own noises and
footsteps' sounds by the ground, the shots' beams and projectiles,
missing, thrown and laid weapons, the phaser's setting, and the enemies'
other states (running away, recharging, investigating a body).

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
  seven seconds later. Every relocation in the maps checked: its map
  found and its places on it. Mission 08 run on: the team charging the Klingons,
  one hurt retreating to its alarm, and the mission lost to it.

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
- **The enemies:** likewise, the AIs' rules: when they pursue and give
  up, what holding a retreat's place is, what `$Trigger` and the
  instigator are in their states' actions, and how a radius's action
  chooses whom it reaches.
- **Noise:** which sides hear which noises, what `0x497d40` checks
  before a noise is heard (perhaps a way there), how the rings are drawn,
  and the investigating AI's rules.
- **Fighting:** the program's rules for sight (whether the footprints
  are what blocks it, `Half Height`), for aiming and missing, which
  weapon a character uses, the shots' beams, where on a character the
  flashes go, stunning, death, and the mouse for attacking.
- **Choosing:** the game's mouse for a box and Shift, and where officers
  sent together stand.
- **The team:** which spawn point each officer takes, `Facing Angle`'s
  degrees as the directions' (90 the viewer's way), the game's path
  finding, its mouse (which button chooses and which sends), the info
  bar's rising, and the frames' movement as a walk's pace.
- **A mission's maps:** whether those the team's left go on, which place
  each officer arrives at, what Only on this map does, and the other
  relocations.
- **The scripts:** whether the player can move the view while they play,
  how long a line stays, and how the beaming in looks.
