# LEGO Alpha Team: the playthrough checklist

What to try when playing LEGO Alpha Team in Reluva, and how to report
what goes wrong. It covers what automated runs can't: playing by hand.
How the automated checks work is at the end.

## Reporting a problem

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

## Starting

- [ ] The intro movie plays (with `lmiintro.avi` copied into the install
      folder), with sound, and a key or click skips it.
- [ ] The logos, then the attract sequence (example levels with scripted
      cameras), then "Click the mouse to begin!".
- [ ] Alpha Team Headquarters: every button answers.

## Menus and options

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

## In a mission

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

## The missions

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

## In Reluva, around the game

- [ ] The window's title shows the frames a second.
- [ ] F12 opens the data viewer and pauses the game; Esc goes back.
- [ ] Closing the window or Ctrl+F5 while playing asks first.
- [ ] Alt+Tab away and back while playing.

## The automated checks

Run by `reluva-tool` (in `src/tools/`), headless and with a fixed clock
(time follows the instructions run), so each run repeats exactly:

- **Routes:** `at-run` with `-click`/`-point` scripted input plays
  Mission One from a save, at 640x480x16 and 1024x768x32, saving a frame
  every 300 (`-shots`, `-every`). Every change to the interpreter, the
  JIT or the drawing is checked by those frames staying the same to the
  byte, and the instruction count staying the same.
- **Sweeps:** `at-sweep` runs all 54 missions past Go to frame 3700;
  `at-sweep -solved` runs every Adventure mission with its solved layout
  until it moves on (50 of 51 do; the last goes to the ending).
- **Unit tests** (`reluva_tests`): the x86 instructions with the
  decoded-instruction cache and the JIT on and off, 3000 random
  instruction sequences compared between them, faults from native code,
  DirectX, A3D, input, MSVCRT and MSVCP60.
- **Recordings of the original** compared by eye
  ([unconfirmed.md](unconfirmed.md)).
