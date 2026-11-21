# Star Trek: Birth of the Federation: testing

## Automated checks

### Unit tests

`tests/engines/botf/unit/`, a file per topic, each a suite
`botf.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `botf.unit.detection` | Recognising the game, and an unknown release by its files' names |
| `botf.unit.archives` | `Stbof.ini` edited as the game does (a played copy unchanged byte for byte, the installer's with keys added in the game's order); `alt.res`'s LZSS (with bytes from the retail file's start, a round trip, and the window's fill) and directory, a saved game's name, description and turn, the `.snd` archives' layouts, finding files through `Stbof.ini` |
| `botf.unit.fonts` | The `.fnt` fonts' glyphs found by their frames, drawing and wrapping text |
| `botf.unit.tables` | The `.tdb` text tables, the data tables' layouts (and that every built-in layout fits its records), `lexicon.dic` |
| `botf.unit.layouts` | The `.wdf` layouts (nested windows, each widget type, cut-short files), the `.mnu` menus, the colour key, dimmed pictures |
| `botf.unit.screens` | The main menu's buttons, with and without a game in progress; the options and setup read from `Stbof.ini`; the options screen's settings' lists and volumes (clicked and dragged); the Hall of Fame's rows and Return; the menu music's order; Multiplayer Setup's panels, fields, the address book, Tab and the password's limit; Load Game's slots (an empty one, choosing, Accept), and Save Game's (an empty one chosen, its description typed and erased, up to its field's width, the cursor's place and flashing); the game setup screen's controls and their choices; the empire selection screen's choosing and buttons |

IMA ADPCM without a WAV header, which the `.snd` archives need, is
checked with the shared audio code, in `common.unit.audio`.

### The basic engine test

`botf.engine` (`tests/engines/botf/engine.cpp`), without the game's
files: an install of its own (a `stbof.res` with a main menu, the
options, Load Game, Hall of Fame, game setup and empire selection
layouts, two saved games, pictures of plain colours, fonts and a
`lexicon.dic`), recognised as an unknown release, then the game run
headless: the main menu, a button pointed at and a dimmed one pressed,
New Single Player Game, the setup screen clicked (a setting, an empire's
level, Shift and every empire's), Accept, an empire chosen, Cancel back to
the setup and the menu, a new game again accepted back to the menu; the
options, a setting changed and a volume dragged, then cancelled; the
options again, tactical combat changed and accepted, and a new game's
setup showing it; Load Game, an empty slot clicked, a save chosen and
accepted; the Hall of Fame and its Return; Multiplayer Setup, Host,
Network and a game's name typed, then Cancel; and Quit Game. Each screen's checksum is in its record
(`data/engine-record.txt`).

### The game-files test

Not yet.

### What the automated checks don't cover

How the screens look against the original: their pictures, their text's
colours, what pressed buttons and the main menu behind look like.

## A playthrough by hand

1. Add the game's folder in the launcher; it's recognised as Star Trek:
   Birth of the Federation.
2. Run it: the opening movie with its sound (a key or a click skips it),
   then the main menu, Continue, Save Game and Retire dimmed; the
   button under the pointer lit.
3. New Single Player Game: the game setup screen over the dimmed menu,
   with every value as the manual's defaults (Early, Many, Normal, None,
   On, Manual, Domination, Irregular, Medium).
4. Click each control round its choices; Shift and a click on an empire's
   level sets every empire's.
5. Accept: the empire selection screen, each empire's level as set.
   Point at each empire for its description; click one to choose it.
6. Cancel goes back to the setup, with its settings kept, and its Cancel
   to the main menu.
7. Options: the options screen, every volume full, Manual, On, Yes. Click
   and drag along a volume bar; click each setting. Cancel, then Options
   again: nothing changed. Change Tactical Combat, Accept, then New Single
   Player Game: the setup shows Automatic.
8. Load Game: the install's saved games in their slots, the tutorials
   among them. Click one: it lights. Accept comes back to the main menu.
9. Hall of Fame: its own screen, the list empty. Return comes back to the
   main menu.
10. New Multiplayer Game: Host, then Network, Modem and Serial, each
    showing its panel; type in the three fields, Tab between them. Cancel
    comes back. Quit Game leaves.
11. In `reluva-viewer`, every page: every picture, font, sound, table and
    layout opens.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail | Windows (by the user) and Linux (headless) | The screens' text over stills of a recording; the viewer's pages on the game's files (in the ReluvaBOTF repository, before the move) |
