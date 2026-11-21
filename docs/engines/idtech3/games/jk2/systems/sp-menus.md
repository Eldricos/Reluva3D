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
    `ui_r_glCustom 4`.
  - `update <setting>` (0x49b140) makes a setting's knock-on changes,
    its value read as a whole number: `ui_r_colorbits` 0, 16 or 32 sets
    `ui_r_depthbits` 0, 16 or 24; `ui_r_lodbias` 0, 1 or 2 sets
    `ui_r_subdivisions` 4, 12 or 20; `ui_r_glCustom` 0 to 3 a quality
    preset (the table at 0x49b5dc) of `ui_r_fullScreen` (always 1),
    subdivisions, lod bias, colour and depth bits, `ui_r_picmip`,
    `ui_r_mode`, `ui_r_texturebits`, `ui_r_fastSky`, `ui_r_inGameVideo`
    and `ui_r_texturemode`, from 4, 0, 32, 24, 0, 4, 32, 0, 1,
    `GL_LINEAR_MIPMAP_LINEAR` to 20, 2, 16, 16, 3, 3, 16, 1, 0,
    `GL_LINEAR_MIPMAP_NEAREST`; `ui_mousePitch` sets `m_pitch` 0.022, or
    -0.022 if it's set; `s_khz` runs `snd_restart`; `ui_SetName` and
    `ui_GetName` copy `ui_Name` to `name` or back; `ui_setRate` sets
    `cl_maxpackets` and `cl_packetdup` by `rate` (5000 and over 30 and
    1, 4000 and over 15 and 2, else 15 and 1). The menus ask only for
    the colour bits, the lod bias and the mouse's pitch.
  - The datapad's `nextDataPadWeapon`... run the client game's commands
    `dpweapnext`, `dpweapprev`, `dpforcenext`, `dpforceprev`,
    `dpinvnext`, `dpinvprev` (0x49a283 on).
  - `Quit`: `quit`; `Leave`: `disconnect`, then the main menu;
    `clearError`: `com_errorMessage` emptied; `loadControls` (0x49f860)
    reads each of the binding list's 63 commands' two keys from the
    engine's bindings, `saveControls` (0x4a38d0) binds them again.
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
  details (0x49bb50), a line each from the item's top, 15 apart:
  `GL_VENDOR: <vendor>`, `GL_VERSION: <version>: <renderer>`,
  `GL_PIXELFORMAT:`, `Color(n-bits) Z(n-bits) stencil(n-bits)`, then
  the extensions one a line, 16 apart, as far as the item reaches; 250 `MENUS_WAITINGFORKEY` while a binding waits, which
  1.02a's string packages haven't.
- **Model items** (0x4a1490): a view of the item's rectangle 1 in, 45
  degrees each way, no world; the model's first frame's bounds put it
  as far off as 3.73 times half its height, centred up and across;
  it turns 0.05 degrees a millisecond, whatever `model_angle` and
  `model_rotation` say; drawn lit from its own origin, no shadow
  (flags 0xc0).
- **An item's text colour** (0x4a02f0): its fore colour; with the
  focus, the menu's focus colour moving to 0.8 of itself and back, by
  0.5 + 0.5 sin(t) of the way with t the time in milliseconds over 75
  as a whole number, each part kept within 0 and 1 (0x4a26f0); the
  menu's disabled colour if its setting disables it; grey (0.5) while
  another item waits for a key.
- **Fading** (0x4a2760, Team Arena's way): `fadein <item>` and `fadeout
  <item>` (0x49d550) show the item and set it fading. Each fade cycle
  (the menu's `fadeCycle` ms) a fade out takes `fadeAmount` off its fore
  colour's alpha, hiding it once that's 0 or less; a fade in adds it,
  up to `fadeClamp`. A menu starts with the assets' values (0x49fa6e,
  `main.menu`'s 1.0, 1 and 0.1) unless it gives its own. A filled
  window with a picture fades its back colour's alpha the same way
  (0x4a2909). No 1.02a menu fades anything.
- **Moving** (0x4a20d0): `transition <item> <x y w h> <x y w h> <ms>
  <steps>` (0x49d880) shows the item at the first rectangle and moves it
  to the second, each side a step of its whole distance (made a whole
  number) over the steps every `ms`, a side passing its place put on
  it, until all four are there; `orbit <item> <x> <y> <cx> <cy> <ms>`
  (0x49d9e0) shows it at x, y and turns its middle 3 degrees round cx,
  cy every `ms`. One clock serves an item's fading and moving. No 1.02a
  menu uses either.
- **Appearance slots** (0x49ff22): an item with `appearance_slot n` is
  drawn only once the menu's count of appearances reaches n. Opening a
  menu (0x49f810) sets the count and its time to 0; going through its
  items as it's drawn, at the first with a slot after its time, the
  count goes up one and the next is due `appearanceIncrement` ms later.
  The datapad's pages give theirs all slot 1, so they show from the
  first frame drawn after opening.
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

### Edit fields (0x4a3e50)

A click on an edit or numeric field, or Enter on it, starts typing in
it: its cursor at its start and the engine's typing over on (the one
mode the console shares, Insert flipping it), 0x4a5741. Each key then
goes to the field first (0x4a539f):

- **A typed character** goes in at the cursor, the rest moved along,
  unless the text has `maxChars` letters already (or 255); typing over,
  it replaces the letter there, unless the cursor is at `maxChars`. The
  cursor moves on, and the first letter shown with it once it passes
  `maxPaintChars`. Backspace's character (8) takes the letter before
  the cursor off; other characters under 32 are ignored; a numeric
  field ends the typing at anything but a digit.
- **Delete** takes the letter at the cursor off; **Left** and **Right**
  move the cursor, the first letter shown following it out of the field;
  **Home** goes to the start; **End** to the end, the first letter shown
  `maxPaintChars` before it, so a field without that limit (all of
  single player's) shows none of its text until Home or Left brings it
  back; **Insert** flips typing over. The keypad's keys alike.
- **Tab** and **Down** move the focus on, **Up** back, the typing going
  with it if the item is a field. **Enter** and **Esc** end the typing
  and go no further. A click ends it and goes on as a click; any other
  key goes on to the menu as it would.

These compare the key's number, so a typed character with the same
number does the same (`©`, 169, is the keypad's Enter).

Drawn (0x4a08c3): the text from its first letter shown, as many as
`maxPaintChars` allows; while typed in, `_` (typing over) or `|`
(inserting) after the letters up to the cursor (0x499a60), in the
item's style with its lowest bit set, so it doesn't blink.

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
by `textalignx` and `textaligny` (its top, as Raven's fonts draw), a
multi's value 8 units after its text, the slider 96 by 16. The focused
item's colour pulses, items fade, move and appear by their slots, and
`update` sets its settings, all as above.

The list boxes, key binding, the owner draws and model items are as
above: the saved games are those in the game's `saves/` and the Play
page's own folder, read with Reluva's reader of the engine's chunks
(sp-saves.md), their pictures decoded from the JPEG kept. The datapad's
pages are the client game's (`SpGameHost::drawDataPad`), its pictures
then numbered among the menus' shaders. Edit fields are as above
(`SpMenus::editKey`), the overstrike mode the console's; the page sends
Backspace's character with its key, which Windows would. A list box's rows are chosen by a
click or the arrows, a second click within 300 ms on the same row a
double click, the wheel and the bar's arrows scroll (Team Arena's way,
unconfirmed); Enter runs its action (the save menu's: save).

Owner draw 249 shows the renderer's own description (its GPU, driver
and version) on its first line: Reluva's renderer isn't OpenGL, so it
has no vendor, pixel format or extensions to show. Some things are
left as they are because nothing would show the difference:
`loadControls` and `saveControls` do nothing, as Reluva's bind items
read and change the engine's bindings themselves, the binding list's
copy the original keeps between them not needed; and the HUD's
`mainhud` isn't closed with the in-game menus, Reluva keeping the
HUD's menus apart, as nothing reads whether it's open (the client
game finds a menu by name only, 0x49d690, and the HUD's menus draw
nothing).

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
  and delete scripts on made-up menus and saved games; another ("Jedi
  Outcast's menu motion") fades, moves, orbits and holds back items by
  their slots frame by frame, and runs `update`'s presets.
