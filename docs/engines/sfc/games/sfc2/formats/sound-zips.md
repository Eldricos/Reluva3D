# Star Trek: Starfleet Command II: sounds and music (`.zip`)

> **Status:** decoded · **Code:** `src/engines/sfc/pages/sound_page.cpp` · **Checked on:** English retail 1.0, its three sound zips · **Confidence:** confirmed unless marked

## Overview

`Assets/Sound/` holds the sounds in three zips, every entry stored
without compression: `SFCSounds.zip` (353 effects, and two text files),
`SFCVoice.zip` (957 voices: the bridge crew's, `kSndSulu1_Advice2.wav`)
and `sfcmusic.zip` (698 pieces of music). `sounds.txt` and
`SFCMusic.txt` say how each is played
([text-tables.md](text-tables.md)).

## Layout

Standard zips of standard WAV files, all at 22,050 Hz:

| Zip | WAVs |
|---|---|
| `SFCSounds.zip` | 291 16-bit PCM mono, 62 stereo |
| `SFCVoice.zip` | 777 IMA ADPCM mono, 180 16-bit PCM mono |
| `sfcmusic.zip` | 698 16-bit PCM stereo |

`SFCVoice.zip` has one entry deflated rather than stored.

## In Reluva

- **Reading them:** the shared zip reader (`src/common/zip.h`) and WAV
  decoder (`src/audio/wav.h`).
- **The data viewer:** the Sounds page lists each zip's sounds and plays
  them.
- **Commands:** `reluva-tool sfc sounds <game folder>` decodes every one.

## Sources and checks

- **Where the facts come from:** the zips' own directories and the WAVs'
  headers.
- **How it was checked:** all 2,008 WAVs decoded by
  `reluva-tool sfc sounds`; sounds of each zip played by the data viewer.
