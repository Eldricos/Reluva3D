# Jedi Knight: cut scenes (`.smk`)

> **Status:** decoded · **Code:** `src/video/smacker.cpp` · **Checked on:** US retail, all 25 cut scenes on both CDs · **Confidence:** confirmed unless marked

## Overview

The cut scenes are Smacker videos ([Smacker](../../../../../common/video/smacker.md))
on the CDs, in `GAMEDATA/RESOURCE/VIDEO`: 8 on CD 1, 17 on CD 2; the
install has none. The episode names which plays where
([episode-jk.md](episode-jk.md)); `01-02A.SMK`, the opening, is named by
`JK.EXE`. `JK.EXE` plays them through `SMACKW32.DLL`, RAD's player,
which it imports by number.

## Layout

All 25 alike:

| | |
|---|---|
| Kind | `SMK2`, no flags |
| Picture | 640x300, 8-bit, its palette in the video |
| Speed | 15 frames a second |
| Sound | One track: 16-bit stereo, 22,050 Hz, packed |
| Length | 6.8 s (`41DA.SMK`) to 229.7 s (`01-02A.SMK`); 29,493 frames, about 33 minutes |

## In Reluva

Read by the shared decoder (`smacker::Decoder`, `src/video/smacker.h`).
`findCutScenes` (`src/engines/sith/folders.h`) lists the cut scenes on
the CDs found for an install. The data viewer's Cut scenes page plays
them with their sound, the picture following the clock.
`reluva-tool sith movies <install> -all`
decodes every frame and its sound.

## Sources and checks

- **Where the facts come from:** the 25 files' headers; `JK.EXE`'s
  imports and strings.
- **How it was checked:** every frame and the sound of all 25 decoded by
  `reluva-tool sith movies -all`, the sound as long as the picture in
  each.

## Open questions

- Where the game puts the 640x300 picture on its screen, and whether it
  shows the subtitles in `cutstrings.uni` over it.
