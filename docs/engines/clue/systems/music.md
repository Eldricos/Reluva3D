# Clue: Murder at Boddy Mansion: music

> **Status:** done · **Code:** `src/engines/clue/frontend.cpp` · **Checked against:** `Clue.exe` 2.2's code

## Overview

The game's music: the CD's four audio tracks, played in the menus and in
the game.

## How it works

The CD (volume label `Clue V2.0.01.27`) has a data track and four audio
tracks, 44.1 kHz stereo, which aren't copied to the hard disk:

| Track | Length |
|---|---|
| 2 | 3:55.00 |
| 3 | 3:11.84 |
| 4 | 2:38.51 |
| 5 | 3:48.13 |

`Clue.exe` plays them through Miles' "redbook" functions. The function
that picks the next one when a track ends (at `0x42F130`) keeps a number
from 2 to 6: 2 goes to 3, 3 to 4, 4 back to 2, 5 plays again, and 6 is
silence. So tracks 2-4 play in turn and 5 loops alone. Where each
starts and stops (0x42F0D0 starts a track, 0x42F100 stops the CD,
0x42F120 asks whether it is playing; the screens are MFC classes, named
by their runtime-class records):

- The menus with the storm (`MainMenu`, `NewGameMenu`, `RulesMenu`, ...)
  play no CD music, only `mnThndr1.wav` looping.
- `FamilyAndSinglePlayerMenu` (the character picker and setup, 0x412F30)
  and `NetPlayerSetupMenu` (0x411CB0), as they open: `mnThndr1.wav`
  stopped and, if the CD isn't playing, track 5. It plays on from there
  through the menus until something stops it.
- `PrefMenu` (the menus' Preferences, dialog 24), as it opens
  (0x415810): track 5 if the CD isn't playing. Its Return (control
  10000, 0x41650C) stops the CD. The game's own Preferences, from the Esc
  menu, are another window (dialog 37, its code about 0x43F0B0), which
  touches the music only by its volume.
- Starting a game (0x4135D0 from the picker, 0x41228F from the network
  setup) stops the CD. Loading it (0x404780) stops `mnThndr1.wav` and
  loops `load music loop22khz.wav` (sound 0x43); when it is loaded that
  stops and track 2 starts (0x404E53).
- A suggestion's video, with "Suggestion Movies" on (0x41B9EA): the CD
  stopped for it, then one of tracks 2, 3 and 4 by chance (0x41BA19).

With no CD audio the original plays no music and carries on. Reluva
does all of the above.

## In Reluva

Reluva can't read the tracks from the files on the CD, so the player
rips them once, as ScummVM asks for some games, to `track02.wav` ...
`track05.wav` (or `.mp3`) in the game's folder. Tracks that aren't there
are left silent, as the original does without its CD. `reluva-tool
cue-rip` makes them from a BIN/CUE image of the CD.

## Sources and checks

Source: the CD's cue sheet and its tracks, and `Clue.exe` 2.2 read where
it plays them.
