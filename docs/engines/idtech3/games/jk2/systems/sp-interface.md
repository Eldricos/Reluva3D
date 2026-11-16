# Jedi Outcast: how single player's DLL and the engine talk

> **Status:** partial · **Code:** `src/engines/idtech3/eliteforce/sp_game_host.cpp` (shared with Elite Force), `src/engines/idtech3/jk2/sp_game_calls.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; `jk2gamex86.dll` running in Reluva up to its first Ghoul2 call · **Confidence:** from the code unless marked

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
handler; read from its jump table (0x401fd0), each case's code and what
it calls (the renderer's through its table at 0x7ff770, in the GPL
source's `refexport_t` order). The GPL source's `cgameImport_t` has 118:
1.02a has four force feedback calls where it has ten, no
`AnyLanguage_ReadFromString`, and one call it lacks (81).

| Number | Calls | As Elite Force's |
|---|---|---|
| 0–25 | print, error, time, settings, arguments, files, commands, screen update, collision (load, models, contents, traces, marks) | the same numbers |
| 26 | `CM_SnapPVS(origin, buffer)`: the clusters seen from there, a bit each | — |
| 27–34 | sound: start, start local, clear and add looping, entity position, respatialize, register, background track | 26–33 |
| 35–38 | force feedback: three of one argument, one of none | 34–37 |
| 39–43 | `R_LoadWorldMap`, `R_RegisterModel`, `R_RegisterSkin`, `R_RegisterShader`, `R_RegisterShaderNoMip` | 38–42 |
| 44–48 | fonts: register, width in pixels, letters, height, draw (Raven's, as multiplayer's) | — |
| 49, 50 | `Language_IsAsian`, `Language_UsesSpaces` | — |
| 51–62 | clear scene, add entity, lighting at a point, polygon, light, render, colour, picture, screenshot picture, model bounds, tag, turned picture | 43–54 |
| 63 | `R_DrawRotatePic2`: a picture turned about its centre | — |
| 64, 65 | `R_LAGoggles`, `R_Scissor` | — (55) |
| 66 | `GetGlconfig`: 84 bytes (0x401020), four text pointers then the numbers, the screen size at 56 and 60 | 56, its own layout |
| 67–73 | game state (20,100 bytes, 0x401000), snapshot number, snapshot, server command, command number, user command (28 bytes), `SetUserCmdValue` | 57–63 |
| 74 | `SetUserCmdAngles(pitch, yaw, roll)` | — |
| 75–80 | ambient sets: update, local set, parse, add entry, brush model sound; `S_GetSampleLength` | 65–70 |
| 81 | two points (0x415ba0) the engine writes out as six numbers, to record a path | — |
| 82–85 | Ghoul2: list bones, list surfaces, `HaveWeGhoul2Models`, `SetModels` | — |
| 86–89 | light styles get and set, a brush model's corners, `R_WorldEffectCommand` | — |
| 90–94 | cinematics: play, stop, run, draw, set extents | — |
| 95, 96 | `Z_Malloc(size, tag)` (0x414960, zeroed), `Z_Free` | — |
| 97–107 | the menus' parser: reset, new menu, parse int, string and float, start and end a session, `PC_ParseExt`, paint all, string init, menu info | — |
| 108 | `SP_Register(package, flags)` (0x431e40; the second argument made 1 or 12) | — |
| 109, 110 | the same case (0x401f63): text by reference, or (110) by number (0x432290: its high byte the package's number, its low byte the entry's), copied into a buffer; the length returned | — |
| 111 | `UI_GetItemText(menu, item, buffer)` | — |

The structures handed over: a snapshot (0x401120) has its flags, ping,
server time and area mask as Elite Force's, the command number at 44,
the player state at 48 (948 bytes), the entity count at 0x3e4, up to 256
entity states of 252 bytes from 0x3e8 and the server command sequence
at 0xfff4; a scene entity (`refEntity_t`) is 168 bytes *(from the GPL
source's layout)*, Elite Force's 136 then the Ghoul2 angles, model scale
and instance pointer; the view (`refdef_t`) is Quake III's, without
Elite Force's text.

## In Reluva

`SpGameHost::load(dll, jedi)` loads the DLL in Jedi Outcast's mode:
slots 0–41 answered as for the Expansion Pack, `Malloc` zeroing when
asked, the entity layout above, the export table's longer list.
`reluva-tool idtech3 sp-run <base folder> <map>` finds `jk2gamex86.dll`
beside the game's folder and runs it.

The client game's calls are answered as the table above says, Elite
Force's handling shared where the call and what it passes are the same
(`SpGameHost::cgameCall` with Elite Force's number), the snapshot and
scene entity sizes per game (`SpGameHost::CgameLayout`). Not yet
answered: the Ghoul2 calls (84, 85), a brush model's corners (88), the
cinematics (90–94), the menus' parser (97–107) and item text (111),
which stop the game with their number, as Ghoul2's engine slots do with
their names. With them, `sp-run` on `kejim_post` runs `dllEntry`, the
game's `Init` (its NPC definitions and ICARUS start) and stops at its
first Ghoul2 call, `G2API_CleanGhoul2Models`.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the table's fill
  (0x4368e0–0x436cf5), the version check (0x436d58), the client game's
  set-up (0x440740) and handler (0x43eea0, 0x401570), `linkentity`
  (0x43a620). The GPL Jedi Outcast single-player source (`g_public.h`,
  `cg_public.h`, `q_shared.h`) as a guide to the tables' order and the
  structures, *(published)* for the Ghoul2 slots' names.
- **How it was checked:** `sp-run` on `kejim_post` loads the DLL, its
  version is 6, `dllEntry`'s client game calls are answered and the
  game's `Init` runs until its first Ghoul2 call. The client game's own
  start (`CG_Init`) needs the level started, so its calls beyond
  `dllEntry`'s haven't run yet.
