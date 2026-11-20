# Star Trek: Birth of the Federation: the movies

> **Status:** partial · **Code:** `src/engines/botf/botf.cpp` (`playOpening`) · **Checked against:** the English retail CD's `open.avi` and the install's `intro.wav` played through Reluva · **Confidence:** confirmed unless marked

## Overview

The CD's language folder holds the game's movies, AVI files with Cinepak
pictures ([avi.md](../../../common/video/avi.md),
[cinepak.md](../../../common/video/cinepak.md)): `open.avi`, the opening
movie, and `C.avi`, `F.avi`, `H.avi`, `K.avi` and `R.avi`, one for each
empire, by the race letters ([formats/stbof-res.md](../formats/stbof-res.md#races)).
Reluva plays the opening movie; the empires' wait for a game to start.

## How it works

### The opening movie

- **`ENGLISH/open.avi`** (and `GERMAN/open.avi`): Cinepak, 800x300, 24
  frames a second, 1,804 frames, 75.17 seconds, and no sound track.
- **Its sound is the install's `intro.wav`:** PCM, 16-bit stereo at 22,050
  Hz, 75.19 seconds, the movie's length to the frame. Reluva plays it with
  the picture following it.
- **On the screen:** the game's 800x600, the picture in its middle, rows
  150 to 449, black above and below: a screenshot during it matches the
  movie's frame 1061 there.
- **The movie** is a flight past stars and planets, ending on the
  copyright notice.
- **Before the main menu:** played at startup, unless `Stbof.ini` says
  `MOVIEON=OFF` ([game-files.md](game-files.md)). A key or a click skips it
  (the user, in the game). Its sound is at the video volume ([sound.md](sound.md)).

## In Reluva

`playOpening` (`src/engines/botf/botf.cpp`) finds `open.avi` and
`intro.wav` where `GameFiles` finds the game's files (the CD's language
folder for the movie), and plays them through the shared movie player
(`src/video/movie.cpp`, `video::playMovie`), which plays Cinepak and
Indeo 5 pictures, on a screen of the engine's size with the picture in its
middle, and a sound given beside the movie when the file has none. The
unit tests (`common.unit.video`: "cinepak movie") cover the player's
placing and playing a Cinepak movie to its end.

## Sources and checks

- **Where the facts come from:** the user's copy of the English retail CD
  and a fresh install (`ENGLISH/open.avi`, `intro.wav`), November 2026;
  `Stbof.ini`'s `MOVIEON` from the installer's copy.
- **How it was checked:** `open.avi` played by Reluva's player into a
  recording system: all 1,804 frames shown in 75.2 seconds on an 800x600
  screen, stills at 5, 20, 40 and 70 seconds looked at, and the sound
  played the same as `intro.wav` sample for sample.

## Open questions

- When the empires' movies play: presumably after the empire selection
  screen, as a game starts.
