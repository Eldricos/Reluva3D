# Jedi Knight: sound classes (`.snd`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/sound_class.cpp` · **Checked on:** US retail, all 102 sound classes · **Confidence:** confirmed unless marked

## Overview

The sounds a kind of thing makes, each for an event of its own: a
door's `startmove`, `moving` and `stopmove`, a character's
`hurtimpact`, `death1`, `lwalkmetal`. They are in `Res2.gob`'s
`misc\snd\`, plain text, one sound a line. The 102 files name 82
events.

## Layout

Line by line; a `#` starts a comment to the line's end. A line is its
event, then the `.wav` file (in `Res1hi.gob`'s `sound\` or `voice\`),
then up to four numbers, as the files' own header comment names them:

| Column | What | Lines with it |
|---|---|---|
| class | The event | All 1,976 |
| filename | The `.wav` | All |
| playflags | Flags, in hexadecimal (`0x010000`) or decimal | 1,901 |
| minRadius, maxRadius | Distances, in the level's units *(inferred)* | 1,501 |
| maxVolume | A volume, 0 to 1 *(inferred)* | 67 |

The flags seen are combinations of `0x1`, `0x8`, `0x80`, `0x100`,
`0x200`, `0x800`, `0x4000` and `0x10000` (on 932 lines, most of the
characters'). `0x1` is on 23 lines: 18 doors' and lifts' `moving`, which
go on as long as the thing moves, a few `activate`s and two `falling`s:
it loops *(inferred)*. The rest aren't understood. Two files a line names aren't in the
archives (`I00PI02Z.WAV`, `df_machine2.wav`).

## In Reluva

`parseSoundClass` (`src/engines/sith/formats/sound_class.h`) reads one
into a `SoundClass`, leaving out the fields a line hasn't. The data
viewer's Sound classes page lists each event and plays its sound.
`reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 102 files, every line of them, and
  their header comments.
- **How it was checked:** every sound class read by `reluva-tool sith
  formats`, and each line's `.wav` looked for in the archives;
  `sith.unit.formats` on a sound class made by the test.

## Open questions

- The flags but `0x1`, and what the game does when a field is left out.
