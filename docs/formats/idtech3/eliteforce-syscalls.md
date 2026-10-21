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

Each frame, while not connected to a game, the engine asks `vmMain(6)`
(is a menu covering the screen?) and, if not, opens the main menu with
`vmMain(7, 1)` before `vmMain(5, time)` draws; Quake III's client does
the same. The menus rely on it: the main menu sends a player without a
checked CD key straight to the CD key screen, without opening itself,
and Accept closes that screen, leaving no menu. Without this step the
screen stays black.

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
| 32 | `S_StartLocalSound(sound, channel)` | seen | `(1, 6)` as a menu closes, after the button sounds were registered; 6 is Quake III's local-sound channel. Reluva plays it at `s_volume` (0.8 unless set), a new sound on a channel other than 0 replacing the one there |
| 33–41 | keys: `KeynumToStringBuf`, `GetBindingBuf`, `SetBinding`, `IsDown`, `GetOverstrikeMode`, `SetOverstrikeMode`, `ClearStates`, `GetCatcher`, `SetCatcher` | order | |
| 42 | `GetClipboardData(buffer, size)` | order | |
| 43 | `GetGlconfig(&glconfig)` | seen | one pointer, during init, just before the fonts' shaders are registered |
| 44 | `GetClientState(&state)` | order | |
| 45 | `GetConfigString(index, buffer, size)` | order | |
| 46–53 | server list and pings | unknown | Quake III's list has `Cvar_Register` at 50, so Elite Force has four more calls before it, presumably for the server browser |
| 54 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | 65 calls in init: `(…, "ui_ffa_fraglimit", "20", 1)`; the structures are 272 bytes apart, a `vmCvar_t` |
| 55 | `Cvar_Update(&vmCvar)` | order | |
| 56 | `MemoryRemaining()` | order | |
| 57 | `SetCDKey(key)` → whether it's taken | seen | the CD key screen's Accept (`qvm-dis -str cdkey`): called with the typed key alone once the menus' own check passes; nonzero closes the screen and sets `ui_cdkeychecked`, zero plays the error sound |
| 58 | `R_ModelBounds(model, mins[3], maxs[3])` | seen | the main menu, right after `R_RegisterModel("models/weapons2/phaser/phaser_w.md3")`: `(1, p, p + 12)`. There is no call to read the CD key back. Taking 58 for `GetCDKey` wrote the key at address 1, over `vmMain`'s jump table, and the menus crashed |
| 100–106 | `memset`, `memcpy`, `strncpy`, `sin`, `cos`, `atan2`, `sqrt` | seen | `memset(buffer, 0, 20000)`, `strncpy(buffer, "ext_data/mp_normaltext.dat", 63)` |

Structures the engine fills in:

- `glconfig_t` (call 43): renderer, vendor and version strings of 1024
  bytes and a 2048-byte extensions string (Quake III's is 8192), then a
  word of Elite Force's own, then Quake III's 17 words: 5192 bytes. Found
  with `reluva-tool qvm-refs` on the address the menus pass: the strings
  at 0, 1024, 2048 and 3072 are each passed on to be printed; the menus'
  next variable starts at 5192 (5196, read 310 times, fits the white
  shader that filled rectangles use). Right after the call the menus
  compute their scale (`qvm-dis -ref`): the word at 5164 times 1/640, the
  word at 5168 times 1/480, so `vidWidth` is at 5164 and `vidHeight` at
  5168, Quake III's words 10 and 11 counted from 5124. With these, the
  main menu's pictures fill a 640×480 screen as expected. (Writing Quake
  III's 11,332 bytes had left the screen size at 0 and overwritten the
  menus' own variables.) What the extra word at 5120 means is unknown;
  Reluva leaves it 0.
- `uiClientState_t` (call 44), assumed to be Quake III's: connection state
  (1 = disconnected), two more words, three 1024-byte strings.

Files the menus read at init: `ext_data/mp_normaltext.dat`,
`ext_data/mp_buttontext.dat` and `ext_data/mp_itemnames.dat` (names built
from `g_language`), `ext_data/fonts.dat`, `scripts/arenas.txt`,
`scripts/*.arena`, `scripts/*.bot`.

Key numbers (`KeyEvent`) are assumed to be Quake III's: ASCII for
printable keys, Tab 9, Enter 13, Escape 27, Backspace 127, then from 128
its list (arrows 132–135, F1 145, mouse buttons 178–180, wheel 183–184).
Typed text comes as `KeyEvent(character + 1024, 1)`, Quake III's
character flag, separately from the keys themselves; the menus' text
fields (the CD key, names) only take those. `MouseEvent` takes movement in
640×480 units; the menus keep their own pointer on the screen. Reluva
keeps it under the system's mouse by watching where the menus draw it
(the picture with `menu/common/cursor`): a large step up and left first
puts it in the corner, which shows how the picture sits relative to the
point, then each frame it sends the step from there to the mouse.

The CD key screen (Holomatch only). The menus check the key themselves
first: at most 22 characters, exactly 18 of them letters or digits (the
others, say, dashes). Then they ask the engine with call 57. The original
engine checks the key further against rules of its own, for the online
authorisation service, which no longer runs; Reluva applies the menus'
rule again, keeps the key in the game's folder in its data directory
(`games/<target>/cdkey.txt`, not where the original keeps it) and answers
that it's taken. The menus ask for a key whenever `ui_cdkeychecked` is 0
when the main menu opens, and set it once one is accepted; with a kept key
Reluva sets it after `Init`, as an earlier accepted entry would have.
Answering 57 as if it were `GetCDKey` had overwritten the typed key with
an empty one and declined every key. The menus never read the key back.

Reluva's side: `src/engines/idtech3/eliteforce/ui_host.cpp`, run by
`reluva-tool ui-run` and shown by the Menu page (`menu_viewer.cpp`).

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
| 23 | `SetBrushModel(ent, name)` | seen | `(ent, *(ent + 324))`: the entity's `model` key, `"*N"` |
| 24 | `Trace(results, start, mins, maxs, end, passEntityNum, contentMask)` | seen | 81 calls on hm_dn1 (items dropping to the floor). The masks are Quake III's values: `0x6000001` (solid, bodies, corpses), `0x2000000` (bodies), `0x2010000` (bodies, player clip), `1` (solid). Afterwards the code reads +4 (`startsolid`), +8 compared with 1.0 (`fraction`), surface flags against 16 (Quake III's `SURF_NOIMPACT`), the entity number against 1023 (none): Quake III's `trace_t` |
| 25–33 | `PointContents`, `InPVS`, `InPVSIgnorePortals`, `AdjustAreaPortalState`, `AreasConnected`, `LinkEntity`, `UnlinkEntity`, `EntitiesInBox`, `EntityContact` | order | `LinkEntity` called 81 times on hm_dn1 |
| 2, 4, 8, 9, 11–14, 16, 17, 20–22, 34–36 | the rest of Quake III's game calls, in its order | order | |

The entities' first 300 bytes, which the engine reads and writes, are
Quake III's `sharedEntity_t` with an 8-byte shorter entity state: 200
bytes of `entityState_t`, then `entityShared_t` at 200 (linked, linkcount,
svFlags, singleClient, bmodel, mins at 220, maxs at 232, contents at 244,
absmin at 248, absmax at 260, currentOrigin at 272, currentAngles at 284,
ownerNum at 296). The evidence, from the traces' arguments: the moving
box is `ent+220` and `ent+232`, the entity to ignore `*(ent+296)` or
`*ent` (`s.number`); the game copies 12 bytes to `ent+92` (`s.origin`, as
in Quake III) before `SetBrushModel`, and stores Quake III's
`CONTENTS_TRIGGER` at `ent+244`; it reads `ent+272` after moving things.
`s.modelindex` is taken to be at Quake III's 160 (what's missing from the
entity state would then come after it), to be checked when the client
game draws brush models. Entities are 588 bytes, clients 740.

Reluva's side: `src/engines/idtech3/eliteforce/game_host.cpp`, run by
`reluva-tool game-run <game folder> <map>`. Starting a level follows the
original server: it sets `mapname` and the server cvars, calls
`vmMain(0, 0, seed, 0)`, during which the game reads the BSP's entity
text with call 37 (each call gives one token, as Quake III's `COM_Parse`
reads them: a quoted string or a run of non-blank characters, comments
skipped; it answers 0 at the end), then runs three frames
(`vmMain(8, levelTime)`, 100 ms apart) to let things settle. Implemented
so far: printing and errors, cvars, files, `LocateGameData`,
configstrings, the entity text, the server info string; linking (filling
in `absmin`/`absmax`, a unit larger than the entity's box, as the original
does), `SetBrushModel`, traces against the world and linked entities,
point contents, entities in a box, entity contact and the PVS tests.
Areas and area portals aren't separated yet. `game-run` also drops a
player's box from every spawn point (raised 9 units, as Quake III's game
spawns players) as a check of the collision model on its own.
| 111 | `ceil` | order | Quake III's game numbers `floor` 110 and `ceil` 111 |

## Client game (`vm/cgame.qvm`)

124,371 instructions in 583 functions; it uses 0–60 and 100–107. Not run
yet: it needs a level loaded. Quake III's client calls are the starting
point; it has more than 60, so where Elite Force's differ will show when
it runs.
