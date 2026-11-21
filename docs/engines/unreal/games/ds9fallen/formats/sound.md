# The Fallen: sounds and music

> **Status:** decoded · **Code:** `src/engines/unreal/formats/sound.cpp` · **Checked on:** the retail release, all 2930 sounds · **Confidence:** confirmed unless marked

## Overview

Sound and Music objects are exports of the packages (`.uax`, `.umx`, and
inside maps), each holding a whole file: a WAV for a sound, a tracker
module for music. The same reader extracts both.

On The Fallen (October 2026), 2868 of 2930 sounds decode. The other 62
(looping ones such as `Looping_Fear.moan_fear_lp`, music cues such as
`Trks_MagicBeaut.orb_thme`) are placeholders: format `wav` but only the
4 bytes `58 20 0d 0a` ("X \r\n"), nothing to play. Reluva treats anything
under 16 bytes that isn't RIFF as one.

They stand for The Fallen's music. The install's `mp3` folder holds 62
MP3 files, as many as there are placeholders, named as the placeholder
Sounds are (`Looping_Fear.moan_fear_lp` - `mp3/moan_fear_lp.mp3`,
`Recurring_cues.UnderWater` - `mp3/underwater.mp3`,
`Trks_MagicBeaut.orb_thme` - `mp3/orb_thme.mp3`), and the game ships
Miles' MP3 decoder (`redist/win32/Mp3dec.asi`) to stream them. So a
placeholder's audio is `mp3/<the Sound's name>.mp3`, found ignoring case
(`loadSound()`); the Sounds page plays it as music. (Worked out from the
install's file listing, October 2026.) Which plays when is the scripts'
doing: the player's `CurrentMusic`, the music triggers and the filler
music ([systems/sound.md](../systems/sound.md)). Sources as in
[package.md](package.md).

## Layout

Sound and Music objects hold a whole file:

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) |
| compact | name index of the format: `WAV` for sounds; a tracker format for music (`IT`, `S3M`, `XM`, `MOD`) |
| i32 | from version 63: the file offset just past the data |
| compact | size |
| bytes | the file: a RIFF WAVE for sounds, decoded by `audio/wav.cpp` |

Whether Music has exactly this layout in every version is to be checked
(`reluva-tool unreal check` counts music objects read and played).

## Behaviour

A Music object's module plays through Reluva's tracker player
([tracker.md](../../../../../common/audio/tracker.md)): it renders the
song from an order (a UE1 song section) at 44,100 Hz to where it jumps
back to an order already played, at most ten minutes, and that point is
the sound's loop. The Fallen has no modules: its music is the MP3s
(above), which play. Other UE1 games' modules (`.umx`: IT, S3M, XM,
MOD) play on the Sounds page, rendered from the first order to where the
song loops back; `reluva-tool unreal check` plays each through. Checked
only on modules the tests build: not yet on a real song.

A level's `Song` isn't played: The Fallen's audio driver (Miles.dll)
never reads it (no module player, `RegisterMusic` empty), and the drivers
that do (Galaxy's) aren't to hand to read their transitions from.

## In Reluva

`formats/sound.cpp` reads Sound and Music objects; the WAV is decoded by
`src/audio/wav.cpp` and played through `src/audio`'s mixer (how the game
plays it: [systems/sound.md](../systems/sound.md)). The Sounds page of the
data viewer plays them, the placeholders' MP3s as music, and
`reluva-tool unreal extract` writes one as a `.wav`.

## Sources and checks

- **Where the facts come from:** as for [package.md](package.md); the
  MP3s' names from the install's file listing (October 2026).
- **How it was checked:** `reluva-tool unreal check` on The Fallen
  (October 2026): 2868 of 2930 sounds decode, the other 62 the
  placeholders above.

## Open questions

- Whether Music has exactly this layout in every version
  (`reluva-tool unreal check` counts music objects read and played).
