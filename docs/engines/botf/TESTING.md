# Star Trek: Birth of the Federation: testing

## Automated checks

### Unit tests

`tests/engines/botf/unit/`, a file per topic, each a suite
`botf.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `botf.unit.detection` | Recognising the game, and an unknown release by its files' names |
| `botf.unit.archives` | `alt.res`'s LZSS (with bytes from the retail file's start, and a round trip) and directory, the `.snd` archives' layouts, finding files through `Stbof.ini` |
| `botf.unit.fonts` | The `.fnt` fonts' glyphs found by their frames, drawing and wrapping text |
| `botf.unit.tables` | The `.tdb` text tables, the data tables' layouts (and that every built-in layout fits its records), `lexicon.dic` |
| `botf.unit.layouts` | The `.wdf` layouts (nested windows, each widget type, cut-short files), the `.mnu` menus, the colour key, dimmed pictures |
| `botf.unit.screens` | The main menu's buttons, with and without a game in progress; the game setup screen's controls and their choices; the empire selection screen's choosing and buttons |

IMA ADPCM without a WAV header, which the `.snd` archives need, is
checked with the shared audio code, in `common.unit.audio`.

### The basic engine test

`botf.engine` (`tests/engines/botf/engine.cpp`), without the game's
files: an install of its own (a `stbof.res` with a main menu, the game
setup and empire selection layouts, pictures of plain colours, fonts and a
`lexicon.dic`), recognised as an unknown release, then the game run
headless: the main menu, a button pointed at and a dimmed one pressed,
New Single Player Game, the setup screen clicked (a setting, an empire's
level, Shift and every empire's), Accept, an empire chosen, Cancel back to
the setup and the menu, a new game again accepted back to the menu, and
Quit Game. Each screen's checksum is in its record
(`data/engine-record.txt`).

### The game-files test

Not yet.

### What the automated checks don't cover

How the screens look against the original: their pictures, their text's
colours, what pressed buttons and the main menu behind look like.

## A playthrough by hand

1. Add the game's folder in the launcher; it's recognised as Star Trek:
   Birth of the Federation.
2. Run it: the main menu, Continue, Save Game and Retire dimmed; the
   button under the pointer lit.
3. New Single Player Game: the game setup screen over the dimmed menu,
   with every value as the manual's defaults (Early, Many, Normal, None,
   On, Manual, Domination, Irregular, Medium).
4. Click each control round its choices; Shift and a click on an empire's
   level sets every empire's.
5. Accept: the empire selection screen, each empire's level as set.
   Point at each empire for its description; click one to choose it.
6. Cancel goes back to the setup, with its settings kept, and its Cancel
   to the main menu; Quit Game leaves.
7. In `reluva-viewer`, every page: every picture, font, sound, table and
   layout opens.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail | Windows (by the user) and Linux (headless) | The screens' text over stills of a recording; the viewer's pages on the game's files (in the ReluvaBOTF repository, before the move) |
