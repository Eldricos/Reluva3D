# Star Trek: Deep Space Nine - Harbinger: testing

## Automated checks

### Unit tests

`reluva-tests stharbinger` (and `ctest -L stharbinger`) runs, on data built
in code, never the game's own:

| Suite | What it checks |
|---|---|
| `stharbinger.unit.formats` | Archives, scripts and name lists, RLE frames, raw sound detection, a disc's folders, dialogue records, fonts and lip-sync |
| `stharbinger.unit.game` | The script interpreter, conversations, videos and cursors, the action sequences, the game menu with saving and loading, the credits and deck map, playing as the original, special functions, animations |
| `stharbinger.unit.detection` | Finding the game on a CD, both CDs, an install; CD names with `;1`; importing both CDs |

The Smacker decoder, DCL and the mixer's streams are tested with the shared
code (`common.unit.video`, `.archives`, `.audio`).

### The basic engine test

Run by the same `reluva-tests stharbinger`, on data built in code:

| Suite | What it checks |
|---|---|
| `stharbinger.engine` | The whole engine, from two CDs in miniature: found and imported as the launcher does, then played (the opening video with its sound, a scene, a click to another), recorded in `tests/engines/stharbinger/data/engine-record.txt` |

### The game-files test

`RELUVA_STHARBINGER=/path/to/the/game ctest --test-dir build -L gamedata`
runs the game-files test (`tests/engines/stharbinger/gamedata.cmake`): the
game's parts and files, what its data holds, and every conversation played
through three times. The path is anything the launcher takes: a CD, both CDs
side by side, an install, or Reluva's import of them.

The records are in `tests/engines/stharbinger/data/gamedata/<release>/`, one
file per step: `both-discs/`, made from the English release with both CDs, on
Linux. That run played 210 conversations (70 in each of three runs) through
903 reply menus with none stuck, read the 269 lines of credits from
`DEEP.EXE`, and listed every scene and dialogue script (5,941 lines of
dialogue). There are none yet for disc 1 alone (`disc-1/`) or another
release: make them with `RELUVA_GAMEDATA_UPDATE=1`
(see [DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests)).

On Windows the `conversations` step can differ from the Linux record with
nothing broken: it picks replies with the C library's `rand()`, whose numbers
differ between systems. Keep a baseline of your own there with
`RELUVA_GAMEDATA_RECORDS=<a folder>` (and `RELUVA_GAMEDATA_UPDATE=1` the first
time).

### What the automated checks don't cover

Everything under *Still to check* below.

## A playthrough by hand

### Still to check

#### A full playthrough

* Every scene and conversation in order on both discs, with the variables as
  the game sets them (not with the developer start scene, which starts with all
  variables 0).
* Every action sequence, including losing each one and its death static.
* Every death in the scenes, and the menu that follows (Save and Resume do
  nothing while dead).
* The ending and the credits that follow it.

#### Discs

* Moving on to disc 2 mid-game with both discs in the game's folder.
* Starting with only disc 1 and reaching disc 2 content (files are
  missing instead of being asked for; see [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)).

#### Saved games

* Loading saves made at different points: in a scene, in a conversation,
  inside a subroutine call, and the autosaves made before action sequences.
* Saves made by the original game loading here, and saves made here loading in
  the original.

#### Rendering

* Scenes with moving or shape-changing animations, compared with the original
  for the leftovers they leave.
* A scene change that does not load a new background (the old picture should
  stay until the next `BACKGRND`).
* Long conversations with several talking heads: no mouths piling up.
* Phaser and enemy beams: each shot a single bolt (not a smear of several),
  shown for about three frames and leaving no marks, including when a shot is
  followed straight away by `BACKGRND`.
* `ANIMDUMP` on a visible animation: its last frame stays until something draws
  over it, as in the original.

#### Unconfirmed details

* The items in [UNCONFIRMED.md](UNCONFIRMED.md) that can be seen in the game, especially Quark's
  lip-sync in QUARK010, QUARK011 and XOQ010.

#### Conveniences

* Everything listed in [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) with and
  without *Play exactly as the original*.
* Reluva's display settings: full screen, Alt+Enter, each window size,
  whole-multiple scaling and smoothing, and the mouse over the borders in
  full screen.

#### In Reluva

* Adding the game from a CD, from both CDs side by side and from an install,
  and importing it from one CD with the other added.
* Saved games in `games/<entry>/savegame/`, and saves from the original
  copied there.
* The settings at each level (every game, all Harbinger games, the game).

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 20 October 2026 | English (DOS), both CDs | Linux, STDS9H's extractor | Every archive unpacked and every animation, sound, background and deck map converted (each format page's **Checked on**) |
| 20–21 October 2026 | The same copy, both CDs | Linux, STDS9H | Played: the list [below](#checked-on-the-real-discs-with-stds9h-before-the-port) |
| 28 October 2026 | The same copy | Linux, STDS9H | The display options (now Reluva's own settings) |
| 12 November 2026 | English (DOS), disc 1's `SSE/SSE.ALL` | Linux, Reluva's detection | The release table's entry: `SSE.ALL`'s size and hash match, and so do `SSE.LST`'s and `SSE.CAT`'s, rebuilt from `SSE.ALL`'s 76 entries; the matcher finds the release with them and none with one byte changed ([README.md](README.md#releases)) |
| 12 November 2026 | The same copy's scripts (disc 1's `SSE` folder) and `DEEP.EXE` | Linux, `reluva-tool stharbinger` | Every scene and dialogue script read and its arguments named; the executable's objects, fixups and strings read |

### Checked on the real discs (with STDS9H, before the port)

The engine was ported from STDS9H with its game logic unchanged, and STDS9H's
tests pass on it. What follows was checked on the real discs with STDS9H;
the port itself has not been played on the discs yet, so all of it needs
checking again in Reluva, together with what's under *Still to check*.

* Disc detection, the opening and the intro videos.
* Scene scripts, hotspots and cursors.
* Conversations: speech, lip-sync and reply menus.
* Action sequences, the drone fights and the countdown.
* The game menu: new game, save, load, delete, difficulty, autosave.
* The credits and the deck map.
* `ANIMCOMPOSIT` and the death static.
* Animation leftovers after the persistent work buffer change, and the
  lip-sync redraw fix (the conversation where the player passes out in
  QUARK010 / QUARK011).
* The menu keeping its own colours, and the volume options.
