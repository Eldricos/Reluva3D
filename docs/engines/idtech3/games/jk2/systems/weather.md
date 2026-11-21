# Jedi Outcast: weather

> **Status:** partial · **Code:** `src/engines/idtech3/render/world_effects.cpp`, `src/engines/idtech3/client/weather_drawing.cpp`, `src/engines/idtech3/client/sp_play_page.cpp`, `src/engines/idtech3/client/play_page.cpp` · **Checked against:** `jk2sp.exe` and `jk2mp.exe` 1.02a's code, `jk2mp.exe` 1.04's; `yavin_swamp` and `ffa_bespin` running in Reluva · **Confidence:** from the code unless marked

## Overview

Both of Jedi Outcast's renderers keep the same weather system (an
earlier one than the later games'): rain or snow round the eye where
the level is open to the sky, with gusts of wind and a misty fog. Its
commands are text lines. Single player's client game sends them with
its call 89, `R_WorldEffectCommand(text)`: in 1.02a only
`yavin_swamp`'s `fx_rain` does, sending `rain init 500` as the level
starts. Multiplayer's renderer takes them from the console command
`r_we` (0x49c880), the rest of the line; nothing in its game modules
sends one. `jk2sp.exe` has the same command's code (0x48f020) but never
registers it.

The weather falls only in "outside" brushes (`textures/system/outside`,
contents 0x10000). Only two levels have any, both single player's
(`ns_streets`, `yavin_swamp`), so `r_we`'s weather shows in no shipped
multiplayer level, only in mods' levels.

The addresses below are 1.02a's. Multiplayer's 1.03 and 1.04 have the
same code, every function and constant, 0x4450 and 0x4380 bytes later
(their parsers at 0x4a09c0 and 0x4a08f0). The GPL Jedi Outcast multiplayer source's
`tr_WorldEffects.cpp` has the same classes and nearly the same numbers;
where it differs, the executables are followed.

## How it works

### Commands (0x48ed50, 0x49c570)

The first word names what's meant:

- **`rain` or `snow`**: `init <count>` makes the system afresh with that
  many drops or flakes, in place of any before; `remove` deletes it;
  anything else goes to the system (below), if there is one.
- **`debug wind`** shows the winds' boxes; **`debug blah`** runs `snow
  init 1000`, `snow alpha 1` and `snow fog`, a test.
- **`exec <text>`**, multiplayer's alone, runs the rest of the line as a
  console command.

A system first offers the line to each of its effects (its fogs and
winds), then reads it itself. Vectors are written in parentheses, `( 1 2
3 )`, as the shaders' (0x48c0e0); one missing a value or a parenthesis
prints a warning naming a shader and keeps what was read.

- **Rain** (0x48e530, 0x49be30): `fog` (a misty fog), `fall ( min max )`
  (its speed down, default -60 and -50 a second; kept only if both are
  read), `spread ( distance height )` (20 and 20), `alpha <a>` (single
  player's 0.15, multiplayer's 0.1), `height <h>` (a drop's length, 5),
  `angle <a>` (how far drops lean in the wind, 1).
- **Snow** (0x49af60): `wind ( origin ) ( velocity ) ( size )` (a wind in
  a box), `fog`, `alpha <a>` (0.09), `spread ( min ) ( max )` (the box
  round the eye, -600 -600 -200 to 600 600 250), `blowing duration <n>`
  (2), `blowing low <n>` (3), `blowing velocity ( min max )` (30 and 70),
  `blowing size ( x y z )` (1000 300 300). `velocity ( min ) ( max )`
  sets the spread too, not the velocities (-15 -15 -20 to 15 15 -70),
  which no command changes.
- **Fog** (0x499af0): `fog density <a>` (0.3), taken by the first misty
  fog there is; without one, `fog` makes one and the rest is ignored.

### Each frame (0x48eca0, 0x49c4d0)

After each view with the level is drawn (none without one, refdef_t's
flag 1; in single player, none while a video plays in the game, the
flag `CL_PlayCinematic` keeps, 0x4051e0), the frame's time (in seconds)
moves the weather on, the rain first then the snow, and each draws: its
effects, then its drops or flakes. The contents at the eye are read
once for all.

Single player's weather is freed as its renderer shuts down
(`RE_Shutdown`, 0x46f86a, calling 0x48ec60), which a level loading
does. Multiplayer's renderer never frees it: nothing but the parser,
the pass and the getters touches it, so it lasts from level to level
until `remove`.

### The rain (0x48e210 on, 0x49bb50 on)

- **Its picture:** `gfx/world/rain` (a `.jpg` in the paks), not
  mipmapped, drawn nearest. Single player's has its red copied into its
  alpha and is clamped (0x48e346); multiplayer's is drawn as it is,
  with no alpha of its own (0x49bc11).
- **Each drop:** an angle round the eye (0 to 2π), a distance from it (0
  to 20), a height (-20 to 40 at the start), and how fast each changes a
  second (±0.1, ±0.1, -60 to -50).
- **Each frame** (0x48e740, 0x49c040):
  - a gust's direction now and then (single player's every 200 to 449
    frames, multiplayer's 200 to 450): a random direction across, of
    length 1, its fogs told the wind as it blows; the wind turns towards
    it by the frame's time of the difference;
  - its effects moved on;
  - in "outside" brushes and not in water (4) it fades in at 0.5 a second
    to 1; otherwise out to 0;
  - while faded in at all, each drop moves on by its speeds; one fallen
    below -20 starts again at 40, its angle, distance and speeds drawn
    afresh.
- **Drawn** (0x48e9d0, 0x49c270), round the eye: each drop a triangle
  from where it is (`sin(angle) x distance` across, `cos(angle) x
  distance` along, its height up, leant by `height x angle x wind`),
  across by 0.2 of the view's left axis, and from there down by the
  drop's length against the wind; its picture's s 1 to 0 across, t 0 to
  1 down. White, its alpha the rain's (below the eye, the distance over
  the depth times it, at most it) times the fade; blended over what's
  there (source alpha, one minus source alpha), depth tested but not
  written, both sides.

### The snow (0x49aca0 on)

- **Its box** round the eye is cut into 16 x 16 x 8 cells, their sizes
  from the spread it starts with (a later `spread` doesn't change them).
  Flakes start far away, so the first frame places them all.
- **Each frame** (0x49b2f0):
  - its wind's direction now and then (200 to 450 frames): random
    across, of length 1, its fogs told it;
  - while any cell is open to the sky, its effects (winds, fogs) moved on;
  - its gust: off once its time runs out; when it's further below than
    a random "low" (3 to 9 seconds), a new one round the eye, across, each
    way at its own speed (30 to 70), in a box of the blowing size, for 2
    to 4 seconds;
  - when the box round the eye has moved more than 25 any way, the
    level's contents are read again at each cell's corner, and the flakes
    it left are brought into the part it moved into, falling afresh (-70
    to -20 a second);
  - with no cell open, nothing more. Otherwise each flake drifts: across,
    back towards -15 to 15 by 18 a second when outside it, else by a
    random -1.4 to 1.4; down, by 36 at once while slower than 20 a
    second. It moves on; one under the box, or out of its cells, starts
    again at the top (or anywhere in the box's height, if that's out of
    it), anywhere across, falling afresh; one in an open cell is drawn.
- **Drawn** (0x49ba70), only while a cell is open: its effects, then
  each flake a point, grey (0.8) at the snow's alpha, blended, untextured,
  10 pixels over the square root of 1 + 0.0004 x the distance squared,
  1 to 4 (0x49babf on; 2 without the point parameters extension).

### Winds (0x49a010 on)

A wind has a point, a velocity and a size; its box is three planes: along
the velocity, towards the point, and across both, its sides the size
(across and up doubled). Each frame (0x49a320), unless it's off or the
eye is more than 300 from its point, each flake in its box (any flake,
for the gust) has the velocity times the frame's time added to its own.
A flake it misses stops it for that frame: the loop tries the same flake
again till its count runs out. With `debug wind`, its three near sides
are drawn red, green and blue, half see-through (0x49a500).

### The misty fog (0x499910 on)

- **What it is:** a grid of 30 x 30 points over the view, drawn in a
  projection of its own (80 degrees, square, 4 to 2048: 0x499e51) at 10
  in front of the eye, each a little off its place; white, added to
  what's there by its alpha.
- **Its picture:** `gfx/world/fog2`. No release's paks hold one, so in
  the shipped games `fog` ends the level with "Could not load
  gfx/world/fog2" (0x4994cc).
- **Its windows:** two pairs of square windows onto the picture (0.15 to
  0.3 across), each pair one showing and one waiting. Each frame, with
  no regard to its length, a window drifts with the wind as the view
  sees it (across by the wind's left part, larger or smaller by its
  forward part); grown or shrunk out of its sizes, it fades out and its
  partner fades in elsewhere (by 0.01 a frame each).
- **Each frame** (0x499b70): in the open and out of water it fades in
  over two seconds, else out; while faded in, each point's alpha is the
  picture's byte where each showing window puts it, times the window's
  alpha, the density and the fade, over 255 (0x499c70). The picture is
  read a byte a point, as its RGBA bytes are, wrapping.

### The surface sprites (0x4882ef on, 0x495c86 on)

Both programs' code is the same, through the same getters (single
player's 0x48f050 to 0x48f0f0, multiplayer's 0x49c8b0 to 0x49c9b6).
While rain falls on the eye or snow is falling (a cell open), weather
sprites (`ssFXWeather`) show in full; otherwise as `r_surfaceWeather`
says. With rain, the wind is as strong as the rain leans (its angle x 75,
x 0.3), gusting every 300 over that seconds (none if under 1); otherwise
as `r_windSpeed` and `r_windGust` say. It blows towards the yaw the
rain's wind blows to, else the snow's, else `r_windAngle`'s.

## In Reluva

`WorldEffects` is the system: its commands, rain, snow, winds and misty
fog, and what each frame draws, as triangles of three looks (the rain's
picture, plain, the fog). `WeatherDrawing` makes their materials and
hands each frame's to the scene drawer, which draws them after the view's
translucent surfaces, the fog in its own projection. The single-player
Play page passes the client game's call 89 to it and moves it on by the
game's clock (so it stops while the game waits, and while a video plays
in the game, which has the screen); it's cleared as a level loads, as
the original's renderer frees it then. The multiplayer Play page has `r_we` and moves it on by the view's
clock; it's kept from level to level, as the original's is. A missing fog
picture ends the level, or the match, once the frame is over.

Differences: the random draws are Reluva's own, so drops, flakes and
gusts aren't where the original's would be. Snowflakes are drawn as
squares facing the eye, as large as the original's points. A box from
`snow wind` is given the list of counts the original never makes for it
(the original would read one that isn't there *(inferred)*: a crash once
the eye comes within 300 of it).

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the command parser
  (0x48ed50), the rain (0x48e210 to 0x48ec21), the snow (0x48d310), the
  misty fog (0x48bf10), the world effects pass (0x48eca0) and its check
  for a video playing (0x4051e0), the renderer's shutdown freeing it
  (0x46f86a, 0x48ec60), the getters (0x48f050 to 0x48f0f0), the image
  loader's file types (0x46b4f0), the surface sprites' frame (0x4882ef
  on). `jk2mp.exe` 1.02a: `r_we` (0x47f7dc, 0x49c880), the parser
  (0x49c570), the rain (0x49bb50 to 0x49c4c1), the snow (0x49aca0 to
  0x49bb45), the winds (0x499fb0 to 0x49a4f7), the misty fog (0x499400 to
  0x499f93), the pass (0x49c4d0), the vector parser (0x48c0e0), the image
  loader (0x47c050), the surface sprites (0x495c86 on), the getters
  (0x49c8b0 to 0x49c9b6). The GPL multiplayer `tr_WorldEffects.cpp` as a
  guide to the classes' shape. Which levels have "outside" brushes: a
  search of every map in the paks.
- **How it was checked:** a unit test (`idtech3.unit.input`, "world
  effects") runs the rain's commands, fade, drops and triangles; the
  snow's flakes in open cells only, its wind's direction, a wind's box
  and the gust's shown by `debug wind`; the fog's error without its
  picture and its grid with one; and `exec`. In Reluva's window:
  `yavin_swamp`'s rain; on `ffa_bespin` (made all "outside" for the
  test), `r_we` snow and rain falling, a gust's box, `snow fog` ending
  the match, and, with a made-up `fog2` picture, the fog over the view.
  Not compared with the original running.
