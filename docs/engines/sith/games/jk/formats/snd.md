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
then up to four numbers, as the files' own header comment names them.
A field left out takes the game's own value (`0x4e6888`) *(from the code)*:

| Column | What | If left out | Lines with it |
|---|---|---|---|
| class | The event | The line is skipped *(from the code)* | All 1,976 |
| filename | The `.wav`; `none` for no sound (`0x4e682e`) *(from the code)* | The line is skipped *(from the code)* | All |
| playflags | Flags, read as hexadecimal (`0x4e68ac`) | `0x40` *(from the code)* | 1,901 |
| minRadius, maxRadius | Distances from the viewer, in the level's units (`0x4db8f7`) | 0.5 and 2.5 *(from the code)* | 1,501 |
| maxVolume | A volume, 1 for the sound as recorded (`0x4e6ce5`) | 1 *(from the code)* | 67 |

The flags are read with `%x`, so they're always hexadecimal: the four
lines that write `0` read the same either way. A line whose event the
game doesn't know is skipped (`0x4e681a`) *(from the code)*. An event
may have more than one line (199 events in their files do): the game
picks one of them at random each time (`0x4e6bb8`) *(from the code)*.

The flags seen are combinations of `0x1`, `0x8`, `0x80`, `0x100`,
`0x200`, `0x800`, `0x4000` and `0x10000` (on 932 lines, most of the
characters').
Two files a line names aren't in the archives (`I00PI02Z.WAV`,
`df_machine2.wav`).

### Flags

What each does is read from the sound class's player (`0x4e6cd0`) and
the sound system's (`0x4db880`, `0x4dba60`); how many lines have it, from
the files.

| Flag | Meaning | Lines with it |
|---|---|---|
| `0x1` | Loops. A sound without it isn't started when the viewer is past maxRadius (`0x4dbac9`), and is let go when it ends (`0x4dc9dc`); when its thing is removed, a looping sound stops and any other plays on where the thing was (`0x4dcf20`, from `0x4cc2be`) *(from the code)* | 23: 18 doors' and lifts' `moving`, a few `activate`s, two `falling`s |
| `0x4` | Not panned, and not made quieter behind the viewer ([Volume and distance](#volume-and-distance)) (`0x4dcc1a`) *(from the code)* | None |
| `0x8` | Given the thing's velocity, for the sound card's 3D sound (Doppler), when the thing moves by physics (`0x4dce10`) *(from the code)* | 1 |
| `0x40` | Played once where the thing is, not following it (`0x4e6d1b`); without it, the sound follows its thing. No file sets it: it is what a line without flags gets *(from the code)* | None |
| `0x80` | Follows the thing: the game sets it on every sound that does (`0x4dbac4`), so it changes nothing in a file *(from the code)* | 3 |
| `0x100`, `0x200` | Kept: not cut off to make room for a new sound (`0x4db6b6`, `0x4dbd75`, `0x4dd42b`), and played even while the character is already speaking (`0x4dbb4b`) *(from the code)*. The game tests them only together: the same *(inferred)* | 56 (death and drowning) and 14 (`deathUnder`) |
| `0x400` | Not played while the same sound is playing anywhere (`0x4e6cf3`) *(from the code)* | None |
| `0x800` | Not played while the same sound is playing on the same thing (`0x4e6d01`) *(from the code)* | 2 |
| `0x4000` | Its sound loaded with the level (`0x4e690c`) *(from the code)* | 404 |
| `0x10000` | A character's voice: on an actor or the player, a new one isn't played until the last one's length has passed, unless it has `0x100` or `0x200` (`0x4dbb43`) *(from the code)* | 932 |

Every test of `0x100` or `0x200` in the sound system (`0x4db100` to
`0x4dd600`) and the sound class's player takes the two together (`0x301`
or `0x300`); none takes either alone *(from the code)*. maxVolume is multiplied by the volume the caller asks for, and the
result held to 0 to 1.5 (`0x4dba82`) *(from the code)*.

## Behaviour

### Volume and distance

In `JK.EXE`: `0x4dcc00`, each frame for each sound playing, after
`0x4dd190` has worked out its distance *(from the code)*.

- **The distance** is from the viewer to the sound: to its thing, or to
  where it was played for one that doesn't follow its thing. Where any of
  the three axes is past maxRadius, it is taken as maxRadius + 1
  (`0x4dd2f4`); else it is an approximate length (`0x449c07`). When one
  of the sound and the viewer is in an underwater sector
  ([jkl.md](jkl.md)'s sector flag `0x2`) and the other isn't, the
  distance is taken 1.5 times (`0x4dd2ac` to `0x4dd2ec`) *(from the
  code)*.
- **Panning**, unless the sound has `0x4` or plays through the sound
  card's 3D sound: the direction to the sound, made a unit vector, times
  the viewer's right, times 2000 is the pan in hundredths of a decibel
  (`0x4dcc28` to `0x4dcc6a`, `0x437360`) *(from the code)*.
- **Behind the viewer**, on the same terms: the volume is multiplied by
  `1 - 0.3 × c`, where `c` is how far behind the direction points (0 to
  the side, 1 straight behind): 0.7 straight behind (`0x4dcc83`) *(from
  the code)*.
- **Between minRadius and maxRadius** the volume falls in a straight
  line: multiplied by `1 - (distance - minRadius) / (maxRadius -
  minRadius)`, the fraction held to 0 to 1, with the `1 / (maxRadius -
  minRadius)` kept when the sound starts (`0x4dba2d`). Full up to
  minRadius, silent from maxRadius (`0x4dcc9f` to `0x4dcce9`, and
  `0x4dcd33` for a 3D sound) *(from the code)*.
- **To the sound card:** the volume, held to 0 to 1.5, is multiplied by
  0.75 (`0x4dcd19`), held to 0 to 1 again, and set as `-2500 × (1 -
  v)²` hundredths of a decibel: -25 dB at 0, about -1.6 dB for a sound
  at 1. Without the 3D sound option it is 2 dB lower still
  (`0x437390`, `0x4373fe`) *(from the code)*.
- **Out of range:** past maxRadius a sound is stopped and its buffer let
  go; one that doesn't loop is done with, and a looping one starts again
  when the viewer comes back within maxRadius (`0x4dca4f` to
  `0x4dcb33`) *(from the code)*.

## In Reluva

`parseSoundClass` (`src/engines/sith/formats/sound_class.h`) reads one
into a `SoundClass`, leaving out the fields a line hasn't, the flags
read as hexadecimal as the game reads them. The data
viewer's Sound classes page lists each event and plays its sound.
`reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 102 files, every line of them, and
  their header comments; `JK.EXE`'s sound class reader (`0x4e67f0`), its
  player (`0x4e6b70` to `0x4e6d62`) and the sound system's (`0x4db100`
  to `0x4dd600`, with the sound card's at `0x437360` and `0x437390`),
  read from the machine code.
- **How it was checked:** every sound class read by `reluva-tool sith
  formats`, and each line's `.wav` looked for in the archives; every
  line's flags and fields counted by a script, against what the code
  does with them; `sith.unit.formats` on a sound class made by the test.

## Open questions

- Whether `0x100` and `0x200` differ anywhere outside the sound system
  and the sound class's player: neither is tested alone there
  ([Flags](#flags)), and the rest of `JK.EXE` wasn't searched.
