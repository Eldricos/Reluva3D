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
  its frames' origin at its place, as a sequence's *(inferred)*.
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
- **The waypoints** (`<map>.way`, a `CWayPointMap`): points
  `MinDistBetweenWayPoints` (15) apart over where walkers can go, lines
  joining those within `MaxDistToConnect` (29).

## In Reluva

`loadMap` (`game/map.cpp`) reads a map's size, camera, minimap and song,
its ground, and its parts' models, places and polygons. `MapScreen`
(`ui/map_screen.cpp`) draws it in `Rules.txt`'s view:

- **The ground** by the program's table, a cell at a time into a kept
  picture, its values spread straight across from the corners (not the
  program's fractal, which isn't decoded yet).
- **Polygons on the ground:** `Textured` with their texture repeated,
  `Black` black, `Shadow` halving what's under it *(inferred)*; the
  others (`Trans` colours, `Warped`, the waypoints' and free-range ones)
  aren't drawn.
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

The team, the team selection's or, without one, the officers the mission
says start, at the spawn points, the first at point 1 *(inferred)*, the
camera on the first (the camera's scripts that take it there aren't
there yet). Each is drawn from their body, its shadow and their head,
standing (`Idle`, by the time) or walking, in front of and behind the
map's models as they stand; the one chosen has the selection's marker
(`Interface/Selection`) under them. A click on an officer, or their slot
on the team bar, chooses them, a double click on the slot shows them; a
click elsewhere on the map sends the one chosen walking there *(inferred:
the game's mouse isn't traced)*, by the waypoints (`loadWayPoints`, the
shortest way along their lines by A*, then straight to the place
*(inferred)*), facing the way they go, at their walk's own pace, its
frames by how far into its cycle they are *(inferred)*.

A new game reaches it after the briefing (mission 1) or the team
selection's START MISSION. Not there yet: the interface's buttons, the
map's song, the camera's scripts, running and crouching, walkers going
round each other, and everyone else on the map.

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
  Refelian chosen and walking where clicked, round the walls. Not yet
  against the original running. Unit tests (`stawayteam.unit.maps`) read
  a map made in the test, and check a cell's textures blended by its
  values, a polygon on it, the camera kept on the map, a way found along
  the waypoints' lines, and an officer at their spawn point walking where
  sent.

## Open questions

- **The fractal** the program spreads a cell's values with, and its
  seeds.
- **The camera:** whether `View Position` is the view's middle, the
  bounds Rules.txt's `Camera Bound Width` and `Height` (142, 106) set,
  and the scrolling's speed and acceleration.
- **Drawing order:** how the program sorts the models, and what
  `YSort` and `Rendering Height` do.
- **Shadows**, and the `Trans` colours: whether the game shows those.
- **The team:** which spawn point each officer takes, `Facing Angle`'s
  degrees as the directions' (90 the viewer's way), the game's path
  finding, its mouse (which button chooses and which sends), the info
  bar's rising, and the frames' movement as a walk's pace.
