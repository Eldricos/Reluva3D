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
   in a language other than English (`s_language` neither `english` nor
   empty, 0x404a8f), their speech is a separate track
   (`sound/chars/video/cinematic_5`, `_6`), handed to the player in
   place of the movie's own sound (0x404b67).
6. Played at 640 x 480; one in a level sets `cl_paused` to 1, which stops
   the server's frames (0x437d40) until it ends.

A movie out of a level (the system's) also shuts the console and closes
the menus as it starts (0x403c74).

`ingamecinematic` (0x404c20) plays it in the level (state 7) at once, or
keeps it for when the level starts. Both commands take `how` as their
second word.

The client game's own calls for movies (90 to 94: play, stop, run,
draw, set extents; jk2sp.exe's 0x403a00, 0x402520, 0x402780,
0x4046d0, 0x404650, cased at 0x401d92 on) aren't made by any release's
client game: its system call wrappers (through the pointer `dllEntry`
keeps, 0x201172c8 in 1.02a's `jk2gamex86.dll`) cover the other numbers,
none of 90 to 94 (93 to 97 in 1.03's and 1.04's, whose DLLs are the
same). The engine answers them as multiplayer's menus' videos
([syscalls.md](syscalls.md), Videos): 16 slots of 0x140 bytes at
0x4f1200, the same name rules and bits, and a seventh argument, a sound
registered and played in place of the video's own (0x403c49). The frame
is drawn stretched at its extents (`DrawStretchRaw`, 0x404821).

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
view is), and its music plays as the movie's sound. A movie asked to be held
(`how` "1", or one of `GameInfo::heldMovies`: `end`) stays on its last
frame until skipped; one asked to loop ("2") starts again with its
sound; out of a level, one shuts the console, and the console is drawn
over a movie while it's open. Seen running: `cinematic openinglogos 1`
from the console held the logos' last frame until a key. `jk05` and
`jk06` outside English play their separate speech
(`GameInfo::separateSpeech`) in place of the movie's sound.

The client game's movie calls (90 to 94) play in `CinematicSlots`, as
the menus' videos do, their frames drawn among its pictures in its 640
x 480 screen, their sound through the page's mixer; a 17th at once ends
the game with the original's error.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: `CL_PlayCinematic`
  (0x404920), the commands (0x404c00, 0x404c20, 0x404c60), the crawl's
  drawing (0x404cc0–0x404fee) and its constants (0x4c8428–0x4c8460), the
  server's pause check (0x437d40); `shaders/text_crawl.shader`.
- **How it was checked:** on the Play page, `kejim_post`'s opening
  played through: the crawl's text scrolls away from the bottom of the
  screen and fades, the second movie plays, then the level's cutscene.
