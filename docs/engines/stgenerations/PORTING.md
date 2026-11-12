# The port from STG-Engine

The engine came from [STG-Engine](https://github.com/Eldricos/STG-Engine)
(its `main` as of November 2026), a ScummVM-style reimplementation of the
game with its own SDL3 backend, common layer and launcher. In Reluva the
game's code, `engines/stg/`, became `src/engines/stgenerations/`, and
everything around it was replaced by Reluva's own. The game logic is as it
was: what each part of the game does was not changed in the port.

This page lists what each part became, and everything that changed or
didn't come across. Where the engine differs from the original game (not
from STG-Engine), see [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

## How it was ported

1. **Mechanically**, every file the same way: STG-Engine's namespaces,
   types (`Common::String` was already `std::string`), includes, key codes,
   byte readers and comments to Reluva's, tabs to four spaces, `T &x` to
   `T& x`. Only names and spacing changed in this pass.
2. **By hand**, what differs between the two projects: the files and
   archives, the screen and its pointer, input, sound, settings, saves,
   detection and the launcher's side, and the tests.
3. **Checked** by STG-Engine's own tests (all pass), new tests of what was
   rewritten, the engine test, and the game's own files
   ([TESTING.md](TESTING.md)).

## STG-Engine's later work

STG-Engine went on after its `main` was ported: the 19 commits of its
`claude/happy-faraday-hzpl8c` branch after `main` (`8e18ce3` to `36df0a6`)
were brought across the same way, each file merged three ways (Reluva's
file, STG-Engine's `main` and its later version, both put in Reluva's
style), so the port's own changes stayed. They add:

- **Keys:** the original's key maps, changed on the Controls screen
  ([formats/menu.md](formats/menu.md#keys)), with Run, Mouse Look and
  Lookspring in missions; `game/keymap.h` is new. The maps are kept in
  `settings.ini` as `away_keys` and `starship_keys`.
- **Space combat:** the enemy view placed as the original places it, the
  viewscreen's zoom and the radar's range sliders, the radar as the
  original draws it, the start facing the nearest enemy, the system and
  shield facing displays, steering by the mouse, the stars, the shields
  (their hit flashes, the facing spheres, the Enterprise's jolt), and
  saving at the start of a fight ([formats/combat.md](formats/combat.md)).
- **Missions:** actors guarding what SET_ACTOR_COMBAT_PARAMS names
  ([formats/levels.md](formats/levels.md)).
- **The menu:** the window size shown dimmed, as the view is always the
  large one.
- **Tests:** five more cases (key maps, the Controls screen, a save in
  space combat, the shields' materials, guarding actors), and the engine
  test's record made again for the changed settings screen, saves (their
  version is 7) and settings.

## Where each part went

| STG-Engine | Reluva |
|---|---|
| `engines/stg/stg.cpp`, `frontend.cpp` (`STGEngine`) | `game/generations.cpp`, `game/frontend.cpp` (`Generations`, no longer an `Engine` subclass: `stgenerations.cpp` runs it) |
| `engines/stg/detection.cpp`, `detection_tables.h` | `stgenerations.cpp` (`kVariants`, the MetaEngine), `detection.cpp` (the fallback by executable, the CDs, importing) |
| `engines/stg/movie.cpp` (Cinepak, AVI) | `src/common/video/cinepak.cpp`, `avi.cpp`, for any engine |
| `engines/stg/resources.cpp`, `font`, `overlay`, `sprite`, `rgbimage`, `framedimage`, `level`, `script`, `opcodes`, `disasm`, `combatdata`, `ini`, `locations` | `formats/` |
| `engines/stg/` the rest (campaign, bridge, mission, vm, actors, movers, projectiles, walker, hud, hudscreen, helditem, combat, combatrender, render3d, screen, dissolve, sound, optionsmenu) | `game/` |
| `common/graphics/surface`, `pixelformat` | `canvas.cpp` (`Canvas`: the game's 8-bit and 0xAARRGGBB surfaces, as before) |
| `common/graphics/palette` | `src/common/palette.h` (`Palette`; its nearest colour is the same) |
| `common/image/bmp` | `src/common/bmp.h`, with `formats/bitmap.cpp` making the game's `Canvas` of it |
| `common/audio/` (mixer, streams, WAV) | Reluva's mixer (`src/common/audio/`), with `audio.cpp` giving the game the calls it was written with |
| `common/serializer.h` | `src/common/serializer.h`, the same byte layout |
| `common/random` | `game/random.cpp`, the same numbers |
| `common/system.h` (`OSystem`), `events` | `platform.cpp` (`Platform`, over Reluva's `System`) |
| `common/debug` | `src/common/log.h` and `debug.cpp` (the debug channels) |
| `common/zip`, `compression/inflate`, `archive`, `fs`, `file`, `stream`, `str`, `rect`, `endian`, `md5` | Reluva's own (`src/common/`) |
| `common/config-manager`, `savefile` | The launcher's settings (`GameTarget::settings`) and the game's folder (`GameTarget::savePath`) |
| `engines/engine`, `metaengine`, `launcher`, `game`, `probe` | Reluva's `Engine`/`MetaEngine`, launcher and `reluva-tool` |
| `sdl/` (the backend) | Reluva's SDL3 backend |
| `test/` | `tests/engines/stgenerations/` ([below](#tests)) |
| `doc/formats.md` | [formats/](formats/README.md), a page per format |

## Shared code

Added to `src/common/` for this engine, for any engine to use, none of it
changing what was there (each engine's tests and records are the same):

- **The game's own mouse pointer:** `System::setMouseCursor` and
  `common/cursor.h`. The SDL backend shows it as the system's pointer,
  scaled with the picture (or draws it itself where it can't).
- **Videos:** Cinepak and AVI (`common/video/cinepak.h`, `avi.h`).
- **Pictures:** BMPs as stored, palette indices with their palette
  (`common/bmp.h`).
- **Saved games:** `Serializer` (`common/serializer.h`).
- **Input:** the keypad's + - * / keys, and the window's focus gained and
  lost (`common/events.h`).
- **Sound:** pausing one sound or all of them (`Mixer::setPaused`,
  `setAllPaused`).
- **Archives:** `ZipArchive::readUnchecked`, a stored file read whatever
  its CRC says.
- **Helpers:** big-endian and signed stream reads, `readLine`,
  `readFixedString`, `writeString`; `text::toLower`, `hasSuffixIgnoreCase`,
  `matchGlob`; byte readers and four-character codes (`bytes.h`); `Rect`'s
  clipping and tests; `Palette::getColor`.

## What changed

### Running it

| STG-Engine | Reluva |
|---|---|
| `stg --path=... --cd1=... --cd2=...` | **Import Game...** (the install and both CDs into one folder), or the install added and `cd1=`, `cd2=` in reluva.ini |
| `stg --add`, `stg <target>`, `--list-targets` | The launcher's library; `reluva <entry>` |
| `stg --detect` | `reluva-tool detect <folder>` |
| `stg --dump-files` | `reluva-tool stgenerations files <folder>` (with each file's signature) |
| `stg --probe` | `reluva-tool stgenerations probe <file or folder>` |
| `stg --extract --pattern=... -o ...` | `reluva-tool stgenerations extract <archive> <folder> [pattern]` |
| `--probe` on a script | `reluva-tool stgenerations script <GAME.BIN>` |
| `--viewer` | Developer setting *Start with: The resource viewer* |
| `--story=<section>`, `--combat=<n>` | `story=`, `combat=` in reluva.ini |
| `-d<level> --debugflags=...` | Developer settings *Console output* and *about* (`debug_level`, `debug_channels`) |
| `-f`, `--filtering`, `--integer-scaling` | Reluva's display settings: full screen, smooth, whole multiples |
| `--engine=stg --game=stg` (forcing the engine) | Not needed: any folder with the game's executable is found, as a release Reluva doesn't know |
| `stg --help`, `--version`, `--list-engines`, `--list-games` | The launcher; `reluva-tool help stgenerations` |
| Ctrl+Alt+F (filtering) | Not kept: the *Smooth* display setting |
| Alt+Enter (full screen) | The same |

### Settings and saves

- **Where:** STG-Engine kept `stg.ini` and `saves/` in its own per-user
  folder; here the game's folder of Reluva's (`games/<entry>/`) holds
  `settings.ini` (the menu's settings) and the saves.
- **Save names:** `<target>.s<nn>` became `stg.s<nn>`; the format is the
  same, so STG-Engine's saves load once renamed.
- **Settings:** the menu's volumes, difficulty, subtitles, large window and
  lookspring are kept as before (in `settings.ini`); the launcher's music,
  effects, speech, difficulty and subtitles settings are what they start
  as. STG-Engine's 0-255 volumes are 0-100% in the launcher. `mute` is
  Reluva's master volume at 0.

### Inside

- **The pointer:** STG-Engine's backend drew the game's 8-bit pointer; now
  Reluva's System shows it as the system's own pointer, coloured from the
  palette as before and remade when the palette changes.
- **Typed names:** a saved game's name came from the keys pressed, read
  with the keyboard's layout by STG-Engine's backend; now from the text
  the system types (Reluva's text input), while the name is being typed.
  Printable ASCII, as before.
- **Archives:** STG-Engine streamed stored files from the archive without
  checking them, and checked the CRC of compressed ones; Reluva reads each
  whole into memory, the same files in the same way (stored files whatever
  their CRC says, with a warning). A movie is read whole before it plays
  (up to 92 MB), where STG-Engine streamed it.
- **Sound:** the game's calls (play with a volume and balance, stop,
  pause, loop) reach Reluva's mixer, which resamples and mixes as
  STG-Engine's did; the volumes are scaled the same. STG-Engine's mixer let
  a sound replace another of the same id; the game never used that.
  "Plain" sounds (none in the game) share the effects' volume.
- **Pictures:** 32-bit BMPs with bit fields are read by their masks (STG-
  Engine took them as B, G, R whatever the masks said); none of the game's
  are. Duplicate names in an archive are listed once, as before.
- **Without a sound device**, nothing is played, as STG-Engine's mixer
  did when it wasn't ready.
- **The screen:** STG-Engine's backend converted 0xAARRGGBB frames;
  `Platform` does it now. Changing between 8-bit and true colour (the
  bridge, combat, the movies) is the same.
- **Pausing:** on losing the window's focus, as before, now with Reluva's
  focus events.
- **Errors:** STG-Engine's `Common::Error` codes became Reluva's `Error`
  with the same messages.
- **The layering test:** STG-Engine's check that its common layer never
  included SDL is Reluva's build and CI (engines are built without SDL's
  headers, and each alone).

## The resource viewer

As in STG-Engine (*Start with: The resource viewer*, or when the story's
data isn't there): the starting screen lists the away missions found;
Up/Down choose one and Enter plays it after its briefing. Esc leaves a
mission for the list. Left/Right step through images, PgUp/PgDn between
archives, Tab and Shift+Tab between 3D levels; away from the starting
screen, Up/Down step through sounds and Space replays one. On a level, `[`
and `]` go from the floor plan to the 3D view and its textures; in 3D, L
switches the lighting and Shift+C walks through walls. Home returns to the
starfield, F8 plays the movies in turn, F5/F7 save and load, Esc quits.

## Tests

| STG-Engine | Reluva |
|---|---|
| `test_script`, `test_level`, `test_hud`, `test_combat`, `test_campaign`, `test_sound` | `stgenerations.unit.script`, `.level`, `.hud`, `.combat`, `.campaign`, `.sound`: the same 51 cases, and the 5 of STG-Engine's later work |
| `test_engines` (fonts, subtitles, overlays, sprite sets, script files) | `stgenerations.unit.formats`: the same 5 cases |
| `test_graphics` | `stgenerations.unit.canvas`: the same 7 cases, the pixel format one for the game's two formats |
| `test_audio` | `stgenerations.unit.audio`: rewritten for the game's calls on Reluva's mixer |
| `test_engines` (detection, the two-CD search paths) | `stgenerations.unit.detection`: rewritten, with importing |
| `test_engines` (the engine run headless) | `stgenerations.engine`: the whole game, built in code, imported and played |
| `test_movie` | `common.unit.video` (`cinepak`, `avi`) |
| `test_common`, `test_zip`, the generic detection table test | Left out: they tested STG-Engine's common layer, now Reluva's, which has its own tests |
| — | The game-files test, `stgenerations.gamedata.*` (new) |

## Status

- [x] Shared code: the pointer, videos, BMPs, the serializer, keys, focus,
  pausing, archives with damaged files
- [x] The game's code, compiling with no warnings
- [x] Detection, importing with the CDs, settings, saves
- [x] reluva-tool commands
- [x] STG-Engine's tests; the engine test; the game-files test and its
  records
- [x] Docs
- [x] Checked on the game's own files: the front end, the bridge, a
  mission, space combat, saving and loading, all the data read
- [ ] A full playthrough in Reluva ([TESTING.md](TESTING.md#still-to-check))
