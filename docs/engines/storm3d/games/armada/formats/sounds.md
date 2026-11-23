# Star Trek: Armada: sounds (`.wav`)

> **Status:** decoded · **Code:** `src/engines/storm3d/pages/sound_page.cpp` (the viewer), `src/audio/wav.cpp` (reading) · **Checked on:** the English retail 1.0 install's 1,168 sounds · **Confidence:** confirmed unless marked

## Overview

Every sound is a WAV file, in folders by kind:

| Folder | Files | Size | What |
|---|---|---|---|
| `sounds/effects/` | 225 | 30 MB | Weapons, explosions, the interface |
| `sounds/music/` | 43 | 169 MB | The music (`MRom10.wav`, `Mbor10.wav`...) |
| `sounds/voiceover/ingame/` | 679 | 30 MB | The ships' and the computer's voices in play |
| `sounds/voiceover/cineractive/` | 216 | 39 MB | The voices of the in-engine cut scenes |
| `wav/` | 5 | 35 MB | The installer's music, which its script names (`FCIntro.wav`, `Install.wav`) |

## Layout

Plain RIFF WAVE files, uncompressed PCM:

| Kind | Files | Where |
|---|---|---|
| 22,050 Hz, 8-bit, mono | 1,102 | most effects and voices |
| 44,100 Hz, 16-bit, mono | 42 | 41 of the 43 pieces of music, and one effect |
| 22,050 Hz, 16-bit, mono | 11 | 9 effects, 2 voices |
| 22,050 Hz, 8-bit, stereo | 7 | the installer's 5, and 2 pieces of music |
| 22,255 Hz, 8-bit, mono | 5 | 5 voices |
| 22,050 Hz, 16-bit, stereo | 1 | an effect |

They add up to 113 minutes.

## In Reluva

Sounds are read with the shared WAV decoder (`audio::decodeWav`,
`src/audio/wav.h`) and played on the shared mixer.

- **The data viewer's Sounds page** (`SoundPage`,
  `src/engines/storm3d/pages/sound_page.h`) lists every WAV in the
  install and plays one, as music, speech or effects by its folder.
- `reluva-tool storm3d sound-check <game folder>` decodes every sound and
  counts them by kind.

## Sources and checks

- **Where the facts come from:** every file's `fmt` chunk; the installer's
  music from its decoded script ([ldl.md](ldl.md)).
- **How it was checked:** `sound-check` decodes all 1,168.
