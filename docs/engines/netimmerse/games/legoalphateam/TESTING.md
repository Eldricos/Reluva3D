# LEGO Alpha Team: testing

## Automated checks

The engine's tests are laid out as every engine's are
([../../../../DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests)): unit tests and
a basic engine test that need no game files, and a game-files test run on
your own install.

Run by `ctest` (and CI) with everything else, or alone with
`reluva-tests netimmerse`:

### Unit tests

- **`netimmerse.unit.formats`** (`tests/engines/netimmerse/unit/formats.cpp`):
  the `.pac`, `.DDS`, text, `.FIN` and `.DDF` readers, on files the test
  builds as the format documents describe them
  (`tests/engines/netimmerse/fixtures.h`).
- What the game runs on is tested with the shared code:
  `common.unit.jit` (the x86 instructions with the decoded-instruction
  cache and the JIT on and off, 3000 random instruction sequences compared
  between them, faults from native code), `common.unit.windows` (threads,
  MSVCRT and MSVCP60, files, windows and the registry, DirectX, input,
  A3D) and `common.unit.indeo` (the intro movie's video).

### The basic engine test

- **`netimmerse.engine`** (`tests/engines/netimmerse/engine.cpp`), the
  basic engine test. It makes a small install of its own: `.pac` archives
  holding two models, a texture, a font with its page, a sound, a puzzle
  and the locale's text. It checks that an install is recognised, and
  that the game, with no `LEGOATeam.exe`, returns an error saying so. Then
  it opens the data viewer on it as `reluva-viewer` does: every page in
  turn, stepping through what each shows, showing a model's hidden parts,
  playing the sound, then Esc. What
  happens (the screens, the 3D renderer's textures, meshes and draws, the
  sound, the window's titles) goes into a record,
  `tests/engines/netimmerse/data/engine-record.txt`. If a change is meant
  to alter it, run `reluva-tests netimmerse.engine --update` and review the
  record's diff.

### The game-files test

`tests/engines/netimmerse/gamedata.cmake`, run by `ctest` when
`RELUVA_ALPHATEAM` names your install (the folder holding `LEGOATeam.exe`
and the `.pac` files):

```sh
RELUVA_ALPHATEAM="/path/to/LEGO Alpha Team" ctest --test-dir build -L gamedata -R netimmerse
```

On Windows: `set RELUVA_ALPHATEAM=C:\Program Files\LEGO Media\LEGO Alpha Team`
(your install's folder), then
`ctest --test-dir build -C Release -L gamedata -R netimmerse`. Without the
variable the steps show as skipped. Nothing is written to the install:
what the game saves goes into the build folder.

| Step | What it runs | Time |
|---|---|---|
| `release` | What the game is (`reluva-tool detect`) | a second |
| `archives` | Every `.pac` archive listed (`pac-list`) | a second |
| `models` | Every `.FIN` model read, its images found (`fin-check`) | seconds |
| `start` | The game's own code from a new install's save to frame 300: the logos and the attract sequence, a frame kept every 100 | about 15 s |
| `mission-one` | Mission One (M1010) at 640x480x16: from an Adventure saved there, through the menus and its fly-over, Go with the parts where its solved layout has them, to frame 3300 (its solution running), a frame kept every 300 | about a minute |
| `mission-one-1024` | The same at 1024x768x32 | about 2 minutes |

The runs are `reluva-tool netimmerse at-run` with `-fixed-clock`: the
game's clock follows the instructions run, and the input is scripted
(`-click`, `-point`), so each run repeats exactly. A step's record (in
`tests/engines/netimmerse/data/gamedata/english/`) has a checksum of each
command's output, the instructions each run took, and a checksum of each
frame kept. A step passes when its record is the one committed.

- **When a step fails**, it prints the recorded and the new lines. Each
  command's output and frames are kept in the build folder, under
  `gamedata/netimmerse/<step>/`, with the previous run's output beside
  it. If the change is meant, run again with `RELUVA_GAMEDATA_UPDATE=1`
  and review the records' diff.
- **The routes are the reference** for changes to the emulator, the JIT
  and the drawing: those must leave `mission-one` and `mission-one-1024`
  as recorded (frames to the byte, the same instruction count) unless
  the change is meant to alter what the game does.
- **On Windows and macOS** the records may differ with nothing broken:
  the game's code calls the x87's `sin`, `cos` and the like, which Reluva
  works out with the system's maths library, and the last bit of those
  can differ between systems. The committed records were made on Linux.
  Keep a baseline of your own before making changes:

  ```sh
  RELUVA_ALPHATEAM=... RELUVA_GAMEDATA_RECORDS=~/at-records RELUVA_GAMEDATA_UPDATE=1 ctest -L gamedata -R netimmerse
  ```

  and afterwards run with `RELUVA_GAMEDATA_RECORDS=~/at-records` (and
  without `UPDATE`) to compare against it.

### What the automated checks don't cover

What only playing can check is in
[A playthrough by hand](#a-playthrough-by-hand), below. Two more checks
are run by hand:

#### By hand, with `reluva-tool`

- **Sweeps:** `reluva-tool netimmerse at-sweep <game> <out>` runs all 54
  missions past Go to frame 3700; with `-solved`, every Adventure mission
  with its solved layout until it moves on (50 of 51 do; the last goes to
  the ending). Each mission's log and save are in `<out>/<mission>/`.
- **Recordings of the original** compared by eye
  ([UNCONFIRMED.md](UNCONFIRMED.md)).

## A playthrough by hand

What to try when playing LEGO Alpha Team in Reluva, and how to report
what goes wrong. It covers what automated runs can't: playing by hand.
How the automated checks work is above, under "Automated checks".

### Reporting a problem

For each one, give:

- the mission (its id, below, or its name as the game shows it) and the
  screen;
- what you did, what happened, and what you expected;
- a screenshot or a short recording if it shows on screen;
- `Game.txt` from the install folder if it depends on how far the
  Adventure has got, so it can be run again from the same save;
- the resolution and colour depth (Options), and whether the JIT was off
  (`RELUVA_NO_JIT=1`);
- `reluva.log` (on Windows in `%APPDATA%\reluva\reluva\`; the run
  before's is `reluva-previous.log`), which has every message Reluva and
  the engine printed.

### Starting

- [ ] The intro movie plays (with `lmiintro.avi` copied into the install
      folder), with sound, and a key or click skips it.
- [ ] The logos, then the attract sequence (example levels with scripted
      cameras), then "Click the mouse to begin!".
- [ ] Alpha Team Headquarters: every button answers.

### Menus and options

- [ ] New Adventure, and the Adventure cards: the mission and the date
      shown.
- [ ] Single Missions: only missions an Adventure has reached are
      offered.
- [ ] Options: each screen resolution and colour depth (640x480 to
      1024x768, 16 and 32 bits); the picture fits the window after each.
- [ ] Options: brightness and gamma visibly change the picture.
- [ ] Options: music, voice-over and sound effects volumes each change
      only their own sounds.
- [ ] Options: mouse sensitivity, and the advanced options (textures,
      lightmaps, backdrop, environment animation, shadows) each change
      what they say.
- [ ] Exit Game, then start Reluva again: the Adventure is where it was,
      and the options are kept.

### In a mission

- [ ] The fly-over and the goal text at the start; the panel.
- [ ] Camera buttons: next view, and the rotate buttons; the view
      changes as expected.
- [ ] Picking parts from the panel, placing them, rotating them (the
      menu on a placed part), taking them back.
- [ ] The hint menu: using a hint, and the counter going down.
- [ ] Go: the crew moves; parts act as they should.
- [ ] Failing: "Team Member Down! Try Again / Return to HQ".
- [ ] Solving: "We did it!" with Next Mission, Instant Replay! and Return
      to HQ; Instant Replay plays the run again.
- [ ] The mission menu: Keep Playing, Start Over, Options, Return to HQ,
      Exit Game (and Skip Training Mission in training).
- [ ] Sounds: music, voices, the parts' and the crew's sounds, placed to
      the left and right as the camera turns.

### The missions

Tick each one played start to finish by hand. The Adventure brings in
the three later training missions on its way: M0020 before M2100, M0030
before M3010, M0040 before M3060.

**Ogel Island:**
- [ ] M1010 (played by hand already) · [ ] M1020 · [ ] M1030 · [ ] M1040
- [ ] M1050 · [ ] M1060 · [ ] M1065 · [ ] M1070 · [ ] M1080
- [ ] M1085 · [ ] M1090 · [ ] M1095 · [ ] M1100 · [ ] M1110
- [ ] M1120 · [ ] M1130 · [ ] M1135 · [ ] M1140

**Goo Caverns:**
- [ ] M2010 · [ ] M2020 · [ ] M2030 · [ ] M2040 · [ ] M2050
- [ ] M2060 · [ ] M2070 · [ ] M2080 · [ ] M2090 · [ ] M2100
- [ ] M2110 · [ ] M2120 · [ ] M2130 · [ ] M2140 · [ ] M2150
- [ ] M2160

**Deep Sea Orb Factory:**
- [ ] M3010 · [ ] M3020 · [ ] M3030 · [ ] M3040 · [ ] M3050
- [ ] M3060 · [ ] M3070 · [ ] M3080 · [ ] M3090 · [ ] M3100

**Arctic Command Base:**
- [ ] M4010 · [ ] M4020 · [ ] M4030 · [ ] M4040 · [ ] M4050
- [ ] M4060 · [ ] M4070 (the last: the ending, "The Victory Party" and
      the credits)

**Training:**
- [ ] M0010 (Alpha Team Training Area, at the Adventure's start) ·
      [ ] M0020 · [ ] M0030 · [ ] M0040

### In Reluva, around the game

- [ ] The window's title shows the frames a second.
- [ ] `reluva-viewer` opens the data viewer on the game's folder; Esc goes
      back to its list.
- [ ] Closing the window or Ctrl+F5 while playing asks first.
- [ ] Alt+Tab away and back while playing.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail CD, installed | Windows (Visual Studio 2022) | `reluva-tests` all pass; the game-files test passes against a baseline of its own (`RELUVA_GAMEDATA_RECORDS`) |
| 2026-11-07 | English retail CD, installed | Linux (GCC) | All tests pass; the game-files test's records made, and its six steps repeat exactly; M1010, M1020, M2010 and M3010 solved by `at-sweep -solved` |
| Not recorded | English retail CD, installed | Windows | Mission One played through by hand ([The missions](#the-missions)) |
| Not recorded | English retail CD, installed | Linux | Every mission run past Go, and every Adventure mission solved by its solved layout, by `at-sweep` |
