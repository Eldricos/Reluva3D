# Elite Force single player: how the DLLs and the engine talk

Single player's game logic lives in two native DLLs that the x86 interpreter
runs (see [../../../common/x86.md](../../../common/x86.md)):

- `efgamex86.dll` holds the server game and, in the same DLL, the client
  game (cgame).
- `efuix86.dll` holds the menus.

This page lists what each passes the other, as found in the 1.2 release.
The sources:

- The retail executable (`stvoy.exe`), which builds the tables the DLLs
  get. It was read for interoperability, as CONTRIBUTING.md allows where
  local law permits.
- Each engine function was identified by its own error messages and by
  what it does.
- Jedi Outcast's GPL source (OpenJK's `codeJK2`) confirmed the names. It
  comes from the same studio and engine generation, and its interfaces
  turned out to be close relatives of these.

Raven's Elite Force source was not used. "JK2" below means Jedi Outcast's
single player.

Calls are cdecl throughout. Pointers are addresses in the emulated
process's memory.

## The server game (`efgamex86.dll`)

The engine calls `GetGameAPI(import)` with a table of 40 engine functions.
It returns a pointer to the game's table. While running, `GetGameAPI`
reads `ext_data/weapons.dat` through slots 14 and 15.

### The engine's functions (import table)

| Slot | Function | Notes |
|---|---|---|
| 0 | `Printf(fmt, ...)` | |
| 1 | `WriteCam(text)` | appends to `maps/<map>_cam.map` |
| 2 | `Error(level, fmt, ...)` | doesn't return |
| 3 | `Milliseconds()` | |
| 4 | `Cvar(name, value, flags)` → `cvar_t*` | the engine's own cvar structure: string at +4, value at +0x1c, integer at +0x20 |
| 5 | `Cvar_Set(name, value)` | |
| 6 | `Cvar_VariableIntegerValue(name)` | |
| 7 | `Cvar_VariableStringBuffer(name, buffer, size)` | |
| 8, 9 | `Argc()`, `Argv(n)` → string | `Argv` returns a pointer |
| 10–13 | `FS_FOpenFile(path, &handle, mode)`, `FS_Read(buffer, length, handle)`, `FS_Write(buffer, length, handle)`, `FS_FCloseFile(handle)` | |
| 14, 15 | `FS_ReadFile(path, &buffer)` → length, `FS_FreeFile(buffer)` | |
| 16 | `AppendToSaveGame(chunkId, data, length)` | |
| 17 | `ReadFromSaveGame(chunkId, buffer, length)` | Chunk IDs are four characters (the engine's own are `TIME` and `TIMR`). |
| 18 | `SendConsoleCommand(text)` | |
| 19 | `DropClient(clientNum, reason)` | |
| 20 | `SendServerCommand(clientNum, fmt, ...)` | |
| 21, 22 | `SetConfigstring(index, text)`, `GetConfigstring(index, buffer, size)` | |
| 23, 24 | `GetUserinfo(n, buffer, size)`, `SetUserinfo(n, text)` | |
| 25 | `GetServerinfo(buffer, size)` | |
| 26 | `SetBrushModel(entity, name)` | |
| 27 | `Trace(results, start, mins, maxs, end, passEntity, contentMask)` | |
| 28 | `PointContents(point, passEntity)` | |
| 29, 30 | `InPVS(p1, p2)`, `InPVSIgnorePortals(p1, p2)` | |
| 31 | `AdjustAreaPortalState(entity, open)` | |
| 32 | `AreasConnected(area1, area2)` | |
| 33, 34 | `LinkEntity(entity)`, `UnlinkEntity(entity)` | |
| 35 | `EntitiesInBox(mins, maxs, list, max)` | |
| 36 | `EntityContact(mins, maxs, entity)` | |
| 37 | `VoiceVolume`: not a function but a pointer to an array of ints, one per entity | the sound code writes how loud each entity's voice is |
| 38 | `Malloc(size)` | Unlike JK2, there's no tag or "zero it" argument. |
| 39 | `Free(pointer)` | |

This is JK2's list without `FlushCamFile`, `FS_GetFileList`, `totalMapContents`, `bIsFromZone` and the Ghoul2 functions.

### The game's functions (export table)

| Offset | Field |
|---|---|
| 0 | API version: 6. The engine refuses anything else ("game is version %i, not %i"). |
| 1 | `Init(mapname, checksum, entityString, levelTime, randomSeed, globalTime, savegameLoaded)` |
| 2 | `Shutdown()` |
| 3 | `WriteLevel(autosave)` |
| 4 | `ReadLevel(arg)` |
| 5 | `GameAllowedToSaveHere()` |
| 6 | `ClientConnect(clientNum, firstTime, savegameLoaded)` → refusal text or 0 |
| 7 | `ClientBegin(clientNum, usercmd, savegameLoaded)` |
| 8 | `ClientUserinfoChanged(clientNum)` |
| 9 | `ClientDisconnect(clientNum)` |
| 10 | `ClientCommand(clientNum)` |
| 11 | `ClientThink(clientNum, usercmd)` |
| 12 | `RunFrame(levelTime)` |
| 13 | `ConsoleCommand()` |
| 14 | `gentities`: the entities, set by the game |
| 15 | `gentitySize`: 0x420 (1056) |
| 16 | `num_entities` |

The fields are 4 bytes each, so offset n is at byte 4n.

`Init`'s arguments are, in order:

- the `mapname` cvar's text;
- `sv_mapChecksum`'s integer;
- the map's entity string, copied from the BSP;
- the server's time;
- a time taken at start-up;
- the current time;
- whether a saved game was just loaded.

That's JK2's `Init` without `spawntarget` and `qbLoadTransition`.

The player state the server copies out of each client is 544 bytes (136
words).

## The client game (cgame, also `efgamex86.dll`)

This is Quake III's native-DLL convention:

- the engine calls `dllEntry(syscall)` with its system call function;
- it calls `vmMain(command, args...)` to run the client game.

### Commands (the engine to the cgame)

| Command | Meaning |
|---|---|
| 0 | `Init(serverMessageNum)` |
| 1 | `Shutdown()` |
| 2 | `ConsoleCommand()` → handled |
| 3 | `DrawActiveFrame(serverTime, stereo)` |
| 4 | `CrosshairPlayer()` |
| 5 | `CameraPos(vec3 out)` → whether a cinematic camera is running. The server calls it while building a snapshot, to see from the camera rather than the player. |

### System calls (the cgame to the engine)

`syscall(number, args...)`, 71 of them. Calls 0 to 25 are numbered as in JK2:

| Number | Call |
|---|---|
| 0–2 | `Print(text)`, `Error(text)`, `Milliseconds()` |
| 3–5 | `Cvar_Register(vmCvar, name, value, flags)`, `Cvar_Update(vmCvar)`, `Cvar_Set(name, value)` |
| 6–8 | `Argc()`, `Argv(n, buffer, size)`, `Args(buffer, size)` |
| 9–12 | `FS_FOpenFile`, `FS_Read`, `FS_Write`, `FS_FCloseFile` |
| 13–15 | `SendConsoleCommand(text)`, `AddCommand(name)`, `SendClientCommand(text)` |
| 16 | `UpdateScreen()` |
| 17–25 | collision: `CM_LoadMap(name)`, `CM_NumInlineModels()`, `CM_InlineModel(n)`, `CM_TempBoxModel(mins, maxs)`, `CM_PointContents(point, model)`, `CM_TransformedPointContents(point, model, origin, angles)`, `CM_BoxTrace` (7 arguments), `CM_TransformedBoxTrace` (9), `CM_MarkFragments` (7) |
| 26–33 | sound: `StartSound(origin, entity, channel, sfx)`, `StartLocalSound(sfx, channel)`, `ClearLoopingSounds()`, `AddLoopingSound` (4), `UpdateEntityPosition(entity, origin)`, `Respatialize` (4), `RegisterSound(name)`, `StartBackgroundTrack(intro, loop)` |
| 34–37 | force feedback (the engine's code for `FFC10.dll`): start (effect), stop (effect), a third taking an effect, stop all. Reluva can ignore these. |
| 38–42 | `R_LoadWorldMap`, `R_RegisterModel`, `R_RegisterSkin`, `R_RegisterShader`, `R_RegisterShaderNoMip` |
| 43–48 | `R_ClearScene`, `R_AddRefEntityToScene`, `R_LightForPoint` (4), `R_AddPolyToScene` (3), `R_AddLightToScene` (5), `R_RenderScene(refdef)` |
| 49, 50 | `R_SetColor(rgba)`, `R_DrawStretchPic` (9) |
| 51 | `R_DrawScreenShot(x, y, w, h)`: the saved-game screenshot, through the renderer's raw-image drawing |
| 52, 53 | `R_ModelBounds(model, mins, maxs)`, `R_LerpTag` (6) |
| 54 | `R_DrawRotatePic` (10 arguments) |
| 55 | renderer entry 18 (4 arguments); JK2 suggests `R_Scissor(x, y, w, h)` |
| 56, 57 | `GetGlconfig(out)` (5196 bytes), `GetGameState(out)` (20100 bytes, Quake III's `gameState_t`) |
| 58–62 | `GetCurrentSnapshotNumber(&n, &time)`, `GetSnapshot(n, out)`, `GetServerCommand(n)`, `GetCurrentCmdNumber()`, `GetUserCmd(n, out)` |
| 63 | `SetUserCmdValue(weapon, sensitivity)` |
| 64 | `MemoryRemaining()` |
| 65–69 | ambient sound sets, as JK2: `S_UpdateAmbientSet(name, origin)`, `S_AddLocalSet` (5), `AS_ParseSets()` (reads `sound/sound.txt`), `AS_AddPrecacheEntry(name)`, `AS_GetBModelSound(name, stage)` |
| 70 | `S_GetSampleLength(sfx)` → float |

## The renderer's functions

Several of the tables above pass on the renderer's own entries. In the
engine's renderer table (API version 9):

| Entry | Function |
|---|---|
| 0 | `Shutdown` |
| 1 | `BeginRegistration` |
| 2 | `RegisterModel` |
| 3 | `RegisterSkin` |
| 4 | `RegisterShader` |
| 5 | `RegisterShaderNoMip` |
| 6 | `LoadWorld` |
| 7 | `SetWorldVisData` |
| 8 | `EndRegistration` |
| 9 | `ClearScene` |
| 10 | `AddRefEntityToScene` |
| 11 | `AddPolyToScene` |
| 12 | `AddLightToScene` |
| 13 | `RenderScene` |
| 14 | `LightForPoint` |
| 15 | `SetColor` |
| 16 | `DrawStretchPic` |
| 17 | `DrawRotatePic` |
| 18 | `Scissor`? |
| 19 | `DrawStretchRaw` |
| 20 | `BeginFrame(stereo)` |
| 21 | `EndFrame(&frontEndMs, &backEndMs)` |
| 22 | captures the screen into a buffer at a given size (256×256 for saved games) |
| 23, 24 | not yet known (5 arguments; one forwarded straight through) |
| 25 | `MarkFragments` |
| 26 | `LerpTag` |
| 27 | `ModelBounds` |
| 28 | not yet known |

## The menus (`efuix86.dll`)

The UI works differently from the game:

- the engine calls `GetUIAPI()`, which takes no arguments;
- it keeps the 12 functions that come back;
- it then calls the first, `Init(2, imports)`, with a table of 59 engine functions.

### The menus' functions

| Entry | Function |
|---|---|
| 0 | `Init(apiVersion = 2, imports)` |
| 1 | `Shutdown()` |
| 2 | `KeyEvent(key)` |
| 3 | `MouseEvent(dx, dy)` |
| 4 | `Refresh(time)` |
| 5 | `(0, &out)`. Asked before the screen is drawn; nonzero in `out` means the menus cover the whole screen. |
| 6 | `SetActiveMenu(name)`. The names are strings: `"main"`, `"ingame"`, `"turbolift"`. |
| 7 | `ConsoleCommand()` → handled |
| 8 | `DrawConnectScreen(server, info)` while connecting |
| 9 | the same, while loading |
| 10 | `UpdateConnectionString(text)`: `"(n)"` with each connection attempt |
| 11 | `UpdateConnectionMessageString(text)`: a server's `print` message |

### The engine's functions for the menus

| Slot | Function |
|---|---|
| 0, 1 | `Printf`, `Error` |
| 2–8 | cvars: `Set(name, value)`, `VariableValue(name)` → float, `VariableStringBuffer(name, buffer, size)`, `SetValue(name, float)`, `Reset(name)`, `Create(name, value, flags)`, `InfoStringBuffer(bit, buffer, size)` |
| 9–12 | `Argc()`, `Argv(n, buffer, size)`, `Cmd_ExecuteText(when, text)`, `Cmd_TokenizeString(text)` |
| 13–19 | `FS_FOpenFile`, `FS_Read`, `FS_Write`, `FS_FCloseFile`, `FS_GetFileList(folder, extension, buffer, size)`, `FS_ReadFile`, `FS_FreeFile` |
| 20–30 | renderer entries 2–5 and 9–16: `RegisterModel`, `RegisterSkin`, `RegisterShader`, `RegisterShaderNoMip`, `ClearScene`, `AddRefEntityToScene`, `AddPolyToScene`, `AddLightToScene`, `RenderScene`, `SetColor`, `DrawStretchPic` |
| 31 | empty (0) |
| 32 | `UpdateScreen()` |
| 33 | captures the screen at 256×256 (for a saved game's picture) |
| 34 | renderer entry 26 (`LerpTag`) |
| 35–37 | `S_StartLocalSound(sfx, channel)`, `S_RegisterSound(name)`, `S_StartLocalLoopingSound(sfx)` |
| 38 | renderer entry 19 (`DrawStretchRaw`) |
| 39–41 | read saved games' chunks: their comments, pictures and so on (to identify one by one) |
| 42 | whether the game may save here: asks the game's export 5 |
| 43 | stores the comment (up to 128 characters) for the next save |
| 44 | the 256×256 screen capture's pixels |
| 45–53 | keys: `KeynumToStringBuf`, `GetBindingBuf`, `SetBinding`, `IsDown`, `GetOverstrikeMode`, `SetOverstrikeMode`, `ClearStates`, `GetCatcher`, `SetCatcher` |
| 54 | `GetClipboardData(buffer, size)` |
| 55 | `GetGlconfig(out)` |
| 56 | the connection state (an int: 7 is in the game) |
| 57 | `GetConfigString(index, buffer, size)` → whether there is one |
| 58 | `Milliseconds()` |

## Structures

As the original engine reads and fills them.

- **Entities** (`gentity_t`, 1056 bytes): the shared part is laid out as in
  JK2.

  | Offset | Field |
  |---|---|
  | 0 | the entity state (232 bytes): number 0, `origin2` 0x68, model index 0xa0, `solid` 0xb4 |
  | 0xe8 | `client`: a pointer to the player state |
  | 0xec | `inuse` |
  | 0xf0 | `linked` |
  | 0xf4 | `svFlags` |
  | 0xf8 | `bmodel` |
  | 0xfc | `mins` |
  | 0x108 | `maxs` |
  | 0x114 | `contents` |
  | 0x118 | `absmin` |
  | 0x124 | `absmax` |
  | 0x130 | `currentOrigin` |
  | 0x13c | `currentAngles` |
  | 0x148 | `owner`: a pointer, not a number |

  `svFlags`: 1 never sent to the client, 0x20 always sent, 0x40 a portal (what's seen from its `origin2` is sent too).

- **Player state**: 544 bytes. Origin at 0x14, view height (an int) at 0xa8.
- **User command** (`usercmd_t`, 28 bytes):

  | Offset | Field |
  |---|---|
  | 0 | server time |
  | 4 | buttons |
  | 8 | weapon (a byte) |
  | 12 | angles: three ints, 65536 a turn |
  | 24, 25, 26 | forward, right and up moves (signed bytes) |

  The engine keeps 64.
- **`trace_t`**: Quake III's 56 bytes. The entity number is at 52 (1022 the world, 1023 nothing).
- **`snapshot_t`** (the client game's):

  | Offset | Field |
  |---|---|
  | 0 | flags |
  | 4 | ping |
  | 8 | server time |
  | 12 | area mask (32 bytes) |
  | 44 | command number |
  | 48 | player state |
  | 0x250 | entity count |
  | 0x254 | entity states (up to 256) |
  | 0xea60 | server command sequence |

  The engine keeps the last 32.
- **`gameState_t`**: Quake III's 20100 bytes.
- **`glconfig_t`** (5196 bytes):

  | Offset | Field |
  |---|---|
  | 0 | renderer name (1024 bytes) |
  | 1024 | vendor (1024 bytes) |
  | 2048 | version (1024 bytes) |
  | 3072 | extensions (2048 bytes) |
  | 5120 | Quake III's numbers |
  | 5168, 5172 | screen width and height: two words later than Quake III's |

- **`cvar_t`**, the engine's own, which the server game holds pointers to:

  | Offset | Field |
  |---|---|
  | 0 | name |
  | 4 | text |
  | 16 | flags |
  | 20 | modified |
  | 24 | modification count |
  | 28 | value |
  | 32 | integer |

## How the halves meet

- The server game calls into its client half through the function
  `dllEntry` hands over, so the engine calls `dllEntry` before the level
  starts. One example: the server game sets `cg_viewsize` through it as
  the player's view is set up.
- Configstring changes reach the client game as `cs <index> "<text>"`
  server commands.
- `S_GetSampleLength` returns its float in the x87's ST(0), not in EAX.
