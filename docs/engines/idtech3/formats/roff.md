# id Tech 3: Raven's recorded paths (`.rof`, "ROFF")

> **Status:** decoded · **Code:** `src/engines/idtech3/raven/roff.cpp`, `jk2/game_calls.cpp`, `jk2/cgame_calls.cpp` · **Checked on:** Jedi Outcast 1.02a's 68 files (`reluva-tool idtech3 roff-check`, November 2026) · **Confidence:** the layout confirmed; how they're played published unless marked

## Overview

A ROFF ("rotation object file format") is a path recorded in a modelling
program: how far an object moves and turns in each frame. Jedi Outcast
plays them on movers: in single player its scripts do it (the game DLL's
own code); in multiplayer the engine does it for the game module and the
client game, as a `trigger_multiple` with `roffname` (and `rofftarget`,
the entity to move) asks when it's touched.

Jedi Outcast's 68 files are all in `scripts/`, for single player's
levels (Yavin's fighters, artus_mine's pipes...); no multiplayer map uses
one.

## Layout

Little-endian. Two versions; the header's first 4 bytes are `ROFF`.

| Offset | Size | Version 1 | Version 2 |
|---|---|---|---|
| 4 | 4 | i32 version (1) | i32 version (2) |
| 8 | 4 | f32 frame count | i32 frame count |
| 12 | 4 | frames start | i32 each frame's time in ms (named a frame rate in the source, used as a time) |
| 16 | 4 | | i32 note count |
| 20 | | | frames start |

A frame is f32[3] move (units), f32[3] turn (degrees: pitch, yaw, roll),
24 bytes; version 2's then has i32 its first note and i32 how many (32
bytes; −1 and 0 for none). Version 2's notes follow the frames: strings,
each ending in 0. Version 1's frames are 100 ms apart.

All of Jedi Outcast's are version 2, 50 ms a frame (20 a second), with
no notes; 7,060 frames in all.

## Behaviour

- **Reading:** a file is refused unless its version is 1 or 2 and its
  count above 0; the `ROFF` isn't checked (the original's test is the
  wrong way round). A turn over 180 or under −180 degrees has 360 taken
  off or added (the exporter writes some the long way round).
- **Caching:** files are cached by name and numbered from 1, the same
  number for the same name; one system serves the game module and the
  client game, so the number the server puts in an event is the client's
  too. A file is forgotten when neither side uses it after the level.
- **Playing:** an entity set playing starts at once; its starting angles
  are kept. Each time its next frame is due, its position's and angles'
  trajectories are set linear from where it is now, moving the frame's
  move and turn over a frame's time (so many a second), and the next
  frame is due a frame's time later. With `translate`, the move is along
  the entity's own axes as it faced at the start (x forward, y left, z
  up). After the last frame, both trajectories are set still where the
  entity is, and (in the game module) its `mIsRoffing` cleared. Purging an
  entity sets it still too.
- **Notes:** a frame's notes are cut at control characters, each piece
  handed to the game's code (the game module's command 11; the client
  game's export 22). Jedi Outcast's multiplayer game knows `loop`, which
  plays the ROFF again (from where the entity started, with an argument).

## In Reluva

`raven::parseRoff` reads a file and `raven::RoffSystem` caches and plays
them, told how to reach a side's entities: the game module's in its
memory (their trajectories, current origin and angles, `mIsRoffing` at
392), the client game's through its exports (an entity's origin 17 and
angles 18, its next state's trajectories 20 and 21). On the Play page's
own server both sides share one system.

Differences:

- The client game's exports are given an address in its shared buffer
  for the origin and angles, where the original engine passes a pointer
  of its own, which only its client game DLL could use; with the QVM
  Reluva runs, the client's side works where the original's couldn't.
- Notes aren't handed to either module yet (no file of Jedi Outcast's has
  one).
- The client game's side runs on the client game's clock; the original
  uses the server's even there.

## Sources and checks

- **Where the facts come from:** the layout from Jedi Outcast's 68 files;
  how they're read and played from the GPL Jedi Outcast multiplayer
  source (`qcommon/RoffSystem.cpp`, `game/g_trigger.c`, `game/g_utils.c`,
  `cgame/cg_main.c`, `cgame/cg_event.c`), read as documentation; the code
  is new.
- **How it was checked:** `roff-check` reads all 68 (recorded by the
  game-files step `jk2-roffs`); a unit test (`idtech3.unit.formats`,
  "roff") reads a made-up file with a note and a long-way turn, plays it
  (frames due and not, the other side's untouched, the end, a translated
  move, purging, cleaning). Nothing has played one in a match: no
  multiplayer map has a trigger with a ROFF.
