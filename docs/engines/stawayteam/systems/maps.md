# Star Trek: Away Team: maps

> **Status:** partial · **Code:** `src/engines/stawayteam/game/map.cpp`, `src/engines/stawayteam/ui/map_screen.cpp`, `src/engines/stawayteam/ui/animation.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

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

A new game reaches it after the briefing (mission 1) or the team
selection's START MISSION. Not there yet: the interface around the view,
the minimap, the map's song, the team, the camera's scripts, and
everything that moves.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; the maps and textures in
  `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, Mission 01's
  Klingon Outpost at its start and scrolled: the paving, dirt and dead
  grass blending, walls, buildings and crates in place and in front of
  one another as they stand; the whole ground drawn small, beside the
  map's minimap. Not yet against the original running. Unit tests
  (`stawayteam.unit.maps`) read a map made in the test, and check a
  cell's textures blended by its values, a polygon on it, and the
  camera kept on the map.

## Open questions

- **The fractal** the program spreads a cell's values with, and its
  seeds.
- **The camera:** whether `View Position` is the view's middle, the
  bounds Rules.txt's `Camera Bound Width` and `Height` (142, 106) set,
  and the scrolling's speed and acceleration.
- **Drawing order:** how the program sorts the models, and what
  `YSort` and `Rendering Height` do.
- **Shadows**, and the `Trans` colours: whether the game shows those.
