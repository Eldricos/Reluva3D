# Clue: Murder at Boddy Mansion: videos (`.avi`)

> **Status:** decoded · **Code:** `src/engines/clue/movie.h`, `src/common/video/avi.h`, `src/common/video/cinepak.h` · **Checked on:** the retail CD (2.0), a clean install of it and the 2.2 patch's install: all 44 files, the same in each · **Confidence:** confirmed unless marked

## Overview

The game's videos: the intro, the deal, the six suspects' prison clips and
the 36 suggestion clips, 44 AVI files. The US and UK games share them: the
CD keeps 43 in `Data/LangIndepend/Video` and the intro in
`Data/ClueSpecific/Video`, and an install copies all 44 into
`Data/Classic/Video`, byte for byte (the 2.2 patch leaves them as they
are).

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
- **How it was checked:** every frame and the sound of all 44 decoded by
  the shared decoder and set against the decoder Clue had before it
  (November 2026): the same pictures but for green, by 1 in 255 at most
  ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#not-yet-as-the-original)),
  and the same sound; the CD's, the 2.0 install's and the 2.2 install's
  copies are the same files. `MP_candle.avi` decoded against a recording
  of the original: a mean difference of 4 in 255 per colour.
