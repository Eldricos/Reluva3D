# Star Trek: Deep Space Nine - Harbinger: the port from STDS9H

The engine came from STDS9H (`Eldricos/STDS9H`, a private repository), a
stand-alone, ScummVM-style engine for this game with its own SDL3 backend,
common layer and launcher, written from 20 to 29 October 2026. In Reluva the
game's code, `engines/harbinger/`, became `src/engines/stharbinger/`, and
everything around it was replaced by Reluva's own. The game logic is as it
was: what each part of the game does was not changed in the port.

This page lists what each part became, everything that changed or didn't
come across, and what STDS9H's history established, so that nothing needs
its commits. Where the engine differs from the original game (not from
STDS9H), see [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

## How it was ported

1. **The code as of 21 October 2026** (STDS9H's `38e0393`): every file the
   same way, STDS9H's names (`_member`, `uint8`, `Harbinger::`) to Reluva's
   (`member_`, `uint8_t`, `reluva::stharbinger::`), its common layer's calls
   to Reluva's, then by hand what differs between the two projects: files and
   archives, the screen, input, sound, settings, saves, detection, the
   launcher's side and the tests.
2. **STDS9H's last engine commits** (`a7ed84a`, `41ea318`, 28–29 October)
   were ported by hand in November 2026, after a comparison of every file
   found them missing: the beam timer, beams cleared on load,
   `ANIMCOMPOSIT` with a stale work buffer, saved priorities clamped, the
   menu thumbnail's colours.
3. **Checked** by STDS9H's tests (all pass), new tests of what was
   rewritten, the engine test, and the game's own files
   ([TESTING.md](TESTING.md)).

## Where each part went

| STDS9H | Reluva |
|---|---|
| `engines/harbinger/game*.cpp`, `game.h`, `arcade.*` | `game/` |
| `engines/harbinger/archive`, `audio`, `dialogue`, `font`, `graphics`, `resources`, `script` | `formats/` |
| `engines/harbinger/viewer.*` | `pages/viewer.*` |
| `engines/harbinger/detection.*` | `detection.*` (the game's parts), `stharbinger.cpp` (the release table, the settings, the MetaEngine) |
| `engines/harbinger/harbinger.*` | `harbinger.*` (resources, modes), run by `stharbinger.cpp` |
| `common/system.h` (`OSystem`), `events` | `platform.*` (`Platform`, over Reluva's `System`) |
| `common/` (archives, DCL, files, streams, strings, INI) | Reluva's own (`src/common/`) |
| `graphics/surface`, `palette` | `src/common/draw.h` (`Surface`), `palette.h` |
| `audio/` (mixer, sound types, queuing stream) | Reluva's mixer (`src/common/audio/`) |
| `video/smacker.*` | `src/common/video/smacker.*`, for any engine |
| `backends/sdl/` | Reluva's SDL3 backend |
| `base/main.cpp`, `plugins` | Reluva's launcher, `reluva-tool` and the engine's settings ([below](#running-it)) |
| `test/` | `tests/engines/stharbinger/`, `tests/common/` ([below](#tests)) |
| `tools/*.py` | `reluva-tool stharbinger` ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)); the scripts themselves in [tools/stharbinger-re/](../../../tools/stharbinger-re/README.md) |
| `docs/FORMATS.md` | [formats/](formats/), a page per format |
| `docs/SCRIPT_REFERENCE.md` | [systems/script-reference.md](systems/script-reference.md) |
| `docs/KNOWN_DIFFERENCES.md`, `UNCONFIRMED.md`, `TESTING.md` | The pages of the same names |
| `docs/reverse-engineering/` | [research/](research/README.md), the notes as they were |

## What changed

### Running it

Each of STDS9H's command-line options and `stds9h.ini` settings is a
Reluva setting, which can be set for every game, for all Harbinger games or
for this one ([README.md](README.md#settings)):

| STDS9H | Reluva |
|---|---|
| `stds9h --path DISC1 --path DISC2` | **Add Game...** (both CDs side by side, or a CD and **Add other disc...** when importing); `reluva <entry>` |
| `--original`, `-o` | *Play exactly as the original* (`original=1`) |
| `--debug` | *Developer: debug keys* (`debug=1`): F2, F10, the detailed title bar |
| `--debug N` | *Developer: console output* (`debug_level=N`) |
| `--scene NAME` | `start_scene=NAME` in the game's section of `reluva.ini` |
| `--mode viewer`, `--mode video --scene DIR/NAME.SMK` | *Developer: start with* (`start_mode=viewer`, or `start_mode=video` with `start_scene=DIR/NAME.SMK`) |
| `--info` | `reluva-tool stharbinger info <game folder>`; `files` for the parts and files found |
| `--engine`, `--list-engines`, `--help` | Not needed: Reluva detects the engine; `reluva-tool` lists the engines and commands |
| Alt+Enter | The same |

`stds9h.ini`'s keys are Reluva's own keys, so its lines can be copied into
the game's section of `reluva.ini` (or `[engine:stharbinger]`) as they are,
with one exception:

| `stds9h.ini` | Reluva |
|---|---|
| `[audio] master_volume` | *Master volume* (`master_volume`), one of the settings every engine has |
| `[audio] music_volume`, `speech_volume`, `effects_volume` | The engine's volumes, the same keys and the same 0–200% |
| `[display] fullscreen`, `integer_scaling`, `smooth`, `vsync` | *Start in*, *Scaling*, *Pixels*, *Vertical sync*: the same keys and values |
| `[display] window_scale=0` (2× where it fits, else 1×) | `window_scale=auto`; unset, Reluva leaves the window as it is |
| `[display] window_scale=1`..`8` | The same (the launcher offers 1× to 4×) |

A size too big for the screen is reduced to the largest that fits, as in
STDS9H, without STDS9H's warning.

### Behaviour around the game

- **Detection.** STDS9H took any folder holding part of the game and said
  what was missing. Reluva offers a folder only when it holds both disc 1's
  scripts and the data, so disc 2 alone isn't offered; importing disc 1 asks
  for the other with **Add other disc...**. The game can't run without
  disc 1's scripts in either.
- **The developer settings.** STDS9H refused `--scene`, `--mode` and
  `--info` unless `--debug` was given. In Reluva they are settings marked
  *Developer:*, off unless set, as every engine's are, and `info` is a
  `reluva-tool` command.
- **Releases.** Reluva also recognises the release by its files' sizes and
  hashes ([README.md](README.md#releases)).

### Settings and saves

- **Where:** STDS9H kept `stds9h.ini`, `dlas.ds9` and `savegame/` in its
  own per-user folder (`~/.local/share/STDS9H/stds9h/harbinger/` on Linux);
  here the game's folder of Reluva's (`games/<entry>/`) holds `dlas.ds9`
  and `savegame/`, and the settings are in `reluva.ini`.
- **Saves:** the same format (the original's), so STDS9H's load once
  copied across ([README.md](README.md#saved-games)).

### Inside

- **Files:** Reluva reads each file whole into memory, where STDS9H streamed
  it; a video is read whole before it plays. If a file can't be read from
  one archive, STDS9H tried the next; Reluva reports it.
- **Sound:** STDS9H's mixer ran 0..255 with a "plain" sound type the game
  never used; Reluva's runs 0..256, and the original's levels (75 of 127 for
  speech, 64 for the action sequences' music) come out within 0.5%.
- **Typed names:** a save's name comes from the text the system types
  (Reluva's text input) while it is typed, where STDS9H read characters from
  key presses.
- **The script listings:** `reluva-tool stharbinger script` takes a
  talker's speaker code from the scene's `CCS` list as the game does;
  STDS9H's `harbinger_script.py` guessed it from the symbols' order
  ([UNCONFIRMED.md](UNCONFIRMED.md#settled)).

## Tests

| STDS9H | Reluva |
|---|---|
| `harbinger_tests.cpp`: archives, directive lists, name and xref lists, RLE, raw sounds, the disc layout, dialogue records, fonts and phonemes | `stharbinger.unit.formats`: the same 8 cases |
| `harbinger_tests.cpp`: the script interpreter, conversations, videos and cursors, action sequences, the menu with saving and loading, credits and the deck map, *original* mode, the special functions, `ANIMCOMPOSIT`, animation leftovers | `stharbinger.unit.game`: the same 10 cases, and *beam timing* (new) |
| `harbinger_tests.cpp`: `all_conversations` (with the game's files) | The game-files test's `conversations` step ([TESTING.md](TESTING.md)) |
| Detection and the real disc layout | `stharbinger.unit.detection`: rewritten, with importing and the release table |
| `video_tests.cpp` (Smacker, the queuing stream) | `common.unit` (`smacker`, the streams): the same synthetic videos, against FFmpeg's decoding |
| `common_tests.cpp` (paths, DCL, streams, surfaces, palettes, raw streams, the mixer, the INI file) | Left out: they tested STDS9H's common layer, now Reluva's, which has its own tests |
| — | `stharbinger.engine`, the basic engine test (new) |

## STDS9H's history

What each stage of STDS9H established, all of it now on the pages named.
Commit ids are STDS9H's.

| Date | Commits | What it established | Now in |
|---|---|---|---|
| 20 October | `1de95f3` | The archives (including the scripts inside disc 1's `SSE/SSE.ALL`), backgrounds, palettes, animations, sounds, scene scripts and name lists; a scene viewer; the Python tools | [formats/](formats/), [the viewer](README.md#developer-settings), [tools/stharbinger-re/](../../../tools/stharbinger-re/README.md) |
| 20 October | `e77e703` | Disc 1's real layout: an `ART` folder beside `DS9` and `SSE`; ISO 9660 `;1` suffixes | [formats/discs.md](formats/discs.md) |
| 20 October | `5067f32`, `bb38385` | The scene interpreter as DEEP.EXE runs it: 20 ms steps, timers, variables, hotspots, animations, sounds, fades; four commands misnamed in the first listings (`DEATH`, `CDCHANGE`, the fades the right way round) and `SOUND`'s keywords read from DEEP.EXE's tables | [systems/script-reference.md](systems/script-reference.md), [research/interpreter.md](research/interpreter.md) |
| 20 October | `ec1b81f`, `e2bbb3b` | Dialogue records and every dialogue command; speech and lip-sync; the reply menu's font, colours and placement | [systems/script-reference.md](systems/script-reference.md), [formats/scripts.md](formats/scripts.md), [research/dialogue.md](research/dialogue.md) |
| 20 October | `c42fe53`, `0813201` | A Smacker decoder, checked against FFmpeg on made-up videos; the video table and positions; the cursors in `art\mouse.ani`, and which point of the cursor is tested where | [formats/videos.md](formats/videos.md), [formats/cursors.md](formats/cursors.md) |
| 20 October | `f998953` | The action sequences: targets, fire, damage, torpedoes, music, results | [formats/action-sequences.md](formats/action-sequences.md), [research/arcade.md](research/arcade.md) |
| 20 October | `d9a1e77`, `cdff9d8`, `5c019da` | The game menu after `Game_Setup`; saves in the original's format, field by field; autosaves; the original's loader bugs not reproduced | [formats/saves.md](formats/saves.md), [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#bugs-fixed) |
| 20 October | `0276ceb` | The credits, read from DEEP.EXE at run time; the deck map | [formats/deck-maps.md](formats/deck-maps.md), [formats/executable.md](formats/executable.md) |
| 20 October | `8cb029d`, `e6e9fc4` | The debugging options kept apart from the game; the added conveniences, and turning them off | [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#added-conveniences-turned-off-by-play-exactly-as-the-original), [above](#running-it) |
| 20 October | `3ac9db8` | QUARK020's talker animation 4095 means none; Quark's speaker code is `qrk` (the `ras` was a listing's mistake) | [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#bugs-fixed), [UNCONFIRMED.md](UNCONFIRMED.md#settled) |
| 20 October | `ce4a597` | The rest of `ACTIONSEQ`: the phaser, enemy shots, the countdown and its table | [research/special.md](research/special.md), [systems/script-reference.md](systems/script-reference.md) |
| 20 October | `4caa43c`, `fc64660` | The original's volumes (speech 75 of 127, the action sequences' music 64); volume settings by sound type | [UNCONFIRMED.md](UNCONFIRMED.md#guessed), [README.md](README.md#settings) |
| 20–21 October | `1c6847d`, `ca3def1`, `f3df4d7` | `ANIMCOMPOSIT` from the screen; the death static; the persistent work buffer and its leftovers; the lip-sync erase rule | [systems/script-reference.md](systems/script-reference.md), [research/interpreter.md](research/interpreter.md) |
| 21 October | `38e0393` | The menu keeping its own palette | [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#added-conveniences-turned-off-by-play-exactly-as-the-original) |
| 28 October | `c75976f`, `997a231` | The known differences, unconfirmed items and testing pages; the research notes | The pages of the same names, [research/](research/README.md) |
| 28 October | `6b73f71`, `71d1f5c`, `405f3a3` | Display settings, now Reluva's own | [above](#running-it) |
| 28–29 October | `a7ed84a`, `41ea318`, `27d5486` | A review's fixes: the beam timer (counted down by every render), beams cleared on load, `ANIMCOMPOSIT` with a stale buffer, saved priorities clamped | [research/special.md](research/special.md), [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#bugs-fixed) |

The work checked on the real discs with STDS9H is in
[TESTING.md](TESTING.md#last-checked).

## Status

- [x] The game's code, as of STDS9H's last commit
- [x] Detection, importing with the CDs, settings, saves
- [x] `reluva-tool` commands, and STDS9H's Python tools kept
- [x] STDS9H's tests; the engine test; the game-files test
- [x] Docs, and STDS9H's history on this page
- [ ] Played on the discs in Reluva ([TESTING.md](TESTING.md#still-to-check))
