# Jedi Outcast: single player's menus

> **Status:** partial · **Code:** `src/engines/idtech3/jk2/menu_defs.cpp`, `src/engines/idtech3/jk2/sp_menus.cpp`, `src/engines/idtech3/jk2/sp_game_calls.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; the client game running in Reluva · **Confidence:** from the code unless marked

## Overview

Single player's menus are Team Arena style menu scripts in the paks
(`ui/menus.txt` and the `.menu` files it names), but unlike
multiplayer's they're run by `jk2sp.exe` itself: there's no menus DLL.
The same engine code also keeps the client game's HUD menus
(`ui/jk2hud.txt`, which loads `ui/hud.menu`), which the client game
reads through its calls 97–107 and 111
([sp-interface.md](sp-interface.md)). This page covers what's done of
that system so far: reading menu files, answering the client game, and
running the engine's own menus.

## How it works

### Reading a menu file

The client game (as the GPL source's `CG_ParseMenu` shows) opens a file
in a parse session and walks its tokens, handing each `menudef` to the
engine:

- **`PC_StartParseSession(file, &buffer)`** (0x49fc00): the file read
  whole; its name kept (64 letters, 0xcf2f98), its text's start and the
  session's place set to the text (0xcf2fdc, 0xcf2fe0); the file's length
  returned, 0 if there's none.
- **`PC_ParseExt(&token)`** (0x49fc80): the next token from the session's
  place (Quake III's rules: quoted strings, `//` and `/* */` comments),
  the place moved past it; "" at the end.
- **`PC_ParseInt`, `PC_ParseString`, `PC_ParseFloat`**: the next token as
  that.
- **`Menu_New(buffer)`** (0x49fb60): a new menu read from the session's
  place, not from the buffer it's handed; at most 64 menus (0x49fb74),
  each 0x330 bytes in the engine.
- **`PC_EndParseSession(buffer)`** (0x49fc50): the file given back.
- **`Menu_Reset`** (0x49f970) forgets the menus; **`String_Init`**
  (0x49d390) the menus' text.

### Asking about menus

- **`Menu_GetInfo(name, &x, &y)`** (0x401ec2): the menu found by name
  (0x49d690); its rectangle's x and y, cut to whole numbers; 1, or 0 if
  there's no such menu. The client game draws its HUD's health, armour,
  ammunition and Force power at the `lefthud` and `righthud` menus' places.
- **`UI_GetItemText(menu, item, buffer)`** (0x401ef9): the item's text
  (its pointer at 0xe8 in the engine's item), 256 letters at most; 1, or
  0 if there's no such menu or item.
- **`Menu_PaintAll`** (0x49fd40): the visible menus drawn. The HUD's
  seven menus have no items, only places, so nothing is drawn for them.

### The HUD's menus

`ui/hud.menu`: an `assetGlobalDef` (the big font) and seven
`menuDef`s, each a name and a rectangle in the 640 x 480 screen
(`mainhud`, `righthud` at 560 400, `lefthud` at 0 400, and others the
client game asks for, `inventoryselecthud` and `forceselecthud` among
them), all visible.

### The engine's menus

`ui/menus.txt` names the menu files (`loadMenu { "ui/main.menu" }`, 21
of them); each file is `{ assetGlobalDef { ... } menuDef { ... } ... }`.

- **Assets** (`assetGlobalDef`, in `main.menu`): the small, medium and
  big fonts (`ocr_a`, `ergoec`, `anewhope`), the cursor, the focus sound,
  fading and shadow values. The fonts are registered in that order
  (0x49abe4 on), and an item's `font` number goes to the renderer as the
  font's handle (0x4a017d): 1 small, 2 medium, 3 big; 0 is the medium
  font (0x4999d8). The cursor is drawn 48 by 48 at the mouse (0x49998c).
- **Symbols**: `WINDOW_STYLE_*`, `ITEM_TYPE_*` and `ITEM_ALIGN_*` are
  names in tables from 0x4eabc4, each name's value its place
  (`WINDOW_STYLE_SHADER` 3, `ITEM_TYPE_BUTTON` 1, `ITEM_TYPE_MULTI` 12,
  `ITEM_ALIGN_CENTER` 1...).
- **Text styles**: the text's font handle gets 0x40000000 (blinking) for
  style 2 and 0x80000000 (a drop shadow) for styles 3 to 6 (0x4999e2).
- **Scripts** (`onOpen`, `onESC`, an item's `action`, `mouseEnter`...):
  a table of 23 commands at 0x4eadd8 (eight bytes each, name and
  handler), each taking the words it needs: `activate`, `close`, `exec`,
  `fadein`, `fadeout`, `hide`, `open`, `openGoToMenu`, `orbit`, `play`,
  `playlooped`, `setasset`, `setbackground`, `setcolor`, `setcvar`,
  `setfocus`, `setitemcolor`, `setitemflag`, `show`, `showMenu`,
  `transition`, `defer`, `rundeferred`. A lone `;` between them is
  skipped, so the menus' scripts often leave it out. Any other word goes
  to the engine's own script handler (0x49f6e9), which takes the next
  word as its command: that's how `uiScript startgame` works. A command
  that returns 0 ends the script; the engine's handler only says so of
  a name it doesn't know ("unknown UI script", 0x49a5c1) and goes on.
  `play` plays its sound on channel 0 (0x49e1e0); `exec` runs a console
  command after the frame. `defer VideoSetup <menu>` (0x49dfd0,
  0x499e80): if `ui_r_modified` is set, that menu opens (a warning) and
  the rest of the script is kept for `rundeferred`, which runs it
  (0x49e020); `activate <menu> <item>` runs that item's action.
- **`uiScript` commands** (0x499ef0):
  - `startgame`: all menus closed, then `map kejim_post` (0x49a3d9);
    `startmap <map>` the same for that map.
  - `closeingame`, `closedatapad`: the keys back to the game, unpaused,
    the menus closed and the HUD's `mainhud` opened; `closedatapad` also
    clears `cg_updatedDataPadForcePower1` to `...3` and
    `cg_updatedDataPadObjective`.
  - `loadAuto`: `load auto`; `loadgame`: `load <the save chosen>`, the
    menus closed first; `deletegame`: `wipe <the save chosen>`;
    `savegame`: the first of `jkii00` to `jkii99` that isn't a saved
    game, its comment `ui_gameDesc` (0x49a112, 0x49a5e0), then `save`
    it. Each reads the list again after.
  - `ReadSaveDirectory`: the list read again when next asked for.
  - `resetdefaults`: `cvar_restart`, `exec default.cfg`, `vid_restart`,
    `com_introPlayed 1` (0x49c5e0).
  - `getvideosetup` copies 15 video settings to the menus' own
    (`r_mode` to `ui_r_mode`... and `cg_shadows` to `ui_cg_shadows`);
    `updatevideosetup` copies them back, clears `ui_r_modified` and runs
    `vid_restart` (0x49c2c0, 0x49c160). `glCustom` sets
    `ui_r_glCustom 4`; `update <setting>` makes a setting's knock-on
    changes (0x49b140: the video presets, the player's name, `s_khz`).
  - The datapad's `nextDataPadWeapon`... run the client game's commands
    `dpweapnext`, `dpweapprev`, `dpforcenext`, `dpforceprev`,
    `dpinvnext`, `dpinvprev` (0x49a283 on).
  - `Quit`: `quit`; `Leave`: `disconnect`, then the main menu;
    `clearError`: `com_errorMessage` emptied; `loadControls`,
    `saveControls`: the bindings read from, or written to, the engine.
- **Saved games' list** (feeder 0, `FEEDER_SAVEGAMES`; ReadSaveDirectory
  at 0x49c620): `saves/*.sav` but `current`; `auto` isn't listed but
  sets `ui_ResumeOK`; at most 100, each's description, time, level and
  the time as `asctime` writes it, newest first. Its two columns are the
  description and the date (0x499b60); other feeders have no rows in
  single player. Choosing one (0x49a6b0) sets `ui_SelectionOK` (whether
  it's in the list) and `ui_gameDesc` (its description), and reads its
  picture.
- **List boxes** (0x4a09c0): a scroll bar down the right, 16 wide (its
  up arrow, bar, down arrow and thumb: `gfx/menus/scrollbar*.tga`); rows
  from the rectangle's corner, 1 in, each column's text 4 more in plus
  the column's place, at most its letters; the chosen row's highlight 2
  in, 6 below the row's top, `elementheight` high and the width less the
  bar and 4, in the item's outline colour.
- **Key binding** (0x4a3960): a click or Enter on a bind item waits for
  a key (the menu's other items made inactive). Then Esc gives up, `\``
  is ignored, Backspace unbinds the command's keys; another key is taken
  from any command in the binding list it had, and becomes the
  command's first key, or its second, or if it has two already both are
  unbound and it's the first.
- **Owner draws** (0x49b7f0, by number less 200): 203 to 206 the
  datapad's pages, each the client game's vmMain command 11 (the
  datapad's frame and the player's health, armour and ammunition) then
  12 to 15 (mission, weapons, inventory, Force powers) through the
  engine (0x40b790); 236 the chosen saved game's picture (512 by 512)
  over the item, its level's name under it; 242 the crosshair (its keys
  step `cg_drawCrosshair` 0 to 9); 249 the video driver's OpenGL
  details; 250 `MENUS_WAITINGFORKEY` while a binding waits, which
  1.02a's string packages haven't.
- **Model items** (0x4a1490): a view of the item's rectangle 1 in, 45
  degrees each way, no world; the model's first frame's bounds put it
  as far off as 3.73 times half its height, centred up and across;
  it turns 0.05 degrees a millisecond, whatever `model_angle` and
  `model_rotation` say; drawn lit from its own origin, no shadow
  (flags 0xc0).
- **Opening a menu by the engine** (0x499310, `UI_SetActiveMenu`):
  `mainMenu` (the server stopped, the keys to the menus, `mainMenu`
  opened, `error_popmenu` too if `com_errorMessage` is set), `ingame` (the
  game paused, the HUD's `mainhud` closed, `ingameMainMenu` or the menu
  named opened), `datapad` (`mainhud` closed, `datapadForcePowersMenu` if
  `cg_updatedDataPadForcePower1` is set, else `datapadMissionMenu`), none
  (the keys back to the game, unpaused). The console commands `uimenu
  [menu]` (0x40bbd0) and `datapad` (0x40bc10, only in a game) open them.
- **The menus' stack**: opening a menu takes the focus from the one that
  had it, which is kept on a stack; closing the focused menu gives the
  focus back to the top of it (0x49d710).
- At start-up the engine plays `cinematic openinglogos` (0x4d8f8c).

## In Reluva

`jk2::MenuDefs` reads `menuDef`s and their `itemDef`s from a parse
session: each keyword with the rest of its line as its arguments, or a
block in braces (on its line, or the next when nothing else followed
it), stopping at the definition's closing brace. It keeps them as
written; a menu's name, rectangle and visibility are read out. The host
answers the calls above from it, the session's text kept on its side
and the buffer and each token copied into the game's memory.

`jk2::SpMenus` runs the engine's menus for the single-player Play page
(behind `client/sp_menu_host.h`, which Elite Force's DLL host shares):
the files named in `ui/menus.txt` read with the same reader, their
keywords made into menus and items; menus opened, closed and stacked as
above; windows (filled, shader, borders), text (aligned, scaled, in the
item's font and style, `@` text from the string packages), the values
of multi, yes/no, edit, bind and slider items drawn; the focused item's
description at the menu's place for it, the cursor over all. Moving the
mouse onto an item runs its `mouseEnter` and gives it the focus (with
the focus sound); leaving it runs its `mouseExit`. Up, Down and Tab move
the focus, Enter or a click runs the item's action (or turns a multi,
yes/no or slider's setting), Esc the menu's `onESC`. Items shown,
hidden, enabled or disabled by a setting (`cvarTest` with `showCvar`...)
are too. The Play page starts them after the opening logos; `startgame`
starts `kejim_post`, Esc in a game opens `ingameMainMenu`, `datapad` the
datapad.

Where jk2sp.exe's own details weren't worked out, Team Arena's way is
used (marked as unconfirmed): text placed from the rectangle's corner
by `textalignx` and `textaligny` (its top, as Raven's fonts draw), the
focused item's text in the menu's focus colour (not pulsing), a
multi's value 8 units after its text, the slider 96 by 16.

The list boxes, key binding, the owner draws and model items are as
above: the saved games are those in the game's `saves/` and the Play
page's own folder, read with Reluva's reader of the engine's chunks
(sp-saves.md), their pictures decoded from the JPEG kept. The datapad's
pages are the client game's (`SpGameHost::drawDataPad`), its pictures
then numbered among the menus' shaders. Edit fields take what's typed
onto their setting's value, Backspace taking the last letter off, as
Team Arena's (its cursor moving within the text isn't done); a cursor
blinks after the text meanwhile. A list box's rows are chosen by a
click or the arrows, a second click within 300 ms on the same row a
double click, the wheel and the bar's arrows scroll (Team Arena's way,
unconfirmed); Enter runs its action (the save menu's: save).

Not yet: fading (`fadein`, `fadeout`, `fadeClamp`... and the items'
`appearance_slot` coming in one after another), `transition`,
`orbit`; owner draw 249 (OpenGL details, which Reluva's renderer
hasn't) and `update`'s presets for an OpenGL renderer; the HUD's
`mainhud` isn't closed with the in-game menus (Reluva keeps the HUD's
menus apart); `loadControls` and `saveControls` do nothing, the
bindings being Reluva's own, kept as they change.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a's handlers for the
  client game's calls 97–107 and 111 (0x401e2d–0x401f43) and what they
  call (0x49f970–0x49fd40, 0x49d690); its menu code (0x499310–0x4a5b00:
  the keyword and script tables, the text drawing, `UI_SetActiveMenu`); the GPL Jedi Outcast single-player
  source's `cg_main.cpp` as a guide to how the client game uses them;
  the menu files in `assets1.pk3`.
- **How it was checked:** a unit test (`idtech3.unit.formats`, "menu
  definitions") reads menus with keywords, scripts and items, and a
  malformed one; `sp-run -cgame` on `kejim_post` reads `ui/hud.menu`, and
  the client game finds its HUD's menus and draws its frames (a
  game-data step records it). A unit test (`idtech3.unit.game-code`,
  "Jedi Outcast's menus") runs two small menus: opening, scripts with
  and without `;`, the mouse and keys, `startgame`. `reluva-tool idtech3
  sp-menus <base>` runs the game's own menus with keys and mouse moves
  (`-mouse frame dx dy`), reporting the menus open, the focus, the
  commands and what's drawn (`-pictures file` writes the last frame's
  pictures). On 1.02a's files it reads all 21 menus; the main, New Game
  and in-game menus, drawn from those pictures, look as the original's
  do, but for the logo's model; the datapad's mission page is its black
  frame and buttons, without the owner draws. A game-data step
  (`jk2-sp-menus`) records the way from the main menu through New Game,
  Jedi Knight and Begin Game to `map kejim_post`, and the in-game menu
  and the datapad opened. `sp-run -datapad` draws the client game's
  datapad pages on kejim_post; `-saveload -savefile` writes the saved
  game, which `sp-menus -savedir` lists in the load menu and loads; a
  key is bound in Controls. Drawn from their pictures, the load menu,
  Controls and the datapad's mission and weapons pages look as the
  original's do. A unit test ("Jedi Outcast's menu items") runs the list,
  binding, edit field, datapad owner draw, `defer` and the save, load
  and delete scripts on made-up menus and saved games.
