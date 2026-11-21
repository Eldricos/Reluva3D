# The Star Trek: Away Team engine

Star Trek: Away Team (Reflexive Entertainment / Activision, 2001), a
real-time tactics game in 2D. An engine for this one
game, rebuilt from its data files and from reading its executable.
Engine id `stawayteam`, in `src/engines/stawayteam/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| Star Trek: Away Team | `stawayteam` | Activision, 2001 | Windows | US English retail (`Star Trek.exe`, `DATA.dat`); any other by its files' names, as an unknown release | Recognised; the opening movies and the main menu: the difficulty window, the credits and Quit |

## Where it stands

- **Recognising the game:** done for the US English retail release, and
  *Import Game...* from its CD.
- **Reading the game's files:** `DATA.dat`, the archive holding nearly
  all of them ([formats/data-dat.md](formats/data-dat.md)); the objects
  in text that hold its rules, maps and missions
  ([formats/text-object.md](formats/text-object.md)); the scripts of its
  words ([formats/rds.md](formats/rds.md)); its pictures
  ([formats/frm16.md](formats/frm16.md)); its models, sequences, fonts and
  waypoint maps, its objects saved in binary
  ([formats/binary-object.md](formats/binary-object.md)), every one of
  them read.
- **The data viewer** (`reluva-viewer`) shows the pictures, sequences,
  models (each animation, turned every way), fonts, sounds, and every
  object and script as Reluva reads it.
- **The game's start:** its four opening movies, each skipped by a key or
  a click, then the main menu's screen, its movies and logo, with the
  startup song ([systems/startup.md](systems/startup.md)).
- **The menus:** the main menu's buttons, the difficulty window, the
  settings (brightness, scrolling, pausing when hit) and the sound's
  (volumes, voiceovers, stereo, mixing), the credits, and the dialog
  asking before quitting, and the keyboard's (the game's actions and
  their keys, the mouse's buttons); the game itself, saved games and
  network games aren't there yet
  ([systems/menus.md](systems/menus.md)).
- **The briefing:** a new game's first mission's movie, and its lines
  typed out beside the speaker's portrait
  ([systems/briefing.md](systems/briefing.md)).
- **Team selection:** the roster by field, the officer chosen, the team
  on the transporter and the items the mission needs; reached from a new
  game only at a later mission, by a developer setting, as mission 1 has
  none ([systems/team-selection.md](systems/team-selection.md)).
- **Maps:** the mission's first map, its ground, polygons and models,
  scrolled by the keys and the mouse; nothing moves on it yet
  ([systems/maps.md](systems/maps.md)).
- **Everything else** is still to come. The plan is in [PLAN.md](PLAN.md),
  and a first survey of the game's files is in
  [research/file-survey.md](research/file-survey.md).

## Using it

- **Add the game's folder** (the install) in the launcher. A full install
  has everything the game needs: the program, `DATA.dat`, and the movies
  in `DATA/Movies/`.
- **Or import it from the CD** (*Import Game...*): Reluva copies what it
  uses from the CD's `Setup` folder, where the install is loose: the
  program, `DATA.dat`, `texture.dat` and the movies. An installed copy
  can be imported the same way.
- **The game** plays its opening movies (a key or a click skips each),
  then shows the main menu. Its buttons work by the mouse or their
  underlined letter's key. Start New Game asks the difficulty (START GAME
  only closes it, for now); Settings shows the game's settings, kept
  between runs in `options.ini` in the game's folder for Reluva's files;
  Credits rolls the credits (RESUME or Esc closes them); Quit asks, Yes
  (or Y) going back to the launcher. Esc asks the same. Load and network
  games don't do anything yet. The movies are the install's
  `DATA/Movies/` files.
- **Settings** (with the game, or under Settings... for every game):
  **Opening movies** leaves out the logos and the introduction, which
  take over two minutes.
- **The data viewer:** `reluva-viewer` with the game. Tab moves between
  its pages, Esc leaves. On every page, Left/Right step through its files,
  PageUp/PageDown by 10, Home/End to the first and last, F to the next
  folder's.
  - **Pictures:** B changes what's behind the picture (dark, a
    checkerboard, magenta), Z the zoom (as large as fits, or 1-4 times), I
    hides the status line.
  - **Sequences** (played at their own speed): Space pauses, , and . step
    a frame, B and Z as for pictures.
  - **Models:** Up/Down choose the animation, , and . turn it, Space
    pauses, [ and ] step a frame, B and Z as for pictures.
  - **Fonts:** every character, and a line of text; Up/Down scroll, B the
    background.
  - **Sounds:** Space plays or stops one, A plays each as it's chosen.
  - **Objects:** any object in text or binary, or script, a field a line;
    Up/Down scroll a line, , and . a page.

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | A page per file format: [data-dat.md](formats/data-dat.md), [text-object.md](formats/text-object.md), [binary-object.md](formats/binary-object.md), [rds.md](formats/rds.md), [frm16.md](formats/frm16.md), [pal.md](formats/pal.md) |
| [systems/](systems/) | A page per part of how the game behaves: [startup.md](systems/startup.md) (the opening movies and the main menu's screen), [menus.md](systems/menus.md) (the menus, their buttons and fonts), [briefing.md](systems/briefing.md) (the briefing before a mission), [team-selection.md](systems/team-selection.md) (choosing the away team), [maps.md](systems/maps.md) (the maps and their drawing) |
| [research/](research/README.md) | Working notes: [file-survey.md](research/file-survey.md), what the install holds; [binary-objects.md](research/binary-objects.md), the objects saved in binary, and [classes.txt](research/classes.txt), the game's classes |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `stawayteam.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `game/` | The game's state: `options.cpp` (its options, kept between runs), `controls.cpp` (its actions and their keys), `team.cpp` (the roster, items and the missions' descriptions), `map.cpp` (the maps) |
| `ui/` | The game's screens: `movies.cpp` (playing a movie), `main_menu.cpp` (the main menu and the confirm dialog), `credits.cpp` (the credits), `difficulty.cpp` (the difficulty window), `settings.cpp` (the settings window), `sound_options.cpp` (the sound window), `key_config.cpp` (the keyboard screen), `briefing.cpp` (the briefing), `map_screen.cpp` (a map in the game's view), `animation.cpp` (sequences' frames in time), `team_selection.cpp` (the team selection), `static_text.cpp` (the menus' static texts), `widgets.cpp` (the window toolkit's buttons and toggles), `button.cpp` (the menus' buttons), `layout.cpp` (reading menus' places from `Rules.txt`), `font.cpp` (text in the game's fonts) |
| `formats/` | Reading the game's files: `text_object.cpp` (objects in text), `binary_object.cpp` (objects in binary), `object_schema.cpp` and the generated `class_table.cpp` (the game's classes), `rds_script.cpp` (`.RDS` scripts), `frame.cpp` (pictures) |
| `pages/` | The data viewer, in `reluva-viewer` only: `viewer.cpp` (its pages, Tab between them), a page each in `picture_viewer.cpp`, `sequence_viewer.cpp`, `model_viewer.cpp`, `font_viewer.cpp`, `sound_viewer.cpp`, `object_viewer.cpp`; what they share in `page.h`, `file_list.cpp`, `animation.cpp`, `draw.cpp` |
| `tools/` | `reluva-tool stawayteam` commands (`reluva-tool help stawayteam` lists them): `archive.cpp` for `DATA.dat`, `text.cpp` for its text, `objects.cpp` for its objects in binary, `images.cpp` for its pictures, `files.cpp` for the game's folder |

## Tests

- `reluva-tests stawayteam` runs its unit tests and its engine test
  (`tests/engines/stawayteam/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **Away team:** the officers the player picks for a mission from the
  ship's roster.
- **`DATA.dat`:** the archive holding nearly all the game's data: a ZIP
  file, with one change ([research/file-survey.md](research/file-survey.md)).
- **Object text:** the game's text format for its rules, maps and
  missions: nested blocks, each named by the C++ class it is read into
  (`CStarTrekRules`, `CLayerSaveData`...)
  ([formats/text-object.md](formats/text-object.md)).
- **Script:** an `.RDS` file of the game's words, its entries found by
  label ([formats/rds.md](formats/rds.md)).
- **Class, property:** the game's objects are of C++ classes, each with a
  list of properties it saves, in text and in binary
  ([research/classes.txt](research/classes.txt)).
- **Model:** a thing that can be on a map, with its animations
  (`.mdl16`, a `CModel`); **sequence:** an animation of frames (`.seq16`);
  **rotation sequence:** a model's animation, its frames for each direction
  ([formats/binary-object.md](formats/binary-object.md)).
- **Frame:** a picture in the game's 16-bit format, a `.frm16` file or
  one inside a sequence or model ([formats/frm16.md](formats/frm16.md)).
- **Blended pixels:** a frame's pixels drawn part see-through, with an
  alpha each: edges, shadows, panels ([formats/frm16.md](formats/frm16.md#blended-pixels)).
