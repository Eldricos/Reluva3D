# Elite Force: the game modules' system calls

Elite Force's Holomatch code is three QVMs in `pak0.pk3`: `vm/ui.qvm`
(menus), `vm/cgame.qvm` (the client: HUD, effects) and `vm/qagame.qvm` (the
server's game logic). Each reaches the engine through numbered system calls
(see [qvm.md](qvm.md)). The numbers and arguments are Elite Force's own, so
they're worked out here from the game's code, for interoperability:

- `reluva-tool qvm-info` lists the numbers each program's code uses.
- `reluva-tool qvm-run` runs a program, answering every call with 0, and
  logs each call with its arguments and the text they point at.
- Where the evidence fits, the meaning is taken from Quake III Arena's
  calls, documented in the GPL Quake III source (the order of its
  `ui_public.h`, `cg_public.h`, `g_public.h`). The calls are identified
  from that order and the logs below, not from Raven's SDK, which Reluva
  doesn't use (see [the engine page](../../engines/idtech3.md)).

"Seen" means the call appeared in a log with arguments that fit; "order"
means it's assumed from Quake III's order (the seen calls fit that
order); anything else is listed as unknown.

All logs below are from the English Windows release (`pak0.pk3` MD5
`f31871f2891b1e9d6e8d1654ea93352d`).

## Menus (`vm/ui.qvm`)

113,286 instructions in 754 functions; it uses calls 0–58 and 100–106.
`vmMain(0)` returns 4, the API version; `vmMain(1)` initialises. The other commands are taken from
Quake III's order: 2 shutdown, 3 key event (key, down), 4 mouse event
(dx, dy), 5 refresh (time), 6 is fullscreen, 7 set active menu (menu),
8 console command, 9 draw the connect screen.

| # | Call | Basis | Evidence (from `qvm-run ... ui.qvm 1`) |
|---|---|---|---|
| 0 | `Error(text)` | seen | `"UI_LoadMenuText : MP_NORMALTEXT.DAT file not found!\n"`: missing menu text is fatal |
| 1 | `Print(text)` | seen | `"0 arenas parsed\n"`, `"^1file not found: scripts/arenas.txt\n"` |
| 2 | `Milliseconds()` | order | |
| 3 | `Cvar_Set(name, value)` | order | 36 call sites |
| 4 | `Cvar_VariableValue(name)` → float | seen | `("fs_restrict")` |
| 5 | `Cvar_VariableStringBuffer(name, buffer, size)` | seen | `("g_language", buffer, 32)`, before choosing a language's text files |
| 6 | `Cvar_SetValue(name, float)` | order | 118 call sites (the settings menus) |
| 7 | `Cvar_Reset(name)` | order | |
| 8 | `Cvar_Create(name, value, flags)` | seen | `("ui_initialsetup", "…", 1)` at the end of init |
| 9 | `Cvar_InfoStringBuffer(flag, buffer, size)` | order | not called by the code |
| 10, 11 | `Argc()`, `Argv(n, buffer, size)` | order | |
| 12 | `Cmd_ExecuteText(when, text)` | order | 41 sites |
| 13 | `FS_FOpenFile(path, &handle, mode)` → length | seen | `("scripts/arenas.txt", &h)`, `("ext_data/fonts.dat", &h)`; a missing file reports "file not found" |
| 14 | `FS_Read(buffer, length, handle)` | seen | after opening `fonts.dat`, into a buffer just cleared by `memset(buffer, 0, 20000)` |
| 15 | `FS_Write(buffer, length, handle)` | order | not called by the code |
| 16 | `FS_FCloseFile(handle)` | seen | after the read |
| 17 | `FS_GetFileList(folder, extension, buffer, size)` → count | seen | `("scripts", ".arena", buffer, 1024)` then "0 arenas parsed"; `("scripts", ".bot", …)` |
| 18, 19 | `R_RegisterModel(name)`, `R_RegisterSkin(name)` | order | |
| 20 | `R_RegisterShaderNoMip(name)` → handle | seen | `"gfx/2d/charsgrid_med"`, `"white"`, `"menu/common/cursor"`, `"gfx/2d/numbers/s_zero.tga"` |
| 21–25 | `R_ClearScene`, `R_AddRefEntityToScene`, `R_AddPolyToScene`, `R_AddLightToScene`, `R_RenderScene` | order | 3D models in menus |
| 26 | `R_SetColor(rgba or 0)` | order | 236 sites, the most of any call |
| 27 | `R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader)` | order | |
| 28 | `UpdateScreen()` | order | |
| 29, 30 | `CM_LerpTag`, `CM_LoadModel` | order | |
| 31 | `S_RegisterSound(name)` → handle | seen | `"sound/interface/button1.wav"` |
| 32 | `S_StartLocalSound(sound, channel)` | order | |
| 33–41 | keys: `KeynumToStringBuf`, `GetBindingBuf`, `SetBinding`, `IsDown`, `GetOverstrikeMode`, `SetOverstrikeMode`, `ClearStates`, `GetCatcher`, `SetCatcher` | order | |
| 42 | `GetClipboardData(buffer, size)` | order | |
| 43 | `GetGlconfig(&glconfig)` | seen | one pointer, during init, just before the fonts' shaders are registered |
| 44 | `GetClientState(&state)` | order | |
| 45 | `GetConfigString(index, buffer, size)` | order | |
| 46–53 | server list and pings | unknown | Quake III's list has `Cvar_Register` at 50, so Elite Force has four more calls before it, presumably for the server browser |
| 54 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | 65 calls in init: `(…, "ui_ffa_fraglimit", "20", 1)`; the structures are 272 bytes apart, a `vmCvar_t` |
| 55 | `Cvar_Update(&vmCvar)` | order | |
| 56 | `MemoryRemaining()` | order | |
| 57, 58 | `GetCDKey(buffer, size)`, `SetCDKey(key)` | order | |
| 100–106 | `memset`, `memcpy`, `strncpy`, `sin`, `cos`, `atan2`, `sqrt` | seen | `memset(buffer, 0, 20000)`, `strncpy(buffer, "ext_data/mp_normaltext.dat", 63)` |

Structures the engine fills in, assumed to be Quake III's until checked
with `reluva-tool qvm-refs`:

- `glconfig_t` (call 43): renderer, vendor and version strings of 1024
  bytes and an 8192-byte extensions string, then 17 words; the menus use
  `vidWidth` and `vidHeight` (words 10 and 11) to scale their 640×480
  layout.
- `uiClientState_t` (call 44): connection state (1 = disconnected), two
  more words, three 1024-byte strings.

Files the menus read at init: `ext_data/mp_normaltext.dat`,
`ext_data/mp_buttontext.dat` and `ext_data/mp_itemnames.dat` (names built
from `g_language`), `ext_data/fonts.dat`, `scripts/arenas.txt`,
`scripts/*.arena`, `scripts/*.bot`.

Reluva's side: `src/engines/idtech3/eliteforce/ui_host.cpp`, run by
`reluva-tool ui-run`.

## Game logic (`vm/qagame.qvm`)

149,568 instructions in 856 functions; it uses 0–38, 100–106, 111, and the
bot library's ranges 200–211, 303–318, 401–427 and 500–572. `vmMain(0,
levelTime, randomSeed, restart)` initialises the level (Quake III's
order; it prints the "Game Initialization" banner). 0–38 follow Quake
III's game calls; unlike the menus, 0 prints and 1 is the error.

| # | Call | Basis | Evidence (from `qvm-run ... qagame.qvm 0 0 0 0`) |
|---|---|---|---|
| 0 | `Print(text)` | seen | `"------- Game Initialization -------\n"`, `"gamename: baseEF\n"` |
| 1 | `Error(text)` | seen | `"SpawnEntities: no entities"`, `"G_FindConfigstringIndex: overflow"` |
| 3 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | `("sv_cheats")`, `("gamename", "baseEF")` |
| 5 | `Cvar_Set(name, value)` | seen | `("g_gravity", "800")` |
| 6 | `Cvar_VariableIntegerValue(name)` | seen | `("bot_enable")`, `("com_buildScript")` |
| 7 | `Cvar_VariableStringBuffer(name, buffer, size)` | seen | `("g_language", buffer, 32)`, `("session", buffer, 1024)` |
| 10 | `FS_FOpenFile(path, &handle, mode)` | seen | `("ext_data/mp_itemnames.dat", &h)` |
| 15 | `LocateGameData(entities, count, entitySize, clients, clientSize)` | seen | `(…, 128, 588, …, 740)`: entities are 588 bytes, clients 740; the count grows as entities spawn |
| 18 | `SetConfigstring(index, text)` | seen | `(12, "baseef-1")`, sounds such as `(544, "sound/player/fry.wav")` |
| 19 | `GetConfigstring(index, buffer, size)` | seen | 510 calls while looking for a free index |
| 37 | `GetEntityToken(buffer, size)` → more? | seen | `(buffer, 1024)`; answering 0 gives "SpawnEntities: no entities" |
| 38 | `FS_GetFileList(folder, extension, buffer, size)` | seen | `("models/players2", …, buffer, 2048)`: Elite Force's player models |
| 2, 4, 8, 9, 11–14, 16, 17, 20–36 | the rest of Quake III's game calls, in its order | order | |
| 111 | `ceil` | order | Quake III's game numbers `floor` 110 and `ceil` 111 |

## Client game (`vm/cgame.qvm`)

124,371 instructions in 583 functions; it uses 0–60 and 100–107. Not run
yet: it needs a level loaded. Quake III's client calls are the starting
point; it has more than 60, so where Elite Force's differ will show when
it runs.
