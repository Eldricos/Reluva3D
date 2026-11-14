# And Then There Were None: saved games

> **Status:** partial · **Code:** `src/engines/awe/formats/savegame.cpp` (shared), `src/engines/awe/games.h` (`GameSaves`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`) · **Confidence:** from the code

## Overview

The save file is Orient Express's
([../../motoe/systems/saves.md](../../motoe/systems/saves.md)) with its
text a byte a character and three records fewer; the save and load
screens are Orient Express's laid out otherwise, and the menus are scenes
in `Interface/Scenes`.

## How it works

### The files

`Saved Games\slot<N>.ac1` with a `slot<N>.bmp` beside it (`0x41e917`,
`0x41ebb4`); `Saved Games\last.ac1` (`0x413711`), what it's for not read
yet; and `Saved Games\<name>.ac1` for a name the code gives (the writer
takes one: `0x412e9f`). The install has none.

### The records

The writer (`0x412e9f`) writes, in this order:

| Tag | Data | Orient Express |
|---|---|---|
| `DESC`, `TIME`, `DATE` | c[32] each (`"%02d:%02d"`, `"%02d-%02d-%02d"`) | w[32] |
| `GLBT` | s16, the time of day (`word[0x485f10]`) | the same |
| `GLBW` | s16, the weather (`word[0x485f12]`) | the same |
| | | `GLBM`, the train's moving: none here |
| `TIMR` | u32 (`[0x485f2c]`) | the same |
| `INFO` | c[32] location, c[256] scene, s16 act number, s16 act letter | w[] |
| | | `ACTI`, the act's row: none here |
| `CHAR`, `FLLW` | 3 f32 position, 3 f32 rotation, when there's a player or follower | the same |
| `TAGS` | n, then each c[64] name and s16 value | w[64] |
| | | `TIMS`, the timers: none here |
| `STAT` | n, then each c[64] name and c[256] value | w[] |
| `POSI` | n, then each c[64] scene, c[64] model, 3 f32, 3 f32 | w[] |
| `TALK` | n, then each c[32] name, c[32] act, s32 part, s32 thread, s16 status, s32 nest | w[] |
| `ITEM` | s16 (the inventory's `+0xb6`), n, then each c[32] name and three s16 (`+0x8f0`, `+0x8f2`, `+0x8f4`) | w[32], state, slot and page |

Text is Windows-1252 in buffers as many characters long as Orient
Express's. A `TALK` record's thread is coded as this game's loader codes
it ([dialogue.md](dialogue.md#the-codes)): thread `1` is 0x1a, where
Orient Express's is 0x60.

### The menus

The main menu is the scene `mainmenu01` (`Locations/Mainmenu`): New Game
(`fade out, act`, its event having run `reset`), Load Game (`menu
load`), Options, Credits and Exit (`yesno quit2 0`). `menu <name>` shows
`interface\scenes\<name>.ac1` (`0x403990`): `ingame` (Resume, Save,
Load, Options, Main Menu), `save`, `load`, `options`. Escape opens
`menu ingame`, or at the main menu asks `quit2` (`0x416f0c`); there are
no P, S or L keys.

### The save and load screens

Their scenes' `PUZZ save` and `PUZZ load` make the screen as Orient
Express's do; its rows are drawn otherwise (`0x41eca0`, clicks
`0x41ef80`):

| | And Then There Were None | Orient Express |
|---|---|---|
| Rows' tops | 57, 222, 389 | 63, 287, 511 |
| Rows' height; clicks across | 150; x 521 to 721 | 190; x 703 to 957 |
| Thumbnail | x 522, a pixel down; alpha 92 unless picked | x 704 |
| Name | centred on x 404, in the row's middle | x 624 |
| Date; time | x 526; ending at x 716, at the row's foot | x 708; 952 |
| Font, colours | font 7 (Arial Narrow 10); white picked, `0xff808080` the rest | `fonts.txt`'s |
| An empty row | `EMPTY` (`0x41ed4a`) | `interface.txt`'s `empty` |
| The glow | `Interface\Scenes\Cutouts\glow.tga` on the picked row | the menu's |

The thumbnail is written 256 by 192, as Orient Express's *(inferred: the
code that makes it isn't read here)*.

## In Reluva

`SaveLayout` (`formats/savegame.h`) says whether text is narrow and which
of `GLBM`, `ACTI` and `TIMS` there are; `GameSaves` in the per-game table
has it, the files' extension and the screens' rows (`SlotLayout`). A
save without `ACTI` takes the act's row from the chapter tables when
it's loaded. `GameFlow` has the menus' scene names, the main menu's
place and whether the letter keys open menus.

Not yet: the options screen as this game's, `last.ac1`, what the three
item numbers mean here, and the threads coded as this game's.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s writer (`0x412e9f`), found
  by its file name and its record tags, and the paths' strings.
- **How it's checked:** `awe.unit.attwn` writes a save in this layout and
  reads it back. In the Scenes page: the main menu, New Game into 1A's
  movie, Escape to the in-game menu in 1B, a save made and named, the
  load screen listing it with its picture, and it loaded from the main
  menu back to the entry hall. No save of the original game has been
  read.
