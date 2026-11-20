# Jedi Outcast: single player's weather

> **Status:** partial · **Code:** `src/engines/idtech3/render/world_effects.cpp`, `src/engines/idtech3/client/sp_play_page.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; `yavin_swamp` running in Reluva · **Confidence:** from the code unless marked

## Overview

`jk2sp.exe`'s renderer keeps a weather system of its own (an earlier one
than the later games' `r_we`): rain or snow falling round the eye while
it's in the level's open air. The client game starts it with its call 89,
`R_WorldEffectCommand(text)`. In 1.02a only one level has weather:
`yavin_swamp`'s `fx_rain` makes the client game send `rain init 500`
as the level starts; no script sends more.

## How it works

### Commands (0x48ed50)

The first word names the system, `rain` or `snow`:

- **`rain init <drops>`**: a new rain of that many drops, in place of any
  before (0x48eecc; its object 0x8c bytes, made at 0x48e210).
  **`rain remove`** stops it. `snow init`, `snow remove` do the same for
  snow.
- Other words go to the system made (0x48e530): **`fall <min> <max>`**
  the speed down (default -60 to -50 a second), **`spread <distance>
  <height>`** (20 and 20), **`alpha <a>`** (0.15), **`height <h>`** (a
  drop's length, 5), **`angle <a>`** (how far drops lean in the wind, 1),
  **`fog`** (a fog layer with it).
- `debug` and `wind` are taken too; `blah` runs `snow init 1000`, `snow
  alpha 1`, `snow fog`, a test.

### The rain (0x48e210 on)

- **Its picture:** `gfx/world/rain.tga`, its red copied into its alpha
  (0x48e346), clamped, no mipmaps, nearest filtering.
- **Each drop:** an angle round the eye (0 to 2π), a distance from it (0
  to 20), a height (-20 to 40 at the start), and how fast each changes a
  second (±0.1, ±0.1, -60 to -50).
- **Each frame** (0x48e740), `seconds` the frame's time:
  - a gust's direction every 200 to 449 frames: a random direction
    across, of length 1; the wind turns towards it by `seconds` of the
    difference;
  - the eye's contents decide whether it rains: in "outside" brushes
    (`textures/system/outside`, contents 0x10000) and not in water (4),
    it fades in at 0.5 a second to 1; otherwise out to 0;
  - while faded in at all, each drop moves on by its speeds; one fallen
    below -20 starts again at 40, its angle, distance and speeds drawn
    afresh.
- **Drawn** (0x48e9d0), after the scene and only in views of the level
  (0x48eca0), round the eye: each drop a triangle from where it is
  (`sin(angle) x distance` across, `cos(angle) x distance` along, its
  height up, leant by `height x angle x wind`), across by 0.2 of the
  view's left axis (0x4cf510), and from there down by the drop's length
  against the wind; its picture's s 1 to 0 across, t 0 to 1 down. Its
  colour is white, its alpha the rain's (below the eye, the distance
  over the depth times it, at most it) times the fade; blended over what's
  there (source alpha, one minus source alpha), depth tested but not
  written, both sides.

### The surface sprites (0x4882ef on)

While there's rain:

- weather sprites (`ssFXWeather`) show in full while it falls on the eye;
- the wind is as strong as the rain leans (its angle x 75, x 0.3),
  gusting every 300 over that seconds (none if under 1), towards the yaw
  the rain's wind blows to.

Otherwise the `r_wind...` settings and `r_surfaceWeather` say.

## In Reluva

`WorldEffects` keeps the rain: its commands, its update and its
triangles. The single-player Play page passes the client game's call 89
to it, moves it on by the game's clock (so it stops while the game
waits), fades it by the level's contents at the view's eye, and hands
its triangles to the scene drawer, which draws them after the view's
translucent surfaces with a material made for it (the picture as above).
The surface sprites' wind and weather follow it as above. Seen running:
`yavin_swamp`'s rain falls in its open swamp, faint streaks round the
player.

Not yet: snow (no 1.02a level asks for it), the rain's `fog`, `debug`
and `wind`; the random draws are Reluva's own, so the drops and gusts
aren't where the original's would be.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the command parser
  (0x48ed50), the rain's constructor, commands, update and render
  (0x48e210 to 0x48ec21), the world effects pass (0x48eca0), the surface
  sprites' frame (0x4882ef on); the GPL Jedi Outcast single-player
  source's `tr_worldeffects.cpp` as a guide to the classes' shape. Which
  levels send commands: a run of each with the client game, printing its
  call 89.
- **How it was checked:** a unit test (`idtech3.unit.input`, "world
  effects") runs the commands, the fade by contents, the drops' falling
  and their triangles; `yavin_swamp` on the Play page, the rain seen in
  Reluva's window.
