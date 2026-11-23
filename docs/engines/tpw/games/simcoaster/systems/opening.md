# SimCoaster: the opening

> **Status:** partial · **Code:** `src/engines/tpw/ui/opening.cpp` · **Checked against:** a recording · **Confidence:** seen, not traced unless marked

## Overview

What the player sees from starting the game to its front end: the EA
GAMES logo, the Bullfrog logo, then the front end's 3D opening in the
game's boardroom.

## How it works

From a recording of the US release
([research/footage.md](../research/footage.md#the-opening-second-by-second)):

### The EA GAMES logo

From 3.73 s to 7.22 s: the EA GAMES movie (`Data/movies/bfSim.tgq`, or
`bf.tgq`, the same file; [tgq.md](../formats/tgq.md)), its ring turning
on light grey, with its sound, filling the 4:3 screen. Its pictures, 320
× 480, are shown at the screen's width *(inferred)*.

The recording cuts away from it at about its 45th to 50th frame of 81,
about when its sound (3.24 s) ends.

### The Bullfrog splash

From 7.22 s to 7.83 s: `Data/Init/<width>/Splash_<language>.png`
(`Splash_American.png` for the US release, a picture the screen's own
size, 640 × 480 in `Init/640/`) as it is: the Bullfrog logo, black on
white. Only 0.6 s, perhaps cut short by a click.

### The front end

From 7.83 s, the boardroom drawn by the engine
([research/footage.md](../research/footage.md#the-front-ends-opening)):
not done in Reluva yet. After it, when a park loads, the loading screen
([loading-screen.md](loading-screen.md)).

## In Reluva

`playOpening` (`src/engines/tpw/ui/opening.cpp`): the movie, through
the shared player (`video::playMovie`), on a 640 × 480 screen, stretched
to fill it, with its sound; then the splash for the language
(`languageOf`, `src/engines/tpw/game_files.cpp`) at 640 × 480. A key or a
click skips either, as the shared player and the splash let it. The
engine runs it first, then shows the loading screen
([loading-screen.md](loading-screen.md)).

Done differently, for now (also in
[KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#not-yet-as-the-original)):

- the movie plays to its last frame, 5.4 s, its last 2 s without sound;
- its colours are BT.601's, so its white is brighter than the game's;
- the splash stays 3 seconds, or until a key or a click *(guessed)*;
- the screen is 640 × 480, the splash's `Init/640/` picture.

## Sources and checks

- **Where the facts come from:** the recording, frame by frame, and the
  game's files set beside its frames: the movie by its pictures, the
  splash by its picture, pixel for pixel in the recording (its white is
  255 there).
- **How it was checked:** `tpw.unit.ui` plays an opening built in the
  test headless: the movie, then the splash held 3 seconds. On the US
  release, `reluva-tool tpw opening-run` shows 83 screens (a black one,
  the movie's 81 frames, the splash) and ends at 8.3 s
  (`tpw.gamedata.opening`).

## Open questions

- **How long the movie plays.** The recording leaves it at about frame
  45-50 of 81, near the end of its 3.24 s of sound: the game may stop it
  with its sound, or the player clicked.
- **The movie's colours.** In the recording its background is about
  (218, 215, 219), where BT.601's conversion of its luma (233) gives 253;
  a fit over a frame isn't clean enough to give the game's formula. The
  splash's white is 255 in the recording, so the recording's levels are
  true.
- **How long the splash stays**, left alone.
- **Which screen size the game picks**, and so which `Init/<width>/`
  picture it shows.
