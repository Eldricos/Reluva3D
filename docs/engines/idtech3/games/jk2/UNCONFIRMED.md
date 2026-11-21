# Jedi Outcast: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- The replacing Ghoul2 angle override (0x4) follows the GPL renderer;
  no 1.02a multiplayer screen shows it ([ghoul2.md](../../formats/ghoul2.md)).
- Surface sprites, light styles and the one-shot animMaps are drawn as
  the GPL Jedi Outcast renderer describes them, which isn't 1.02a's own
  code; Yavin's levels look as expected in `reluva-viewer`, but haven't
  been compared with the original running
  ([shader.md](../../formats/shader.md), [rbsp.md](../../formats/rbsp.md)).
- Single player's scene entity layout and its kinds of entity follow
  the GPL source; Ghoul2 models are seen drawn in `kejim_post`, the
  other kinds (its effects' sprites, lines, electricity and cylinders)
  not yet ([systems/sp-interface.md](systems/sp-interface.md)).
- Saved games are written in `jk2sp.exe`'s order and form, read from
  its code; none written by Reluva has been loaded in the original yet,
  nor one of the original's loaded here ([systems/sp-saves.md](systems/sp-saves.md)).
- The network protocol's level changes, downloads and long matches, and
  a Reluva server's heartbeats to a real master, haven't been tried
  against the retail game ([systems/network.md](systems/network.md)).
- The weather (rain, snow, winds, misty fog) is drawn as `jk2sp.exe`'s
  and `jk2mp.exe`'s code says, but hasn't been compared with either
  running: no shipped multiplayer level has the "outside" brushes `r_we`
  needs, and no release has the fog's picture
  ([systems/weather.md](systems/weather.md)).

## Seen running, not traced in the code

- Single player's menus place text, draw multi values and sliders, scroll
  and double-click list boxes as Team Arena's menu code does, not traced
  in `jk2sp.exe`; side by side with the original's screenshots (below)
  they match ([systems/sp-menus.md](systems/sp-menus.md)).
- The console's typing line scrolls as the GPL source's does, not traced
  in `jk2mp.exe` (its width is, 0x40c366); single player's matched the
  original's screenshot ([systems/console.md](systems/console.md)).
- Side by side with the retail game's screenshots (November 2026, 800 x
  600, windowed): the main menu, Setup's Player and Video pages, Join
  Server, Create Server and Controls; the HUD, the scoreboard (a long
  name cut where the original cuts it), the message as a player joins;
  the console and a chat line; and Kyle posed with saber and pistol from
  behind, the front and the side, crouching and running. Fonts (Raven's
  and the console's), the menus' text and its shadows, and the Ghoul2
  poses all match. What differed and was fixed: the level twice as bright
  (in a window the original never brightens it,
  [rbsp.md](../../formats/rbsp.md#in-reluva)), the saber's glow not
  drawn ([efx.md](../../formats/efx.md)), and the video settings'
  defaults. Seen again with those fixes, both games started on their
  default settings: the level's brightness and the saber's glow match,
  and Create Server matches line for line. The Video page then differed
  in texture detail and filter; with `r_picmip` (1) and the filter
  (bilinear) taken from their registration (0x47edcf, 0x47f18c), not a
  quality preset, it matches too (a third shot).
- Single player side by side with the retail game's screenshots and a
  recording (November 2026, 800 x 600, windowed): the main and new game
  menus and the logo, the loading screen, the opening crawl and
  kejim_post's opening scene (subtitles and widescreen bars), the
  in-game save and load pages, the datapad's mission, weapons and Force
  pages, and yavin_canyon's and yavin_swamp's fog from six places. What
  differed and was fixed: the levels' empty sky black instead of the
  fog's colour, models unfogged, Raven's `GE192` alpha test unknown (the
  swamp's grass sprites hid what stood behind them), and list columns
  and edit fields limited by letters instead of pixels. Still open: the
  datapad's frame (`gfx/hud/datapad2`, 1024 x 1024, drawn at 640 x 480)
  looks softer in the original, as if it shrank through mipmaps, and
  the Force page's icons (128 x 128, drawn far smaller) have a wider blue
  halo there. The client game registers the frame without mipmaps (its
  call 43, `R_RegisterShaderNoMip`; jk2sp.exe passes the flag at
  0x47fe04), so why isn't known: something else in how the original
  uploads or filters its images.
- The network protocol beyond its tables and Huffman code (which are
  jk2mp.exe's own): a match played against the retail game both ways,
  on Windows (November 2026), Reluva hosting and joining
  ([systems/network.md](systems/network.md)).

## Inferred

- A save is refused while a cinematic outside the game plays, as the
  client isn't in the level then; the original's check reads only the
  in-game cinematic's flag and the client's state
  ([systems/sp-saves.md](systems/sp-saves.md)).

- A saber's trail's second triangle keeps the first's alphas for its
  first two corners, as the GPL source's one array of corners would;
  jk2mp.exe's own trail code wasn't traced
  ([efx.md](../../formats/efx.md)).

## Guessed

Nothing at present.

## Settled

- `videoMap` textures play their video: kejim_post's Mon Mothma
  hologram (`textures/video/mon_mothma`) matched the original's
  screenshots ([shader.md](../../formats/shader.md)).
- The level's fog (`yavin_canyon`, `yavin_swamp`) covers everything,
  fogged by distance from the eye: six places compared with the
  original's screenshots matched once frames cleared to its colour
  ([rbsp.md](../../formats/rbsp.md)).
- Single player plays a level's music from its start. A recording of
  the retail game (November 2026: a new game, kejim_post) matched
  against the level's tracks: `impbaseb_explore` plays in step with it
  from within a few seconds of the level's opening scene starting, under its
  dialogue, and loops when it ends, as Reluva's first
  `S_StartBackgroundTrack` starts it. How the original keeps playing
  once its files are let go (0x4275a7) is still unread. In a fight later
  (the recording's 280 to 300 seconds) the explore music stops; none of
  the level's tracks is heard clearly through the gunfire, and about
  20 seconds after the last shot the explore track comes back part way
  through ([systems/sp-music.md](systems/sp-music.md)).
- The game reads shader scripts in `shaders/` only, not `scripts/`:
  `jk2mp.exe` lists `shaders` for `.shader` files (0x490391, the only
  folder it names for them), and neither executable holds the name
  `scripts`; the paks have the 29 scripts there. Reluva reads only
  `shaders/` for it ([shader.md](../../formats/shader.md)).
- The multiplayer engine runs `mpdefault.cfg`, then the player's config
  (`jk2mpconfig.cfg`, unless in safe mode), then `autoexec.cfg`
  (0x429aac–0x429ad4); `jk2sp.exe` and Elite Force's engines run
  `autoexec.cfg` too ([systems/syscalls.md](systems/syscalls.md)).
- Multiplayer's bots use their own waypoints (`botroutes/*.wnt`), not
  Quake III's botlib areas: the paks hold no `.aas` file, and
  `jk2mpgame.qvm`'s only area-system call is 303, an entity's
  information, which needs no `.aas`
  ([systems/game-calls.md](systems/game-calls.md)).

- Multiplayer 1.02a has no Ghoul2 collision: hits and the saber's
  contacts are the entities' boxes. `jk2mp.exe`'s trace call (game call
  24, 0x44ae57) passes the game's ninth and tenth arguments on as the
  trace flags and level of detail, but the QVM pushes only seven, and
  the engine never reads them: its entity clipping (0x4508f0) and
  `SV_ClipToEntity` (0x450840) only trace the boxes and keep the
  nearest, and `CM_Trace` clears the trace's 16 Ghoul2 records
  (0x426aa1), so they come back all 0. Neither module makes a Ghoul2
  collision call (`qvm-info`: the client game's 278–299 and the game's
  582–599 are the instance, bolt and animation calls). The GPL source's
  Ghoul2 collision in `SV_ClipMoveToEntities` and the client game's
  `CG_G2TraceCollide` come from a later version.

- Multiplayer 1.02a speaks network protocol 15: `jk2mp.exe` registers
  `protocol` as 15 (the value pushed at 0x44dde9), as its recording's
  name (`demos/JK2FFA.dm_15`) suggested.
- The multiplayer engine registers the server's settings before any
  game, with the defaults its calls give (`sv_hostname` `noname`,
  `sv_maxclients` 8 latched and not archived, `fraglimit` 20...), read
  from `jk2mp.exe` ([systems/syscalls.md](systems/syscalls.md)).
