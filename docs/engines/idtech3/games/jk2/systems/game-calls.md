# Jedi Outcast: the multiplayer game module's system calls

> **Status:** partial · **Code:** `src/engines/idtech3/eliteforce/game_host.cpp` (shared with Elite Force), `src/engines/idtech3/jk2/game_calls.cpp` · **Checked against:** the code only (1.02a's `jk2mpgame.qvm`, run in Reluva, and `jk2mp.exe`) · **Confidence:** from the code unless marked

## Overview

Multiplayer's game logic is `vm/jk2mpgame.qvm` in `assets0.pk3`:
249,138 instructions in 1,112 functions. It reaches the engine through
numbered system calls in the order of the GPL Jedi Outcast source's
multiplayer `g_public.h`, read as documentation; each call below was
checked against what 1.02a's module passes (`qvm-info`, `game-run
-trace`). Calls 0–38 are numbered as Elite Force's, so Reluva runs both
games' modules with one host (`eliteforce::GameHost`), each with its own
layout (`GameLayout`) and its own calls past 38 (`jk2Call`).

The module's commands are Quake III's (0 init, 1 shutdown, 2 client
connect, 3 client begin, 4 userinfo changed, 5 disconnect, 6 client
command, 7 client think, 8 run frame, 9 console command, 10 bot frame),
with an 11th, a ROFF note track callback.

## How it works

### Its structures

The sizes are from what the module copies whole (`qvm-dis -blockcopies`);
the fields from the GPL source, checked where the running game shows
them.

| Structure | Bytes | Layout |
|---|---|---|
| `entityState_t` | 296 | Quake III's start (number, type, flags, two trajectories, times, origin 92, origin2 104, angles), then Raven's: bolts, force, saber, Ghoul2 fields; `modelindex` 212, `solid` 248 |
| `entityShared_t` | 112, from 296 | `linked` 296, `linkcount` 300, `svFlags` 304, `singleClient` 308, `bmodel` 312, `mins` 316, `maxs` 328, `contents` 340, `absmin` 344, `absmax` 356, `currentOrigin` 368, `currentAngles` 380, `mIsRoffing` 392, `ownerNum` 396, `broadcastClients` 400 |
| `playerState_t` | 1,368 | `origin` 20, `viewheight` 168 (the eye the server sees from) |
| `trace_t` | 1,080 | Quake III's 56 bytes, then 16 Ghoul2 collision records of 64 bytes, which 1.02a's engine leaves all 0: it never collides with Ghoul2 models ([UNCONFIRMED.md](../UNCONFIRMED.md), Settled) |
| `usercmd_t` | 28 | server time, angles 4, buttons 16, then a byte each: weapon, force power, item, generic command, forward, right, up |
| `bot_input_t` | 40 | think time, direction, speed, view angles, action bits 32, weapon 36 |

The game's entities are 828 bytes and its clients 1,756 (as it reports
them with `LocateGameData`).

### The calls past Elite Force's

| # | Call | What Reluva does |
|---|---|---|
| 39, 40 | debug polygons | nothing |
| 41 | `RealTime(qtime)` | the local time |
| 42 | `SnapVector(v)` | each part to the nearest whole number |
| 43, 44 | `TraceCapsule`, `EntityContactCapsule` | the mover a capsule ([bsp.md](../../../formats/bsp.md), Collision); unused by 1.02a's code |
| 45, 46 | `SP_RegisterServerCmd`, `SP_GetStringTextString` | 1; no text yet |
| 47–51 | ROFF: clean, update entities, cache, play, purge | recorded paths played on the game's entities: their trajectories set each frame ([roff.md](../../../formats/roff.md)) |
| 107–111, 114, 115 | matrix multiply, angle vectors, perpendicular vector, floor, ceil, acos, asin | as their names say |
| 200–211 | the bot library's setup, snapshot entities, console messages, a bot's user command | its user command is run as a player's (`ClientThink`) |
| 303 | `AAS_EntityInfo` | none: the module keeps its own |
| 400–425 | the elementary actions (say, attack, alt attack, force power, use, moves, jump, view...) and `EA_GetInput`, `EA_ResetInput` | a bot's input for the frame, with Quake III's newer action bits (attack 1, use 2, jump 0x10, crouch 0x80, forward 0x200, back 0x800, left 0x1000, right 0x2000, force power 0x100000, alt attack 0x200000); a held jump is let go for a frame |
| 525–563 | goal, move and weapon states | a handle; the module's own code does the thinking |
| 582–599 | Ghoul2: models, bolts, animation | as the client game's ([ghoul2.md](../../../formats/ghoul2.md), Animation): the saber's and hits' bolts where the posed body puts them |

Jedi Outcast's bots find their way by the module's own waypoints
(`botroutes/*.wnt`): it makes no area-system call but 303.

### Joining a match

A player who connects starts as a spectator, the module sending `nfr
<rank> 1 <team>` ("new force rank": the highest rank, show the force
menu, the team). They join with the `team` command once their force
powers are set. The module checks the player's `forcepowers` userinfo
(rank, side, then a digit for each power) against the server's
`g_maxForceRank` (6 by default): a higher rank is lowered and the player
put back to spectating to choose again. The client's default
(`7-1-032330000000001333`, rank 7) is above the default server's, so the
force menu always shows first.

The client's userinfo settings and their defaults are registered as
`jk2mp.exe` registers them (from 0x40c9a2): `name` `Padawan`, `rate`
4000, `snaps` 20, `model` and `team_model` `kyle/default`,
`forcepowers`, `color1` 4, `color2` 5, `handicap` 100, `teamtask`,
`sex` `male`, `cl_anonymous`, `password`, `cg_predictItems` 1.

## Sources and checks

- **Where the facts come from:** 1.02a's `jk2mpgame.qvm` and `cgame.qvm`
  (their calls with `qvm-info`, their whole-struct copies with `qvm-dis
  -blockcopies`), `jk2mp.exe` (the client's settings), and the GPL Jedi
  Outcast source's multiplayer `g_public.h`, `q_shared.h`, `botlib.h` and
  `w_force.c`, read as documentation; the code is new.
- **How it was checked:** `game-run` on `ffa_bespin`: the level starts
  (its light styles, music and items), the player joins with `team free`
  and runs forward at 250 units a second, and two bots (Tavion and
  Desann) join, spawn and move about. A game-files step (`jk2-game`)
  records both runs.

### Open questions

- The player state's fields past the few the engine reads haven't been
  checked against 1.02a's code (the network protocol needs them all).
- A ROFF's notes aren't handed to the game (command 11) yet.
