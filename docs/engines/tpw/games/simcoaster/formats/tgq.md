# SimCoaster: movies (`.tgq`, `.avi`)

> **Status:** decoded · **Code:** `src/video/tqi.cpp`, `src/video/ea_movie.cpp` · **Checked on:** the US release's two `.tgq` files and its `.avi` · **Confidence:** confirmed unless marked

## Overview

The game has three movies:

| File | What it is |
|---|---|
| `Data/movies/bf.tgq`, `Data/movies/bfSim.tgq` | The EA GAMES logo, the same file twice (1,295,940 bytes each, the same MD5) |
| `TPinc30.avi` | "SimCoaster Training Video": 3 min 57 s of play, 320 × 240, Cinepak, with PCM sound ([cd.md](cd.md)) |

## Layout

### The logo (`.tgq`)

An EA movie, as the shared page describes it
([tqi.md](../../../../../common/video/tqi.md)): one sound segment,
`SCHl` (stereo EA-XA at 22,050 Hz, 71,432 samples: 3.24 s), `SCCl`, 81
`SCDl` blocks and `SCEl`, with 81 `pIQT` frames among them, each with
its sound block. The frames are 320 × 480 at quantiser 99, 15 a second:
5.4 s, longer than the sound.

The pictures are half as wide as they're shown: at 640 × 480 the logo's
ring is round, as the recording shows it
([research/footage.md](../research/footage.md#the-opening-second-by-second))
*(inferred)*.

### The training video (`TPinc30.avi`)

An AVI with Cinepak pictures, which the shared player plays
([cinepak.md](../../../../../common/video/cinepak.md)). The recording
doesn't show the game playing it.

## In Reluva

The shared code reads the logo (`video::EaMovie`, `tqi::Decoder`), and
the shared player plays it (`video::playMovie`); the engine plays it at
the start ([opening.md](../systems/opening.md)).

`reluva-tool tpw tgq-check <file.tgq> [frame out.tga]` decodes a movie,
every frame and its sound, or writes a frame out; `reluva-tool tpw
movie-check <game folder>` decodes every `.tgq` of a game, which the
game-files test runs (`tpw.gamedata.movies`).

## Sources and checks

- **Where the facts come from:** the files, their chunks listed; the two
  `.tgq` compared by MD5; the training video's title from its frames.
- **How it was checked:** both `.tgq` decoded with `reluva-tool tpw
  tgq-check`, their pictures compared with ffmpeg's decoder's (run as a
  program) to 58-61 dB, and a frame looked at stretched to 640 × 480.

## Open questions

- Whether the game shows all 81 frames, or stops the movie with its
  sound, at 3.24 s ([opening.md](../systems/opening.md#open-questions)).
- How the game turns the pictures' YUV into RGB: its logo's white is
  darker than BT.601 gives ([opening.md](../systems/opening.md#open-questions)).
