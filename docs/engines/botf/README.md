# The Star Trek: Birth of the Federation engine

Star Trek: The Next Generation - Birth of the Federation (MicroProse,
1999), a turn-based 4X strategy game. An engine for this one game.
Engine id `botf`, in `src/engines/botf/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| Star Trek: Birth of the Federation | `botf` | MicroProse / Hasbro Interactive, 1999 | Windows | English retail (`stbof.res`, `english.snd`); German by its `stbof.res` (its `german.snd` isn't hashed yet); any other by its files' names, as an unknown release | The main menu and the start of a new game: game setup and empire selection |

## Where it stands

- **Reading the game's files:** done for everything in `stbof.res` that
  isn't a model or texture, for `alt.res`, and for the `.snd` sound
  archives: the pictures, fonts, text tables, all 35 data tables, the
  window layouts and menus, and `lexicon.dic`. See [formats/](#this-folder).
- **The game:** it plays the opening movie, then the main menu, from
  which New Single Player
  Game opens the game setup screen over it, then the empire selection
  screen, all working from the game's own layouts, pictures, fonts and
  words, and matched against a recording of the game. There's no game to
  start yet: the empire screen's Accept comes back to the main menu. The
  menu's Options opens the options screen, Load Game the saved games'
  slots, Hall of Fame its list, empty so far, and New Multiplayer Game the
  multiplayer setup, without connecting to anyone.
- **The data viewer** (`reluva-viewer`) shows the pictures, fonts, sounds,
  text and data tables, and the window layouts.
- **Not yet:** the empires' movies, saving and loading a game, playing
  multiplayer, the galaxy map and every screen after it;
  the game's rules; the `.hob` models and their `.gif` textures; the AVI
  movies; saves; multiplayer.

The plan is in [PLAN.md](PLAN.md).

## Using it

- **Add the game's folder** (the install) in the launcher. A "minimum"
  install reads `music.snd` and the movies from the CD: Reluva looks for
  them in the folder `Stbof.ini`'s `CDPATH` names, and beside the install,
  so copying the CD's `music.snd` and `ENGLISH` folder into the game's
  folder does too ([systems/game-files.md](systems/game-files.md)).
- **The game** opens at the main menu: New Single Player Game for the
  game setup screen, where a click changes a setting (Shift and a click
  sets every empire's civilization level), then Accept for the empire
  selection screen, where pointing at an empire shows its description and
  a click chooses it. Options for the options screen: a click on a volume
  bar sets it (or drag along it), a click on a setting changes it, and
  Accept keeps the changes. Load Game lists the install's saved games; a
  click chooses one. Cancel goes back a screen; Quit Game, or Esc on
  the main menu, goes back to the launcher.
- **The data viewer:** `reluva-viewer` with the game. Tab moves between
  its pages:
  - **Images:** Left/Right step through `stbof.res`'s and `alt.res`'s
    pictures, PageUp/PageDown by 10, S the next archive, K shows the colour
    key (magenta) as transparent, I hides the status line.
  - **Fonts:** Left/Right pick one, +/- the sample text's size.
  - **Sounds:** Up/Down pick one, Left/Right the archive, Space plays or
    stops it, A plays each as it's picked, R changes the speech and music's
    sample rate (not stored; 22,050 Hz, the default, is right).
  - **Text:** Left/Right pick a table, Up/Down and PageUp/PageDown scroll,
    F changes the font.
  - **Data:** Left/Right pick a table, Up/Down and PageUp/PageDown a record.
  - **Layouts:** Left/Right pick one, Up/Down a widget, O turns the
    outlines off, S shows buttons' selected pictures, I hides the details;
    a selected menu is shown open.

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | A page per file format: [stbof-res.md](formats/stbof-res.md), [alt-res.md](formats/alt-res.md), [snd.md](formats/snd.md), [fnt.md](formats/fnt.md), [tdb.md](formats/tdb.md), [tables.md](formats/tables.md), [wdf.md](formats/wdf.md), [mnu.md](formats/mnu.md), [lexicon.md](formats/lexicon.md), [sav.md](formats/sav.md), [executable.md](formats/executable.md) |
| [systems/](systems/) | How the game behaves: [game-files.md](systems/game-files.md) (where it finds its files), [screens.md](systems/screens.md) (the screens rebuilt so far), [sound.md](systems/sound.md) (the main menu's music and sounds), [movies.md](systems/movies.md) (the opening movie), [galaxy-screen.md](systems/galaxy-screen.md) (the screen a game is played on) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `botf.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine`: the main menu, the game setup and empire selection screens |
| `game_files.cpp` | Finding the game's files: its folder, its language folder, the CD |
| `formats/` | Reading the game's files: `alt.res`, the `.snd` archives, fonts, text and data tables, window layouts, menus, `lexicon.dic` |
| `ui/` | The screens: what every screen drawn from a layout shares (`layout_screen.cpp`), the pictures, fonts and menus they draw with (`assets.cpp`), the main menu, game setup and empire selection |
| `pages/` | The data viewer (reluva-viewer's, never the game's) |
| `tools/` | `reluva-tool botf` commands (`reluva-tool help botf` lists them) |

## Tests

- `reluva-tests botf` runs its unit tests and its engine test
  (`tests/engines/botf/`).
- No game-files test yet: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **`stbof.res`:** the game's main archive, a zip of its pictures, models,
  layouts, tables and text ([formats/stbof-res.md](formats/stbof-res.md)).
- **Major races, empires:** the five the player can be: Cardassians,
  Federation, Ferengi, Klingons, Romulans, in that order in every table.
- **Minor races:** the 30 races the AI plays, each in its home system.
- **Civilization level:** what an empire starts with, from Beginning to
  Advanced ([systems/screens.md](systems/screens.md)).
- **Race letter:** the letter per-race files carry: `c`, `h` (the
  Federation: human), `f`, `k`, `r`.
- **Layout, widget:** a `.wdf` file and the parts of a screen it places
  ([formats/wdf.md](formats/wdf.md)).
- **Colour key:** the colour (magenta) that pictures use for what isn't
  drawn.
- **UE:** the Ultimate Editor, the modding community's editor, whose
  readers several pages draw on ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)).
