# Jedi Outcast: how single player's DLL and the engine talk

> **Status:** partial · **Code:** `src/engines/idtech3/eliteforce/sp_game_host.cpp` (shared with Elite Force), `src/engines/idtech3/jk2/sp_game_calls.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; `jk2gamex86.dll` loading in Reluva · **Confidence:** from the code unless marked

## Overview

Single player's game logic is `jk2gamex86.dll` (exports `GetGameAPI`,
`dllEntry` and `vmMain`; imports only KERNEL32's and WINMM's, all of
which Reluva's Windows stand-ins provide). Like Elite Force's
`efgamex86.dll` it holds both the server game and the client game, and
Reluva runs it the same way, in its x86 interpreter, with the engine's
side answered by Elite Force's single-player host in a Jedi Outcast mode.
ICARUS, the effects system and NPC navigation are inside the DLL, not
the engine.

The GPL Jedi Outcast source's single-player `g_public.h` and
`cg_public.h` were read as a guide to the tables' shapes; every count,
place and layout below was checked in `jk2sp.exe` 1.02a. Calls are
cdecl; pointers are addresses in the emulated process.

## How it works

### Loading (0x4368e0 on)

1. The engine fills its table on the stack (0x4368e0–0x436cf5) and
   calls `GetGameAPI` (looked up at 0x440703) with it.
2. The game's table comes back; its first word, the API version, must be
   **6** (0x436d58), as Elite Force's.
3. The same DLL's `dllEntry` is handed the client game's call handler
   (0x43eea0, which hands its arguments to 0x401570), and its `vmMain`
   kept (0x440740). `dllEntry` itself already calls the handler: it
   registers settings (call 3).

### The engine's functions (import table)

103 slots, the same count as the GPL source's `game_import_t`:

- **0–41:** as the Elite Force Expansion Pack engine's 42
  ([../../stvef/systems/sp-interface.md](../../stvef/systems/sp-interface.md)):
  `Printf` 0 … `FS_FreeFile` 15, `FS_GetFileList` 16,
  `AppendToSaveGame` 17, `ReadFromSaveGame` 18 (with a fourth argument
  and the length returned), its optional form 19, `SendConsoleCommand`
  20 … `EntityContact` 38, `VoiceVolume` 39 (an array, not a function).
  Two differ in their arguments: `trace` 29 takes two more after the
  content mask (a Ghoul2 trace type and level of detail), and `Malloc`
  40 takes a tag and whether to zero what it gives, before `Free` 41.
- **42–102:** Ghoul2's, in the GPL source's order: from
  `G2API_PrecacheGhoul2Model` 42 and `G2API_InitGhoul2Model` 43 to
  `RE_RegisterSkin` 101 and `RE_GetAnimationCFG` 102 (the names in
  `jk2/sp_game_calls.cpp`). *(Their order follows the GPL source; only
  the count was checked.)* Many take the instance as the game's
  `CGhoul2Info_v`, a handle into an array the engine owns and hands out
  through `TheGhoul2InfoArray` 83 as a C++ object with virtual
  functions, whose elements the game also reads directly.

The fill is one long run of stores into the table on the stack, with a
`push` halfway (0x436c02) that moves the later offsets by 4: slots 77
and 82 onward are written 4 bytes further up than their places.

### The game's functions (export table)

The API version, then 15 functions, then three words:

| Place | What |
|---|---|
| 0 | API version (6) |
| 1–13 | as Elite Force's Expansion Pack's: `Init(mapname, spawntarget, checksum, entities, levelTime, randomSeed, globalTime, savegameLoaded, loadTransition)` … `ConsoleCommand` |
| 14, 15 | `PrintEntClassname(clientNum)`, `ValidateAnimRange(start, end, speed)` |
| 16, 17, 18 | the entities, their size, how many are in use |

### Entities

The parts the engine shares, as `jk2sp.exe`'s `linkentity` (0x43a620)
reads them: the entity state is 252 bytes (Elite Force's 232, then a
model scale, a radius and a bolt word after the vehicle model); then the
player state pointer 0xfc, in use 0x100, linked 0x104, server flags
0x108, brush model 0x10c, mins 0x110, maxs 0x11c, contents 0x128,
absolute mins 0x12c and maxs 0x138, current origin 0x144 and angles
0x150, owner 0x15c, and the Ghoul2 instance 0x160 *(inferred: after the
owner, as the GPL source has it)*. The model index (0xa0)
and solid (0xb4) are where Elite Force's are.

### The client game's calls

Numbered 0 to 111 (0x401577 rejects any higher), through `dllEntry`'s
handler. The GPL source's `cgameImport_t` has 118, so 1.02a's numbering
differs from it somewhere; it's to be read from the handler's jump table
(0x401fd0).

## In Reluva

`SpGameHost::load(dll, jedi)` loads the DLL in Jedi Outcast's mode:
slots 0–41 answered as for the Expansion Pack, `Malloc` zeroing when
asked, the entity layout above, the export table's longer list.
`reluva-tool idtech3 sp-run <base folder> <map>` finds `jk2gamex86.dll`
beside the game's folder and runs it.

Not yet: Ghoul2's slots and the client game's calls stop the game with
the name or number of the call, the first being `dllEntry`'s call 3.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the table's fill
  (0x4368e0–0x436cf5), the version check (0x436d58), the client game's
  set-up (0x440740) and handler (0x43eea0, 0x401570), `linkentity`
  (0x43a620). The GPL Jedi Outcast single-player source (`g_public.h`,
  `cg_public.h`, `q_shared.h`) as a guide to the tables' order and the
  structures, *(published)* for the Ghoul2 slots' names.
- **How it was checked:** `sp-run` on `kejim_post` loads the DLL, its
  version is 6, and `dllEntry` runs until its first client game call.
