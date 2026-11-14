# Jedi Outcast: the multiplayer menus' system calls

> **Status:** partial · **Code:** `src/engines/idtech3/jk2/ui_host.cpp` · **Checked against:** the code only (the game's own `ui.qvm`, run in Reluva) · **Confidence:** from the code unless marked

## Overview

Multiplayer's menus are `vm/ui.qvm` in `assets0.pk3` (MD5
`831a90b7f33333ff6b183d413e3833d4` in 1.02a): 90,756 instructions in 657
functions, 203 calls through function pointers. They reach the engine
through numbered system calls (see [../../../formats/qvm.md](../../../formats/qvm.md)).
Unlike Elite Force's, the numbers are the ones the GPL Jedi Outcast
source's multiplayer `ui_public.h` lists, in its order: that list was
read as documentation, and each call below was checked against what
1.02a's `ui.qvm` passes to it (`reluva-tool idtech3 qvm-info` for the
numbers it uses, `ui-run -trace` for the arguments). The GPL source is
1.04's, not 1.02a's; where they could differ, 1.02a's bytecode decides.

`qvm-info` finds calls 0–6, 10–17, 18, 20–22, 24–28, 31, 39–45, 50, 51,
56–62, 64–69, 72–92, 100–106, 200 and 201 named in the code. The ones
reached only through function pointers (the menus' drawing and key
functions, kept in a table) aren't in that list, so every call of the
order is answered.

`vmMain(0)` returns 6, the API version; the other commands are Quake
III's (1 init, 2 shutdown, 3 key event, 4 mouse event, 5 refresh, 6 is
fullscreen, 7 set active menu, 8 console command, 9 draw the connect
screen). While not connected, Reluva opens the main menu when none covers
the screen, as for Elite Force.

## How it works

### The calls

| # | Call | What Reluva does |
|---|---|---|
| 0, 1 | `Error(text)`, `Print(text)` | stops the menus; prints |
| 2 | `Milliseconds()` | the page's clock |
| 3–9 | `Cvar_Set`, `Cvar_VariableValue`, `Cvar_VariableStringBuffer`, `Cvar_SetValue`, `Cvar_Reset`, `Cvar_Create`, `Cvar_InfoStringBuffer` | Quake III's, on the page's cvars |
| 10–12 | `Argc`, `Argv`, `Cmd_ExecuteText(when, text)` | the commands are run by the page (below) |
| 13–17 | `FS_FOpenFile`, `FS_Read`, `FS_Write`, `FS_FCloseFile`, `FS_GetFileList` | as Elite Force's (`VmFiles`) |
| 18 | `R_RegisterModel(name)` | a handle; the main menu's turning emblem, `jk2logo.md3` |
| 19 | `R_RegisterSkin` | not called |
| 20 | `R_RegisterShaderNoMip(name)` | a handle; 0 for a missing picture, which the menus check |
| 21, 22, 24, 25 | `R_ClearScene`, `R_AddRefEntityToScene(refEntity)`, `R_AddLightToScene`, `R_RenderScene(refdef)` | the scene, with Jedi Outcast's 212-byte `refEntity_t` (`jk2/ref_entity.h`); the refdef's rectangle is in 640 x 480 and scaled to the frame |
| 26, 27 | `R_SetColor(rgba)`, `R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader)` | pictures, in 640 x 480 |
| 28 | `UpdateScreen` | nothing: the page draws each frame |
| 31, 32 | `S_RegisterSound`, `S_StartLocalSound(sound, channel)` | the menus' sounds |
| 33–41 | the key calls (`Key_KeynumToStringBuf` ... `Key_SetCatcher`) | as Quake III's, on the page's bindings; Quake III's key numbers (`K_MOUSE1` 178, the character flag 1024) |
| 42 | `GetClipboardData` | empty |
| 43 | `GetGlconfig(glconfig)` | 11,332 bytes: three 1,024-byte strings and 8,192 of extensions, then Quake III's 17 words; it reports 640 x 480, the size the menus draw in |
| 44 | `GetClientState(state)` | 12 bytes of state, then three 1,024-byte strings |
| 45 | `GetConfigString(index, buffer, size)` | the match's, when connected |
| 46–49, 73–82, 89–92 | the server list's (`LAN_*`) | empty lists for now: the menus show none |
| 50, 51 | `Cvar_Register`, `Cvar_Update` | Quake III's `vmCvar_t` |
| 52 | `MemoryRemaining` | 8 MB |
| 56 | `R_RegisterFont(name)` | `fonts/<name>.fontdat`, its picture `fonts/<name>` ([fonts.md](../../../formats/fonts.md)) |
| 57, 58, 59 | `R_Font_StrLenPixels(text, font, scale)`, `R_Font_StrLenChars(text)`, `R_Font_StrHeightPixels(font, scale)` | measured as the font lays the text out |
| 60 | `R_Font_DrawString(x, y, text, rgba, font, charLimit, scale)` | the text as pictures; below |
| 61 | `AnyLanguage_ReadCharFromString(&text)` | the byte there, the pointer moved past it: 1.02a's languages are all one byte a letter |
| 62 | `R_ModelBounds(model, mins, maxs)` | the model's first frame's |
| 63–69 | `PC_AddGlobalDefine`, `PC_LoadSource`, `PC_FreeSource`, `PC_ReadToken`, `PC_SourceFileAndLine`, `PC_LoadGlobalDefines`, `PC_RemoveAllGlobalDefines` | the menu scripts, through the engine's script reader; a token (`pc_token_t`) is its type, subtype, int, float, then 1,024 bytes of text |
| 70, 71 | `S_StopBackgroundTrack`, `S_StartBackgroundTrack(intro, loop)` | the Menu page's music |
| 72 | `RealTime(qtime)` | the local time |
| 83–87 | the cinematic calls (`CIN_*`) | none plays: `CIN_PlayCinematic` returns -1, which the menus take as no video |
| 88 | `R_RemapShader` | not yet |
| 100–106 | `memset`, `memcpy`, `strncpy`, `sin`, `cos`, `atan2`, `sqrt` | the interpreter's own |
| 200, 201 | `SP_Register(package)`, `SP_GetStringTextString(reference, buffer, size)` | the string packages, in `sp_language`'s language ([strings.md](../../../formats/strings.md)) |

### Text

`R_Font_DrawString`'s sixth argument is a number of letters, not a width
in pixels as in the single-player engine: the GPL multiplayer renderer
names it `iCharLimit`, and 1.02a's menus pass a list column's or an edit
field's `maxChars`/`maxPaintChars` (a map list column of 25 shows whole
map names; read as pixels it cut them to three letters). Letters and
spaces count; colour codes and line breaks don't. -1 is no limit.

The font handle's two high bits are styles: `0x80000000` a drop shadow,
`0x40000000` blinking. The shadow is the text again, offset by the
font's point size times the scale times 0.075 (rounded), in grey (0.15,
0.15, 0.15) whatever its colour codes say. Blinking text isn't drawn every other 128
ms of the engine's clock.

The menu scripts' names may start with `@` (`@MENUS2_PLAY`, a string
package reference): the script reader keeps it as part of the name, as
Raven's does.

### Commands

The menus run their commands through `Cmd_ExecuteText`; the Menu page
acts on the ones that leave it (`map`, `connect`, `demo`, `quit`,
`vid_restart`) as for Elite Force. The main menu starts its music with
the `music` command (`music music/mp/duel`), not the background track
call; one name is both the intro and the loop.

### Settings the menus show

The menus' Create Server shows the server's `sv_hostname` and
`sv_maxclients`, which the original engine registers as it starts, before
any game; Reluva does the same. `timelimit`, `fraglimit` and the other
game settings are the game code's, so they're empty until a match has
registered them. The default keys and settings are in `mpdefault.cfg`,
which the multiplayer engine runs first (`default.cfg` is single
player's).

## Sources and checks

- **Where the facts come from:** 1.02a's `ui.qvm`, through `qvm-info`
  and `ui-run -trace`, and the GPL Jedi Outcast source's multiplayer
  `ui_public.h`, `ui_main.c` and renderer `tr_font.cpp`, read as
  documentation for the order of the calls and what they do; the code is
  new.
- **How it was checked:** the menus run in Reluva: the main menu with its
  emblem, Play, Setup's Player and Video pages, Controls with the
  bindings, Create Server with the map list and its levelshot, Join
  Server. A game-files step (`jk2-menus`) records the main
  menu, Setup, and Create Server reached by clicking. Not compared with
  the original's screens.

### Open questions

- The menus register four pictures 1.02a's paks don't hold
  (`levelshots/ffa_bespin_small`, `menu/art/jk2unknownmap`,
  `menu/art/3_cursor2`, `gfx/2d/crosshairj`); they get 0 and the menus
  draw something else (the map preview is `levelshots/<map>`). Whether
  the original draws anything for them isn't confirmed.
- The server lists, the cinematic calls and `R_RemapShader` aren't
  answered yet.
