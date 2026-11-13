# The Star Trek: Generations engine

`stgenerations` runs MicroProse's **Star Trek: Generations** (1997, Windows,
two CDs) from the player's own copy, from the title to the credits: the
captain's menu, the story, the bridge and its star map, the eleven away
missions in 3D, space combat, and the movies.

It came to Reluva from [STG-Engine](https://github.com/Eldricos/STG-Engine),
ported with its game logic unchanged; [PORTING.md](PORTING.md) says what
changed on the way and what was left behind.

Engine id `stgenerations`, in `src/engines/stgenerations/`.

## Games

One game: MicroProse's Star Trek: Generations (1997, Windows, two CDs).

### Releases

| Release | Recognised by |
|---|---|
| The 2 CD Windows release (English), installed | `sgens.exe` and `DATA/COMMON.DAT` (`kVariants`, `src/engines/stgenerations/stgenerations.cpp`) |

Any other release is still found by its executable naming the game, and is
shown as *a release Reluva doesn't know*; it runs the same way. To add one
to the table, `reluva-tool stgenerations files <game folder>` prints every
file's size and signature.

## Where it stands

Every part of the game runs on the game's own files: the title, the menu,
a new game's movies, the bridge, an away mission, space combat, saving and
loading ([TESTING.md](TESTING.md)). STG-Engine's tests all pass on the
port. It hasn't been played through from start to finish in Reluva yet;
that, and the few effects not yet compared with the original
([UNCONFIRMED.md](UNCONFIRMED.md)), are what's left.

## Using it

### Adding the game

The game runs from the folder its setup installed (the one with
`sgens.exe` and `DATA/`), with its two CDs: the setup leaves the story
(`STELLAR.DAT`), space combat (`COMBAT.DAT`), the music (`MUSIC.1`,
`MUSIC.2`) and the movies (`MOVIE/`) on the CDs. Reluva can't run the
game's setup itself: install the game with it first (on Windows, or in
Wine), or use an install made before.

- **Import Game...** (recommended) copies the install and, from each CD,
  only what the setup left there, into a folder of its own in Reluva's
  library (the CDs' files in `disc1/` and `disc2/`). Pick a folder holding
  the install and both CDs (or copies of them) side by side, and all three
  are found at once; or start from any one of them and add the rest with
  **Add CD or installed game...**. Where copies of an archive differ (one
  read badly when it was copied), the whole one is copied: between the two
  CDs, and between the install and a CD for the archives the setup copied
  in (it still goes in the install's `DATA/`).
- **Add Game...** on the install alone works too, but without the CDs
  there's no story to play: the launcher says so. The CDs can then be given
  in `reluva.ini` (`cd1=` and `cd2=`, below).

### Playing

The game starts as the original does: the title, then the captain's menu
(the original goes straight from the title into a new game; here the menu
comes first, so a saved game can be loaded instead). **New** starts the
story: its movies, then each away mission in the order the script gives,
branching on whether its objectives were met, through to one of the endings
and the credits. **Esc** in a mission, or **OPTIONS** on the bridge, opens
the menu: Load, Save (nine slots plus the automatic save the story makes),
New, Quit (the credits, then back to the launcher), Settings (effects and
music volume, difficulty, subtitles; the window size is shown dimmed, as the
view is always the large one) and Controls (the keys: click a box and press
the key or mouse button for it, or Esc to give up; each action can have two,
on the away and starship pages, and Lookspring levels the view when Mouse
Look is let go). A save made in space combat, as in the original, starts
that fight again from its briefing.

- **The bridge:** click a star, planet, station or fleet to select it; SET
  COURSE warps the Enterprise there, FULL SECTOR VIEW / SYSTEM VIEW zoom,
  LONG / SHORT RANGE SCAN reads what's there, SIM. DESTRUCTION shows where
  the Nexus would go if a star died, and the button under SET COURSE offers
  TRANSPORT (beam down), TACTICAL (engage) or the COMPUTER LOG (the current
  hint). Time passes only as you act: a warp takes a stardate, a scan a
  tenth, and the story's deadlines run on stardates. Space skips the speech
  playing.
- **Away missions:** the keys are the Controls screen's, by default the
  original's: the arrows walk and turn, Num / and Num * slide, PgUp/PgDn
  look up and down, Home levels the view, Enter jumps, Ctrl crouches while
  held, Shift runs, and holding the right mouse button looks with the mouse
  (the further from the middle of the view, the faster it turns). While the
  keys are free, WASD also walk and slide, End levels the view, X jumps and
  C crouches. Insert or the left mouse button attacks: with the weapon in
  use, held up in the view, it fires at the pointer; with none,
  bare-handed. `[` and
  `]` set the weapon's power. 1-9 use the items carried; click inventory
  items to use them (right click to have them described), drag them to
  another slot or out to drop them; B, H and O switch the left panel (life,
  map, object).
- **Space combat:** after the briefing (PROCEED), with the original's
  default keys, fly by hand with the arrows or Num 8/2/4/6 (Up dives, Down
  climbs), Ctrl and Num 0 to roll, Num + and Num - for speed, or give the
  helm an order with 1-7 (Close on target, Evade, Maintain distance, Full
  stop (also Num 5), Stand by, Warp out, Maintain status) or the menu the
  CONN line calls up. Shift fires phasers, Enter photon torpedoes. The
  Controls screen changes any of these; while the keys are free, Q / E also
  roll, = and - set the speed, and F and G fire. T or the button by the
  radar picks the next target, V switches between the forward and enemy
  views, the slider at the right of the viewscreen zooms it, the one by the
  radar sets its range, holding the mouse on the viewscreen steers towards
  it (above the middle climbs), the buttons at the foot of each systems
  panel switch it between the system and shield facing displays, and
  clicking the target's schematic picks a system to aim at.
- **Anywhere:** Alt+Enter switches between a window and full screen;
  Ctrl+F5 goes back to the launcher. The game pauses, its sound too, while
  its window isn't the one in front.

### Settings

Under **Settings...** for all of the engine's games, or with the game:

| Setting | What it does |
|---|---|
| Music, effects and speech volume | 0-100%; 75% is the original's default |
| Difficulty | Captain, Lieutenant (the original's default) or Ensign |
| Subtitles | The crew's words under the picture |

The game's own Settings in its menu set these too, and once they have,
theirs win: they're kept in the game's folder (`settings.ini`), as the
original kept its own. Reluva's master volume and display settings (full
screen, window size, whole-multiple scaling, smoothing) apply as for every
game.

### Developer settings

Not part of the game, for studying it:

| Setting | Where | What it does |
|---|---|---|
| Console output, about | launcher | Messages as the game runs, and which topics (files and archives, the missions' scripts, sound) |
| `story=<section>` | reluva.ini | Start the story at a section of `STATES.INI`, as if a new game had got there (`story=Amargosa2`; `story=list` prints them with console output on) |
| `combat=<n>` | reluva.ini | Start in space combat n (1-25) |
| `cd1=<folder>`, `cd2=<folder>` | reluva.ini | The CDs' folders, when they aren't in the game's own |
| `fixed_save_date=<date>` | reluva.ini | For tests: saved games dated this (`01/01 00:00`), not by the clock, so a run is the same whenever it's made |

### Saved games

Saves go in the game's own folder of Reluva's (`games/<entry>/`), as
`stg.s00` to `stg.s08` (the menu's slots) and `stg.s09` (the automatic
save). The format is STG-Engine's: its saves (`<target>.s00` and so on, in
`saves/` of its folder: `~/.local/share/STG-Engine/stg/` on Linux,
`%APPDATA%\STG-Engine\stg\` on Windows) load once copied in and renamed to
`stg.s<nn>`. STG-Engine's `stg.ini` isn't read; set its volumes and
difficulty again in the menu.

## This folder

| Page | What it has |
|---|---|
| [PORTING.md](PORTING.md) | The port from STG-Engine: what each part became, what changed, what didn't come across |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where the engine knowingly differs from the original game |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been compared with the original yet |
| [TESTING.md](TESTING.md) | The tests, and what has been checked on the game's own files |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How the formats and behaviour were worked out, and the tools for it |
| [formats/](formats/README.md) | The game's file formats, a page each |
| [research/](research/README.md) | The working notes from reading `sgens.exe` that the engine was built from |

## Code

`src/engines/stgenerations/`:

| Folder or file | What it has |
|---|---|
| `stgenerations.cpp` | The MetaEngine (releases, import, settings) and the Engine the launcher runs |
| `detection.cpp` | The game's parts in a folder (the install, each CD) and importing them |
| `platform.cpp` | The game's view of Reluva's System: its screen, pointer, input, time, mixer and files |
| `canvas.cpp`, `audio.cpp`, `debug.cpp` | The game's drawing surface, its sound calls on Reluva's mixer, its debug output |
| `formats/` | The game's files: archives, bitmaps, fonts, overlays, sprite sets, levels, scripts, space combat's data |
| `game/` | The game: the front end, the story, the bridge, missions and their scripts, actors, the HUD, space combat, the 3D renderers |
| `pages/` | The resource viewer, `reluva-viewer`'s and never the game's: `ResourceViewer`, the game (`Generations`) without its front end and with the viewer's pages and keys ([PORTING.md](PORTING.md#the-resource-viewer)) |
| `tools/` | `reluva-tool stgenerations` commands (`reluva-tool help stgenerations` lists them) |

Shared code it brought to `src/common/`: Cinepak and AVI
([docs/common/video/cinepak.md](../../common/video/cinepak.md)), BMPs with their
palettes, the serializer for saved games, the game's own mouse pointer
(`System::setMouseCursor`), the keypad's operator keys, focus events,
pausing sounds. [PORTING.md](PORTING.md#shared-code) lists it all.

## Tests

- `reluva-tests stgenerations` runs the unit tests (STG-Engine's, and
  detection's) and the basic engine test: a game built in code, imported
  and played through.
- The game-files test: `RELUVA_STGENERATIONS=<the game's folder> ctest -L
  gamedata`.

[TESTING.md](TESTING.md) has both, and what's been checked.
