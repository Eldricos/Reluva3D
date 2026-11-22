# Jedi Knight: sounds (`.wav`)

> **Status:** decoded · **Code:** `src/audio/wav.cpp` · **Checked on:** US retail, all 644 sounds in `Res1hi.gob` · **Confidence:** confirmed unless marked

## Overview

Every sound is a Windows `.wav` in `Res1hi.gob`: 420 in `sound\`
(effects, the levels' sounds), 224 in `voice\` (speech). The sound
classes ([snd.md](snd.md)) and scripts name them. `Res1low.gob`, on CD 1
for a smaller install, has the same names.

## Layout

RIFF WAVE files, as Microsoft documents them:

| Kind | Files |
|---|---|
| 16-bit PCM, 22,050 Hz, mono | 589 |
| 16-bit PCM, 22,050 Hz, stereo | 53 |
| 16-bit PCM, 11,025 Hz, mono | 2 |

41 of them have a `smpl` chunk after the sound, which holds no loops
(only its header, the unity note 60): whether a sound loops is up to
whoever plays it ([snd.md](snd.md)).

## In Reluva

Read by the shared decoder (`audio::decodeWav`, `src/audio/wav.h`).
`reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 644 files' chunks, read one by one.
- **How it was checked:** every sound decoded by `reluva-tool sith
  formats`.
