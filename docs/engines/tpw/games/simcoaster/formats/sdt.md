# SimCoaster: sound banks (`.sdt`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/sdt.cpp` · **Checked on:** the US release's 28 banks, every sound · **Confidence:** confirmed unless marked

## Overview

The game's sounds and music are in banks: 28 of them, loose in
`Data/global/sound/` (`UIHD.sdt`, `KidsHD.sdt`, `RideHD.sdt`...),
`Data/global/Speech/speechHD.SDT` (the advisor's 1,136 lines) and each
world's `Sound/` and `Music/` folders, 5,399 entries in all. The `HD`
in their names is perhaps "hard disk" *(guessed)*. Beside them, `.map`
files ("cat_...BANK.map", "cat_...SFX.map") describe them to the game:
not read yet.

## Layout

All numbers little-endian.

### The bank

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | How many entries |
| 4 | 4 each | Where each entry's header is, from the start of the file |

The first entry follows the table; each entry's data runs straight into
the next one's header.

### An entry

A 0x28-byte header, then its data:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | 0x28, the header's size |
| 0x04 | 4 | The data's size |
| 0x08 | 16 | Its file's name, ending in a 0 if shorter: `BALLONP3B.mp2`, `bkart_b1.wav`; a longer one is cut at 16 (`bellowsteam1.mp`) |
| 0x18 | 2 | The sample rate: 22,050, or 44,100 for 12 sounds |
| 0x1A | 1 | 16: bits a sample |
| 0x1B | 1 | Its kind: `0x24` MPEG mono, `0x25` MPEG stereo, `0x02` PCM mono |
| 0x1C | 4 | 0 |
| 0x20 | 4 | For MPEG, the sound's size decoded, in bytes; 0 for PCM |
| 0x24 | 4 | 0 |
| 0x28 | | The data |

In the kind, `0x20` marks MPEG and `0x01` two channels *(inferred: the
MPEG frames' own channel mode agrees)*; what `0x04` and `0x02` add isn't
known.

### The data

- **MPEG:** MPEG audio frames: MPEG-2 layer II (`.mp2`; the frames
  start `FF F5`) or layer I (`FF F7`, the effects, named `.mp2` all the
  same), at 22,050 Hz; MPEG-1 layer I (`FF FF`) for the 12 at 44,100 Hz.
  4,811 are mono, 452 stereo (the music).
- **PCM:** 16-bit samples, with no WAV header despite the names: 52
  sounds.

### What doesn't decode

- **Two music banks' last entries:** `Data/levels/arabian/Music/MusicHD.sdt`'s
  last 40 (of 143) and `Data/levels/water/Music/MusicHD.sdt`'s last 15
  (of 116) point where the entry before ends, but no header is there,
  and the bytes aren't MPEG frames. Both files end at round sizes
  (0x26E6000 and 0x1E00000 bytes), and they're the same in the CD's
  cabinet, its MD5s checked: written short when the game was made, and
  never played *(inferred)*. The entry before the gap in the first
  (`Level5-3.mp2`) turns from stereo to mono partway, and doesn't decode
  either.
- **28 speech lines** (`SP_0195` and others in `speechHD.SDT`): each a
  single MPEG frame, 315 bytes, one byte shorter than the frame says it
  is; ffmpeg rejects them as well. Their headers make them 18 ms long:
  perhaps lines left unrecorded *(guessed)*.
- **Two effects** (`pirateservo1B`, `steamspinidle1` in
  `Data/levels/science/Sound/RideHD.sdt`) decode to a length other than
  their header's (58,368 samples for 60,928; 91,776 for 77,062).

## In Reluva

`SoundBank` (`src/engines/tpw/formats/sdt.cpp`) reads a bank's entries
and decodes one: MPEG through the shared decoder (`audio::decodeMp3`,
minimp3, which takes layers I to III), PCM as it is. An entry without a
header is listed as not readable.

`reluva-tool tpw sdt-check <game folder> [bank]` decodes every sound,
listing those that don't decode; with a bank's name, it lists its
sounds. The game-files test runs it (`tpw.gamedata.sounds`).

## Sources and checks

- **Where the facts come from:** the US release's banks (November 2026):
  the table by its count and offsets, the headers' fields by their
  values across all 5,399 entries against their names and data; MPEG's
  layer and rate from the frames, checked with ffprobe; the decoded
  sizes against what the frames decode to (the speech line `SP_0001`:
  175,848 bytes, 3.99 s, as ffprobe's 4.02 s).
- **How it was checked:** every sound decoded with `reluva-tool tpw
  sdt-check`: 5,315 decode, all but two to their header's length. The
  unit tests (`tpw.unit.formats`) read a bank built in the test.

## Open questions

- The `.map` files: what they say of each sound (volumes, priorities,
  which bank), and the 16-byte id they start with.
- The kind byte's `0x04` and `0x02`.
