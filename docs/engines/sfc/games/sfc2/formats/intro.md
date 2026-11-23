# Star Trek: Starfleet Command II: the opening movie (`SFC2Intro.exe`)

> **Status:** decoded · **Code:** `src/engines/sfc/formats/intro_movie.cpp` · **Checked on:** English retail 1.0, its `SFC2Intro.exe` · **Confidence:** confirmed unless marked

## Overview

The opening movie isn't a file of its own: `SFC2Intro.exe` (68,947,456
bytes) is a small Windows program that plays a Bink movie held after its
own sections, and the original launcher, `SFC2.exe`, runs it from the
game's folder or `Movies\` before the game. The game's program doesn't
play it.

## Layout

| Offset | Size | What |
|---|---|---|
| 0 | 0x1d600 | The player: a PE whose sections (packed with UPX) end at 0x1d600 |
| 0x1d600 | 8 + the size at +4 | A Bink movie, `BIKi` (revision `i`), its size after the first 8 bytes at +4 |
| after it | 296 | Zeros, to the next 512 bytes |

The movie is found where the program's sections end (the largest raw
offset plus raw size in its section table): the player is laid out as
any program is and the movie is appended to it *(inferred)*.

The movie: 640 x 480, 2,721 frames at 15 a second, one audio track at
44,100 Hz in stereo, 181.40 seconds; 68,826,840 bytes
([Bink](../../../../../common/video/bink.md)).

## In Reluva

`introMovie` takes the movie out of the program; the engine plays it
(`video::playMovie`) before starting the game, when `SFC2Intro.exe` is in
the game's folder, and a key or a click skips it.
`reluva-tool sfc intro <game folder> [-frame <n> <out.png>]...` reads it,
decodes its sound and every frame, and writes the frames asked for.

## Sources and checks

- **Where the facts come from:** the program's own PE headers and the
  Bink header found at the end of its sections; that the launcher runs it
  from `\sfc2intro.exe` or `\Movies\sfc2intro.exe`, from the names in
  `SFC2.exe` *(seen, not traced)*.
- **How it was checked:** `reluva-tool sfc intro` decoded its sound and
  all 2,721 frames without an error; the movie played in the window, its
  sound with it, the Taldren logo at 20 seconds, a click skipping it to
  the game's title screen. `testIntroMovie` (`tests/engines/sfc/unit/formats.cpp`)
  takes a movie out of a small program built for it, and refuses one
  that claims more than there is.

## Open questions

- Whether the launcher has a setting that skips the movie: none is found
  in `SFC2.exe` *(seen, not traced)*; Reluva always plays it.
