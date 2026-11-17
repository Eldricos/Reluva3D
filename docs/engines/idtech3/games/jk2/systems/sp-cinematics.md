# Jedi Outcast: single player's cinematics and opening crawl

> **Status:** partial · **Code:** `src/engines/idtech3/client/sp_play_page.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; the first level's opening played in Reluva · **Confidence:** from the code unless marked

## Overview

Single player's movies are RoQ files in `video/` (the paks hold them),
played by `jk2sp.exe` when the game or the console asks: `cinematic
<name>` ends the game and plays one full screen, `ingamecinematic
<name>` plays one over a level the game waits under. The game asks
through its scripts (`SET_VIDEO_PLAY`), sending the console command.
The opening of the first level, `kejim_post`, is two: the opening text
crawl (`jk0101_sw`) and the approach to Kejim (`jk0101`).

## How it works

### Playing one (0x404920)

`CL_PlayCinematic(name, how, in game)`:

1. `timescale` is set to 1 and `skippingCinematic` to 0.
2. The file is `video/<name>` (the name as given if it has a slash),
   `.roq` added if it has no type; if there's no such file, nothing is
   played ("PlayCinematic(): Failed to open").
3. If the client isn't in the level yet, the name is kept (0x7ec010)
   and played once it is (0x404c60).
4. Its flags: a cinematic out of a level is the system's (1); `how`
   "1" (or `end.roq`) holds its last frame (4), "2" loops it (2).
5. For `video/jk0101_sw.roq`, the crawl: the picture
   `menu/video/tc_<sp_language>` registered and kept with it, its last
   frame held, and `music/cinematic_1` its sound. For `jk05` and `jk06`
   in a language other than English, their speech is a separate track
   (`sound/chars/video/cinematic_5`, `_6`) and the movie's own silent.
6. Played at 640 x 480; one in a level sets `cl_paused` to 1, which stops
   the server's frames (0x437d40) until it ends.

`ingamecinematic` (0x404c20) plays it in the level (state 7) at once, or
keeps it for when the level starts.

### The opening crawl (0x404cc0)

Drawn over the held movie from 9 seconds after it starts, as a 3D
scene with no world (`RDF_NOWORLDMODEL`):

- **The view:** x 0, y -50, as wide as the screen and twice its height,
  130 degrees across and up, the eye at the origin looking along x.
- **The polygon:** four corners, (90, -250, 0) and (90, 250, 0) near,
  (715, 250, 1100) and (715, -250, 1100) far, the picture's s 1 to 0
  across; t at the near edge (time since 9 s) x 0.000015, the far edge
  one less, so it scrolls up and away.
- **The colour:** (255, 235, 127), opaque, the far corners' colour a
  tenth of it; from 78.5 s it fades to nothing at 81 s, when the
  cinematic ends.

The picture's shaders are in `shaders/text_crawl.shader`: `tc_0`
English, `tc_1` French, `tc_2` German, each a clamped picture blended
by its vertex colour and alpha.

## In Reluva

The single-player Play page plays them as Elite Force's: the movie full
screen, the game held under one played in a level, a typed key or a
click skipping it. The game's console commands wait while one plays,
as the original's game is paused before it sends more. The crawl
(`GameInfo::textCrawl`) is drawn as above through the scene drawer,
which takes a view reaching past the screen and its own field of view
across (the game's `fov_x`, widened in a window wider than 4:3 as every
view is), and its music plays as the movie's sound.

Not yet: the separate speech of `jk05` and `jk06` in other languages,
the cinematics the client game plays itself (its calls 90 to 94), and
the console's `cinematic` with a second word.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: `CL_PlayCinematic`
  (0x404920), the commands (0x404c00, 0x404c20, 0x404c60), the crawl's
  drawing (0x404cc0–0x404fee) and its constants (0x4c8428–0x4c8460), the
  server's pause check (0x437d40); `shaders/text_crawl.shader`.
- **How it was checked:** on the Play page, `kejim_post`'s opening
  played through: the crawl's text scrolls away from the bottom of the
  screen and fades, the second movie plays, then the level's cutscene.
