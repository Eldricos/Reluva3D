# Jedi Outcast: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

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

## Guessed

- Multiplayer's bots use their own waypoints (`botroutes/*.wnt`) rather
  than Quake III's botlib areas, as the paks hold no `.aas` file. To check
  by which bot calls `jk2mpgame.qvm` makes.

## Settled

None yet.
