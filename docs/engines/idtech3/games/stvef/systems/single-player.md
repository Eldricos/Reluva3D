# Elite Force: running single player's DLLs

> **Status:** done · **Code:** `src/engines/idtech3/eliteforce/sp_game_host.{h,cpp}`, `src/engines/idtech3/pages/sp_play_viewer.cpp` · **Checked against:** the retail DLLs themselves, running in the emulator; parts of play against the original running (October 2026, [TESTING.md](../TESTING.md#last-checked)) · **Confidence:** confirmed unless marked

## Overview

Single player's game logic is two Windows DLLs, run as they shipped in
Reluva's [x86 emulator](../../../../../common/x86/README.md). How they and the
engine talk is [sp-interface.md](sp-interface.md); this page is how they were
brought to run.

## How it works

### First results with the retail DLLs (1.2)

- Both DLLs' start-up runs to completion: `efuix86.dll` in about 9,800
  instructions, `efgamex86.dll` in about 172,000. Every KERNEL32 function
  they call has a stand-in.
- `GetUIAPI` returns a table of 12 functions, without calling the engine.
- `GetGameAPI` returns a structure shaped like Jedi Outcast's
  `game_export_t` (codeJK2/game/g_public.h, GPL):
  - an API version, 6;
  - 13 functions (Jedi Outcast's 14 without `ConnectNavs`);
  - then `gentities` (0 until the game is started), `gentitySize` (0x420,
    1056 bytes) and `num_entities`.

  It makes two calls to the engine's table, to slots 14 and 15: it reads
  `ext_data/weapons.dat` (`FS_ReadFile`, then `FS_FreeFile`; at 15 and 16
  in Jedi Outcast's table).

The full interface between the DLLs and the engine, as later read from the
retail executable, is in
[sp-interface.md](sp-interface.md).

## In Reluva

### Running the single-player game

`src/engines/idtech3/eliteforce/sp_game_host.{h,cpp}` runs `efgamex86.dll`'s
server game:

- **Loading.** It loads the DLL, calls `GetGameAPI` with native functions
  in the engine's 40 slots, and checks the API version.
- **Starting a level.** It calls `Init` with the map's entity text, lets
  the player join (`ClientConnect`, then `ClientBegin`), and runs frames.
- **Reused from Holomatch.** Collision, entity linking, area portals and
  traces come from the Holomatch host, with the single-player entity layout.
- **Cvars.** The engine's cvar structures live in emulated memory and are
  kept up to date, since the game reads them directly.
- **printf-style calls.** `Printf`, `Error` and `SendServerCommand` are
  formatted from the emulated stack (`src/emulation/x86/format.cpp`).

`reluva-tool idtech3 sp-run <game folder> <map>` runs it and reports what the game
did. It finds the DLL beside the game folder unless given `-dll`.

Status: on borg1 the retail game starts the level and runs.

- `Init` parses its data files, starts ICARUS and spawns 561 entities,
  371 of them linked into the world. That's about 83 million
  instructions.
- The player joins.
- Frames run, about 250,000 instructions each.
- The game sets 103 configstrings: the music, models and so on.

The server game also calls into its client half (`cg_viewsize`, sounds),
through the system call function `dllEntry` hands it, so that's set up at
load.

Speed: about 50 million instructions a second in a Release build before
the decoded-instruction cache, about 1.5 times that with it. A Debug build
is much slower.

### Playing single player

The **Single player** page (`src/engines/idtech3/pages/sp_play_viewer.cpp`)
appears when `efgamex86.dll` is in the install folder. It lists the maps
that aren't Holomatch's and starts at borg1.

- **What it runs.** Each level runs the retail server and client game as
  `sp-run -cgame` does.
- **Frames.** The client game's scenes and pictures are drawn by the same
  scene drawer as the Play page's. The client game's view and scene
  entities are laid out much as in Holomatch: the 112-byte `refdef_t`,
  and a 152-byte `refEntity_t` with Quake III's numbering of its kinds
  (see [sp-interface.md](sp-interface.md),
  "Scene entities").
- **Input.** Keys and mouse go through the player's bindings, as on the
  Play page:
  - movement and buttons become the 28-byte user command;
  - other commands go to the client game first, then the server game.
- **Keys.** Ctrl+R restarts the level, Ctrl+PgUp/PgDn change map, Ctrl+I
  hides the info line, and Esc returns to the menus.

Also done: sound, with the ambient sound sets; wall marks
(`CM_MarkFragments`); lighting from the map's light grid
(`R_LightForPoint`); rotated pictures; saving and loading games, with
the saved-game picture; cinematics; and the menus' single-player screens
(`efuix86.dll`).

## Sources and checks

- **Where the facts come from:** the retail DLLs (1.2), run in the emulator;
  the retail executable, for the interface ([sp-interface.md](sp-interface.md)).
