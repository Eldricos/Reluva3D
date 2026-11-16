# Jedi Outcast: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- Fonts are laid out as the GPL Jedi Outcast renderer lays them out;
  the Text page reads right, but it hasn't been compared with the
  original's screens ([fonts.md](../../formats/fonts.md)).
- `videoMap` textures play their video as the GPL source describes;
  none has been seen drawn in a level yet (kejim_post's screens are deep
  in it) ([shader.md](../../formats/shader.md)).
- Ghoul2 models are posed as the GPL Jedi Outcast renderer poses them,
  the replacing angle override (0x4) included; they look right in the
  Models page and in a match, but haven't been compared with the
  original's screens ([ghoul2.md](../../formats/ghoul2.md)).
- Surface sprites, light styles and the one-shot animMaps are drawn as
  the GPL Jedi Outcast renderer describes them, which isn't 1.02a's own
  code; Yavin's levels look as expected in `reluva-viewer`, but haven't
  been compared with the original running
  ([shader.md](../../formats/shader.md), [rbsp.md](../../formats/rbsp.md)).

- The console's chat line (its big characters and widths) and its
  typing line's scroll follow the GPL source, not traced in `jk2mp.exe`
  ([systems/console.md](systems/console.md)).
- The multiplayer menus' text: a number of letters as `R_Font_DrawString`'s
  limit, and the drop shadow's offset and grey, as the GPL multiplayer
  renderer has them; the menus read right, but haven't been compared
  with the original's screens
  ([systems/syscalls.md](systems/syscalls.md)).

- The network protocol's level changes, downloads and long matches, and
  a Reluva server's heartbeats to a real master, haven't been tried
  against the retail game ([systems/network.md](systems/network.md)).

## Seen running, not traced in the code

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

## Guessed

None yet.

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
