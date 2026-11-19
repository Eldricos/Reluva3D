# Murder on the Orient Express: testing

How Reluva's AWE engine is checked: the automated checks, what they
cover and don't, and a list of things to try in a playthrough by hand.

## Automated checks

### Unit tests

`ctest --test-dir build` runs the engine's suites (`tests/engines/awe/unit/`,
`reluva-tests awe` alone) among the others:
the text tables, scenes, Granny files, Oodle1, the dialogue manager
(choices, picture choices, the wheel, talk starts), lighting and so on,
on small made-up data. They need no game files and run in CI.

### The basic engine test: no game files

`awe.engine` (`tests/engines/awe/engine.cpp`) makes a small install of its
own and plays it through the engine as the launcher starts it, then
through the data viewer as reluva-viewer starts it, so a change
anywhere (in `src/common/`, say) that alters what the engine does is caught.
CI runs it on Linux and Windows with the rest.

- **The install:** stand-ins for `motoe.exe` and `granny2.dll` (found by
  the fallback detection, which looks for the files whatever their
  contents), `Text/itemlist.txt`, an `acts.txt` of one act, the main
  menu's scene with its Play hotspot, and two scenes of a place of its own
  (`Locations/Test`), with pictures made in the test.
- **The play:** the main menu the game starts at (in the viewer, after
  N in its Scenes page); Play (`act 01a`); the act's place loaded and its
  first scene; a click on its hotspot, which sets a tag and goes to the
  next scene; a quick save (F2); back to the launcher.
- **The record** (`tests/engines/awe/data/engine-record.txt`): what's
  detected, then for the game (`run`) and the viewer (`viewer`) every
  screen shown (a checksum), the window's titles, and the
  saved game's place, scene and tags (read back, as its bytes hold the date).
- **When it fails:** it prints each line, with the old value beside any
  that changed. If the change is meant, `reluva-tests awe.engine --update`,
  review the record's diff, and commit it with the change.

### The game-files test: with your own copy of the game

Point `RELUVA_MOTOE` at the game's folder (the one holding `motoe.exe`) and
run the steps labelled `gamedata` (`tests/engines/awe/gamedata.cmake`):

    RELUVA_MOTOE=/path/to/Murder\ on\ the\ Orient\ Express ctest --test-dir build -L gamedata -R awe

On Windows: `set RELUVA_MOTOE=C:\Games\Orient Express`, then
`ctest --test-dir build -C Release -L gamedata -R awe`. Without the variable
the steps show as skipped.

| Step | What it runs |
|---|---|
| `awe.gamedata.release` | What the game is (`detect`) |
| `awe.gamedata.files` | Every Granny file, scene, text, movie and sound read; the cameras and compressed curves compared with `granny2.dll`'s (`awe files-check`) |
| `awe.gamedata.scripts` | Every script's commands counted, and any verb Reluva doesn't know (`awe script-check`) |
| `awe.gamedata.route-search` | A way from a new game to the credits, searched for once (`awe game-run -solve`) |
| `awe.gamedata.route` | The whole game by it, four ways, as below (`awe game-check -quick`) |

The search runs the first time only, and keeps the route in the build
(`gamedata/awe/route.txt`), as it names the game's files; the record holds
its checksum, so a search that finds another route shows. It has a step of
its own, with two hours to run, as it can take more than the hour each
step is given. Each step's
output is kept in the build (`gamedata/awe/<step>/`), the previous run's
beside it, and its checksum and summary line go in the record,
`tests/engines/awe/data/gamedata/english-1.0/<step>.txt` (made on Linux;
another release's go in a folder of their own, with
`RELUVA_GAMEDATA_UPDATE=1`). See [DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests)
for the shared helpers.

**A baseline of your own in `reluva-viewer`'s Tests window**
([DEVELOPMENT.md](../../../../DEVELOPMENT.md#the-tests-window)) is kept in a folder
per engine, this engine's in `<the window's folder>/awe/`
(`RELUVA_GAMEDATA_RECORDS_ROOT`). One made from the command line with
`RELUVA_GAMEDATA_RECORDS` isn't found there: make it again in the window,
with **Make my baseline**, before making the changes it's to check.

### The whole game: `reluva-tool awe game-check`

    reluva-tool awe game-check <game folder> <route.txt> [-3d] [-quick]

Plays the whole game four ways, each from a new game, as `reluva` runs it
(the data viewer's scene browser off: none of its keys or overlays), and
ends with a summary (exit code 1 if anything failed):

1. **The route by the mouse, with sound.** Every click and use is made
   at a point where the game's pick finds the object; voices and sounds
   take their own time. With `-3d`, the real renderer on a display.
2. **By the player's hand alone.** Every action by mouse and keys as a
   player makes them: items taken up from the inventory window, the bar
   brought down to travel, parts put in the combine boxes, choices
   clicked in the box, lines clicked through, movies skipped by Escape.
3. **Saved and loaded after every action,** played on from the loaded
   game; the story state (act, scene, tags, items, conversations, scenes
   seen) must come back the same.
4. **Wandering off the route.** Every 10 actions, up to 30 random actions
   (one in eight an item used where it isn't wanted), with saves and
   loads at random, then back to the route. Anything stuck, failed, lost
   on loading, not done or with nothing to do is listed.

Each passes when the end credits are reached and nothing is listed. The
route file comes from `awe game-run -solve`. It isn't in the repository, as
it names the game's files; `awe game-check` searches for one if the file
isn't there and keeps it. On the English install the quick run takes
about ten minutes. The options are described in
[systems/acts.md](systems/acts.md).

### Frames to look at

`awe game-run -play <route> -mouse -3d -shots <dir>` saves the screen before
every action (1,105 for the route); `-shot <n>c<k>` before a given
dialogue choice. These were set side by side with the walkthrough
recordings to check how scenes look (see
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)).

### Frame time

With the environment variable `RELUVA_PERF` set, Reluva writes to its
console, every three seconds, the scene, the frames a second and where
a frame's time goes (2D, characters, present); see
[../../README.md](README.md#frame-time). Set it where Reluva is started:
`set RELUVA_PERF=1` in a Command Prompt, `$env:RELUVA_PERF = "1"` in
PowerShell (`set` there makes a PowerShell variable the program never
sees), `RELUVA_PERF=1 ./reluva` on Linux and macOS, or an `env` entry in
Visual Studio's `launch.vs.json` (see [../../../../DEVELOPMENT.md](../../../../DEVELOPMENT.md)).

`awe game-run -play <route> -mouse -3d -draw` plays the route in a window
drawing every frame, so two builds can be timed on the same scenes, and
their `-shot` frames compared byte for byte.

### What the automated checks don't cover

- The real window: the operating system's events, the audio device, a
  real graphics card, the frame rate. The checks drive the game page's
  own event handling, on a virtual display at most.
- Whether the original game would answer an off-route action the same
  way: wandering only checks that nothing breaks.
- Whether the game can still be finished after wandering: the checks go
  back to the route.
- Releases other than English 1.0.

## A playthrough by hand

Things to try in a real window, on real hardware, that the checks don't
or can't. Note the act and scene for anything that looks or sounds
wrong, and if possible compare with the same moment of a recording.

### Throughout

- [ ] The window: the picture keeps its shape at your window or screen
      size.
- [ ] Sound: music, effects and voices play, and their volume sliders
      work; nothing crackles or stops.
- [ ] Frame rate: walking, the moving train's windows, snow and steam
      stay smooth; fades take about a third of a second. A scene below
      your screen's refresh rate: its `RELUVA_PERF` lines (above).
- [ ] Walking: characters walk from their first step, turning on the
      spot first where they face away; none slides along mid-gesture
      (Hardman in the market's fountain talk does in the original too:
      he has no walk).
- [ ] The cursor: the right verb over hotspots, doors, characters and
      items; the item in hand as the cursor; the top bar comes down at
      the top of the screen.
- [ ] Right-click opens and closes the inventory, or puts the item in
      hand back.
- [ ] The mouse wheel and middle button in a conversation's choices.
- [ ] Escape skips movies (not the legal notice); F2 quick-saves, F3
      quick-loads.

### Options

- [ ] Each option changes at once and is kept after quitting: volumes,
      brightness, captions (lines and movie subtitles go, choices stay),
      3D shadows, animated effects, fog, particles, anti-aliasing.

### Saving and loading

- [ ] Save in each act, in a close-up, and try mid-conversation, then
      load: the scene, items, characters' places and the music come back.
- [ ] Overwrite a save; the thumbnail and name show in the slot.
- [ ] Load a save made by the original game, if you have one.
- [ ] Leave a game for the main menu and press Play: a new game, the
      first conversation and the market's characters as at the start.

### Off the route

- [ ] Use items on the wrong things and people: Antoinette's "that won't
      work" lines.
- [ ] Combine the wrong items; take combined items apart.
- [ ] Give wrong answers in act 8's quizzes, and pick "I need a moment
      to decide".
- [ ] Walk away from conversations and come back; talk to someone twice
      in a visit and on a later visit (the greeting changes).
- [ ] Travel between cars by the top bar where it's allowed; the bar
      refuses where it isn't (act 1 to 3, the forest, act 8).
- [ ] In the forest, walk without and with the snowshoes; the wolf;
      the hut's window.

### Moments worth a close look

- [ ] Act 1, Sirkeci: the market's crowd and the cleaver puzzle.
- [ ] Act 4: Ratchett's body on the bed, the gun close-up, the scarlet
      kimono in the corridor.
- [ ] Act 5: the puzzle box (closed on the carpet, opening), the torn
      letter, the ashes.
- [ ] Act 6: the forest and the shepherd's hut, the engine's cab, the
      baggage car's safe.
- [ ] Act 7: Barnaby's radio, the fingerprint cards under the magnifier.
- [ ] Act 8: the evidence choices with their pictures, the three
      solutions, the coda and the credits.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 6 November 2026 | English 1.0 | Linux | The game-files test, every step passed and its records made ([above](#the-game-files-test-with-your-own-copy-of-the-game)): the files read, the scripts' 9,797 commands, the route searched twice (the same route both times) and played the four ways |
| 6 November 2026 | English 1.0 | Linux | The movies by Reluva's one Bink decoder against the game's `binkw32.dll`, all 87 files (`awe bink-check`, [systems/movies.md](systems/movies.md)); the path-finder against the game's code, every scene with a walk grid (`awe path-check -all`) |
| 28 October 2026 | English 1.0 | Linux, a virtual display | The route played by the mouse with the real renderer, a frame before every action, compared with the walkthrough recordings ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#checking-against-the-recordings)) |

The playthrough by hand above hasn't been recorded here yet.
