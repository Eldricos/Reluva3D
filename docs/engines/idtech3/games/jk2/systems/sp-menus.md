# Jedi Outcast: single player's menus

> **Status:** partial · **Code:** `src/engines/idtech3/jk2/menu_defs.cpp`, `src/engines/idtech3/jk2/sp_game_calls.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; the client game running in Reluva · **Confidence:** from the code unless marked

## Overview

Single player's menus are Team Arena style menu scripts in the paks
(`ui/menus.txt` and the `.menu` files it names), but unlike
multiplayer's they're run by `jk2sp.exe` itself: there's no menus DLL.
The same engine code also keeps the client game's HUD menus
(`ui/jk2hud.txt`, which loads `ui/hud.menu`), which the client game
reads through its calls 97–107 and 111
([sp-interface.md](sp-interface.md)). This page covers what's done of
that system so far: reading menu files and answering the client game.

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

## In Reluva

`jk2::MenuDefs` reads `menuDef`s and their `itemDef`s from a parse
session: each keyword with the rest of its line as its arguments, or a
block in braces (on its line, or the next when nothing else followed
it), stopping at the definition's closing brace. It keeps them as
written; a menu's name, rectangle and visibility are read out. The host
answers the calls above from it, the session's text kept on its side
and the buffer and each token copied into the game's memory.

Not yet: interpreting the rest of the keywords (items' kinds, colours,
fonts, owner draws, scripts), drawing items, and the menus the engine
runs itself (the main menu, saving and loading, the datapad).

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a's handlers for the
  client game's calls 97–107 and 111 (0x401e2d–0x401f43) and what they
  call (0x49f970–0x49fd40, 0x49d690); the GPL Jedi Outcast single-player
  source's `cg_main.cpp` as a guide to how the client game uses them;
  the menu files in `assets1.pk3`.
- **How it was checked:** a unit test (`idtech3.unit.formats`, "menu
  definitions") reads menus with keywords, scripts and items, and a
  malformed one; `sp-run -cgame` on `kejim_post` reads `ui/hud.menu`, and
  the client game finds its HUD's menus and draws its frames (a
  game-data step records it).
