# Jedi Outcast: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- `videoMap` textures play their video as the GPL source describes;
  none has been seen drawn in a level yet (kejim_post's screens are deep
  in it) ([shader.md](../../formats/shader.md)).
- The replacing Ghoul2 angle override (0x4) follows the GPL renderer;
  no 1.02a multiplayer screen shows it ([ghoul2.md](../../formats/ghoul2.md)).
- The console's typing line's scroll follows the GPL source, not traced
  in `jk2mp.exe` ([systems/console.md](systems/console.md)).
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
- Single player's menus place text, draw
  multi values and sliders, scroll and double-click list boxes as Team
  Arena's menu code does, not traced in `jk2sp.exe`; the logo's model hasn't been seen drawn (no display here); drawn from the game's own menus they look right, but
  haven't been compared pixel by pixel with the original's screens
  ([systems/sp-menus.md](systems/sp-menus.md)).

- The weather (rain, snow, winds, misty fog) is drawn as `jk2sp.exe`'s
  and `jk2mp.exe`'s code says, but hasn't been compared with either
  running: no shipped multiplayer level has the "outside" brushes `r_we`
  needs, and no release has the fog's picture
  ([systems/weather.md](systems/weather.md)).

## Seen running, not traced in the code

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
- The network protocol beyond its tables and Huffman code (which are
  jk2mp.exe's own): a match played against the retail game both ways,
  on Windows (November 2026), Reluva hosting and joining
  ([systems/network.md](systems/network.md)).

## Inferred

- The level's fog (`yavin_canyon`, `yavin_swamp`: a fog with no brush,
  named by the worldspawn's `fog` key) covers everything, fogged by
  distance from the eye, as a fog volume the eye is inside
  ([rbsp.md](../../formats/rbsp.md)). To compare with the original's
  pictures of those levels.
- A save is refused while a cinematic outside the game plays, as the
  client isn't in the level then; the original's check reads only the
  in-game cinematic's flag and the client's state
  ([systems/sp-saves.md](systems/sp-saves.md)).

## Guessed

- A paused Ghoul2 animation in single player is posed as if it went on;
  the original's poses it where it was paused
  ([systems/sp-ghoul2.md](systems/sp-ghoul2.md)).

## Settled

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
