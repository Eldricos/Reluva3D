# Clue: Murder at Boddy Mansion: videos (`.avi`)

> **Code:** `src/engines/clue/movie.h`, `src/common/video/avi.h`, `src/common/video/cinepak.h`

## Overview

The game's videos: the intro, the deal, the six suspects' prison clips and
the 36 suggestion clips, 44 AVI files.

## Layout

44 files, all Cinepak at 15 frames a second with 16-bit mono PCM sound at
22,050 Hz: the intro, the deal and the six suspects' prison clips
(`CM_prison.avi` ...) at 320×240; the 36 suggestion clips, a suspect
with a weapon, at 200×400 (`CM_candle.avi` alone at 250×500). The
suggestion clips last 6.3 s: 1.4-2.7 s of picture (in the five
looked at), then black frames while the sound goes on, as the original
shows them.

## In Reluva

They're played by the shared AVI and Cinepak decoder
([docs/common/video/cinepak.md](../../../common/video/cinepak.md)), shown by the clock
(src/engines/clue/movie.h), with their sound at full volume, as Clue.exe's
DirectShow graph plays it.

## Sources and checks

- **Where the facts come from:** the 44 files themselves; the suggestion
  clips' lengths of picture from the five looked at.
