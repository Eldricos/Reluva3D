# The Star Trek: Deep Space Nine - Harbinger engine

Engine id `stharbinger`, in `src/engines/stharbinger/`. It runs *Star Trek:
Deep Space Nine – Harbinger* (Viacom New Media, 1996, DOS, two CDs) from the
player's own copy. It came to Reluva from STDS9H (the stand-alone engine for
this game it was first written as), ported with its game logic as it was
([PORTING.md](PORTING.md)).

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| Star Trek: Deep Space Nine – Harbinger | `ds9harbinger` | Viacom New Media, 1996 | DOS, two CDs | English (DOS) | Runs everything the game does; not yet played on the discs in Reluva |

### Releases

Recognised by the game's files (`findGameParts`,
`src/engines/stharbinger/detection.cpp`): disc 1's scripts (`SSE/SSE.ALL`, or
an install's `CC.ALL`) and the data on the discs (`DS9/`), each CD's number
in `DS9/TORPEDOS/CD`.

Which release it is comes from Reluva's table of the releases' file
signatures (`kVariants` in `stharbinger.cpp`). Its entries are checked
against each part of the game found, each CD as it is, so a CD is
recognised alone, beside the other, or in Reluva's import (`disc1/`,
`disc2/`); the part with the scripts is tried first.

| Release | Disc 1's files |
|---|---|
| English (DOS) | `SSE/SSE.ALL` (2,865,766 bytes), `SSE/SSE.LST` (308), `SSE/SSE.CAT` (779) |

The entry was checked in November 2026 on disc 1's `SSE.ALL`: its size and
hash match. `SSE.LST` and `SSE.CAT` were rebuilt from `SSE.ALL`'s 76
entries (their offsets, and their names from the installed files) and match
too, byte for byte. With the three files, Reluva's matcher (`detectFromTable`)
finds this release; with one byte of `SSE.LST` changed, it finds none.

For another release or language, run `reluva --hash` on disc 1's files
from the CD's root (so the names include `SSE/`) and add its entry:

- `SSE/SSE.ALL`: the installer's archive. `DEEP.EXE`, the scripts and the
  dialogue text are only inside it on the CD, so another build or language
  changes it. Its signature is of its first 5,000 bytes and its size, and
  the start may be the same in every release, so:
- `SSE/SSE.LST` and `SSE/SSE.CAT`: small, and they say where each file
  inside the archive starts and what it's called, so any change inside
  shows in them.

`DS9/TORPEDOS/CD` only says which disc a CD is, the same in every release,
so it's no use there. A disc-2 file (a video in `DS9/INTRO`, say) can be
added for a release that differs on disc 2. For any release not in the
table, the file check names the game.

## Where it stands

Every data format is decoded. The engine runs the scene scripts
(backgrounds, hotspots, variables, timers, animations, sounds, scene
changes), the conversations (speech with lip-sync and the reply menu), the
videos (Smacker), the game's mouse cursors and the action sequences (left
button: phaser, right button: torpedoes), the game menu with saving and
loading, the credits and the deck map. Everything the game does is there;
what remains is checking it against the original
([TESTING.md](TESTING.md)).

In STDS9H it was checked on the real discs; the port to Reluva passes all of
STDS9H's tests and Reluva's own, but hasn't been played on the discs yet.

**Left to clean up:** `Game::playVideoFile`
(`src/engines/stharbinger/game/game_video.cpp`) only serves the viewer's
one-video mode, but stays in the game's library, and so in `reluva`,
because it drives `Game`'s own video state. Moving it to `pages/` means
giving the viewer a way to start and run a `Game`'s video.

## Using it

### Adding the game

The game needs both CDs for all of its scenes (disc 1 has the scripts; each
disc has its part of the data and videos).

- **Import Game...** (best): pick disc 1 (or a copy of it), then **Add other
  disc...** for disc 2. Both are copied into Reluva's library, as
  `disc1/` and `disc2/`, and played from there.
- **Add Game...** plays from where the game is: a folder holding copies of
  both CDs side by side (any names), one CD (*disc 1* is shown in the list:
  disc 2's scenes will be missing), or a folder the game was installed or
  extracted to, beside the CDs' data. CD folders whose names show a `;1`
  version are fine.

### Playing

As the original: left-click hotspots and replies; right-click opens the game
menu (new game, save, load, quit, difficulty, credits); **M** shows the deck
map where the game allows it; **Esc** skips videos. Reluva adds a few
conveniences ([KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)): **Esc** also opens
(and resumes from) the game menu, confirmations take **Y/N/Enter/Esc** as
well as the mouse, **M** also closes the deck map, the menu appears on the
very first start too, and it keeps its own colours behind a saved game's
picture. *Play exactly as the original* turns these off.

**Ctrl+F5** goes back to the launcher, **Alt+Enter** switches full screen.

### Settings

Set for every game (**Settings...**, *Every game*), for all Harbinger games
(*All Star Trek: Deep Space Nine - Harbinger games*) or for one (*This
game's settings*). Reluva's own (display, master volume) are in the main
[README](../../../README.md#settings). The engine's, kept in `reluva.ini` as:

| Setting | `reluva.ini` key | Choices | Default |
|---|---|---|---|
| Play exactly as the original | `original` | on, off | off |
| Music volume (action sequences' music, videos' soundtracks) | `music_volume` | 0% to 200% | 100% |
| Speech volume (conversations, action sequences' voices) | `speech_volume` | 0% to 200% | 100% |
| Effects volume (every other sound) | `effects_volume` | 0% to 200% | 100% |

The volumes are shares of the original game's levels: 100% sounds as the
game does.

### Developer settings

**These exist in the engine for studying the game, and aren't part of it.**
They show in the launcher's lists with *Developer:* before them, and are off
unless set. The launcher's two, and the keys they turn on, are only in
builds with `RELUVA_DEVELOPER_KEYS` on (the default; release builds turn it
off, [DEVELOPMENT.md](../../DEVELOPMENT.md#build-folders)).

| Setting | `reluva.ini` key | What it does |
|---|---|---|
| (no launcher entry) | `start_scene` | The scene to start in (its name, e.g. `BRIDGE`, or CC.ALL slot), with every variable at 0, so later scenes may not behave as in a playthrough; in the scene viewer (below), the scene it opens on, or with `start_mode=video`, the video in DS9 (`INTRO/VNM.SMK`). Set it in the game's section of `reluva.ini` |
| (no launcher entry) | `start_mode` | For the scene viewer only: `video` plays the video named by `start_scene` on its own instead. The game then starts as usual, ignoring `start_scene` |
| Developer: debug keys | `debug` | **F2** shows the hotspot rectangles (green on, red off), **F10** prints the variables that are set, and the title bar names the scene, the hotspot under the mouse and anything skipped |
| Developer: console output | `debug_level` | How much the engine prints (0 to 3), with the debug keys on |

### The scene viewer

The engine's data viewer, in `reluva-viewer` rather than the game
(`reluva-viewer <name>` or `reluva-viewer <folder>`; see the main
[README](../../../README.md)): it shows a scene's resources without running
its script.

| Key | Action |
|---|---|
| Left / Right | previous / next background of the scene |
| Up / Down, PgUp / PgDn | previous / next scene |
| H | show hotspot rectangles; hover one to see its name in the title bar |
| A, Shift+A | next / previous animation |
| S, Shift+S | next / previous sound |
| Space | stop animation and sound |
| Esc, Q | back to `reluva-viewer`'s list |

### Saved games

In the game's own folder of Reluva's
([where Reluva keeps its files](../../../README.md#where-reluva-keeps-its-files)):
`games/<entry>/savegame/*.ds9`, in the original's format, and the original's
settings (difficulty, autosave, the last save shown) in
`games/<entry>/dlas.ds9`. Saves from the DOS game's `SAVEGAME` folder can be
copied in, and saves made in Reluva copied back. STDS9H kept them in its own
folder (`~/.local/share/STDS9H/stds9h/harbinger/` on Linux,
`%APPDATA%\STDS9H\stds9h\harbinger\` on Windows): copy its `savegame` folder
and `dlas.ds9` across. STDS9H's `stds9h.ini` isn't read; its settings are
Reluva's now.

## This folder

| Page | What's in it |
|---|---|
| [formats/](formats/) | The game's files: [discs](formats/discs.md), [archives](formats/archives.md), [backgrounds and palettes](formats/backgrounds.md), [animations](formats/animations.md), [sound](formats/audio.md), [deck maps](formats/deck-maps.md), [cursors](formats/cursors.md), [action sequences](formats/action-sequences.md), [videos](formats/videos.md), [saved games](formats/saves.md), [scripts](formats/scripts.md), [the executable](formats/executable.md) |
| [systems/](systems/) | How the game runs its data: the [script reference](systems/script-reference.md) |
| [research/](research/) | The working notes from reading DEEP.EXE: the scene interpreter, dialogue, the menu and saves, the arcade sequences, cursors and video, special functions, and address lists |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, the notes from reading DEEP.EXE ([research/](research/)), and the tools |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where it knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | Details inferred or guessed from the executable |
| [TESTING.md](TESTING.md) | The tests, and what has been checked on the real discs |
| [PORTING.md](PORTING.md) | The port from STDS9H: where each part went, what changed, STDS9H's options as Reluva's settings, and STDS9H's history |

## Code

| Folder | What's in it |
|---|---|
| `stharbinger.cpp` | The `MetaEngine` (detection, importing both CDs, the settings) and the `Engine` |
| `detection.*` | The game's parts in a folder: CDs, an install |
| `harbinger.*` | The game's data and options, and running the game |
| `platform.*` | The game's side of Reluva's `System`: its 8-bit screen, the player's input as the game takes it, the time, the mixer, its save folder |
| `formats/` | The game's files: archives and the search path over the discs, scripts, name lists, backgrounds, animations, sound, dialogues, fonts |
| `game/` | The game: the scene scripts, conversations, videos and cursors, the action sequences, the menu and saved games, credits and deck map |
| `pages/` | The scene viewer, and the viewer's `MetaViewer` (`meta_viewer.cpp`): built for `reluva-viewer` only, never into `reluva` |
| `tools/` | Its `reluva-tool` commands ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)) |

Shared code it uses: the Smacker decoder (`src/video/`,
[smacker.md](../../common/video/smacker.md)), PKWARE DCL and byte streams
(`src/common/`, [dcl.md](../../common/dcl.md)), 8-bit drawing and palettes
(`src/graphics/`), and the mixer and its streams (`src/audio/`).

## Tests

- `reluva-tests stharbinger` runs its unit tests and its engine test
  (`tests/engines/stharbinger/`); see [TESTING.md](TESTING.md).
- The game-files test: `RELUVA_STHARBINGER=<the game's folder> ctest -L
  gamedata` ([DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests)).

## Terms

- **STDS9H:** the stand-alone engine for this game the engine was first
  written as, before it came to Reluva ([PORTING.md](PORTING.md)).
- **DEEP.EXE:** the game's program, installed from disc 1's `SSE/SSE.ALL`
  ([formats/executable.md](formats/executable.md)).
- **DOS/4GW, LE:** the DOS extender that runs `DEEP.EXE`, and the 32-bit
  executable format it loads.
- **Directive:** one command of a scene or dialogue script, with its
  arguments ([formats/scripts.md](formats/scripts.md)).
- **Name lists, `CCS`:** each scene's lists of the names its scripts use
  (`CA` to `CV`), and its list of every symbol, which speaker codes come
  from ([formats/scripts.md](formats/scripts.md)).
- **`ACTIONSEQ`:** the script command that starts videos, action sequences
  and other special functions
  ([systems/script-reference.md](systems/script-reference.md)).
- **Action sequence:** one of the game's arcade fights, played over a video
  ([formats/action-sequences.md](formats/action-sequences.md)).
- **Work buffer:** the picture of the scene the renderer keeps from step to
  step, animations erased and drawn on it
  ([systems/script-reference.md](systems/script-reference.md)).
- **Talker:** a speaker whose mouth animation follows the phonemes while
  they speak ([systems/script-reference.md](systems/script-reference.md)).
