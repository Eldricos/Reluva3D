# Jedi Outcast: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- Ghoul2 models are posed as the GPL Jedi Outcast renderer poses them;
  they look right in the Models page, but haven't been compared with the
  original ([ghoul2.md](../../formats/ghoul2.md)).
- Surface sprites, light styles and the one-shot animMaps are drawn as
  the GPL Jedi Outcast renderer describes them, which isn't 1.02a's own
  code; Yavin's levels look as expected in `reluva-viewer`, but haven't
  been compared with the original running
  ([shader.md](../../formats/shader.md), [rbsp.md](../../formats/rbsp.md)).

## Seen running, not traced in the code

None yet.

## Inferred

- Multiplayer 1.02a speaks network protocol 15: the recording in its paks
  is `demos/JK2FFA.dm_15`, and Quake III names recordings by protocol.
  To check in `jk2mp.exe` before the protocol is written
  ([PLAN.md](PLAN.md), phase 6).
- The game reads shader scripts in `shaders/` only, not `scripts/`: its
  paks have its 29 scripts there and none in `scripts/`. Reluva reads only
  `shaders/` for it ([shader.md](../../formats/shader.md)). To check in
  `jk2sp.exe` and `jk2mp.exe`.

- The level's fog (`yavin_canyon`, `yavin_swamp`: a fog with no brush,
  named by the worldspawn's `fog` key) covers everything, fogged by
  distance from the eye, as a fog volume the eye is inside
  ([rbsp.md](../../formats/rbsp.md)). To compare with the original's
  pictures of those levels.

## Guessed

- Multiplayer's bots use their own waypoints (`botroutes/*.wnt`) rather
  than Quake III's botlib areas, as the paks hold no `.aas` file. To check
  by which bot calls `jk2mpgame.qvm` makes.

## Settled

None yet.
