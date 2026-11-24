# Star Trek: Armada: the settings (`ARMADA.PRF`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/preferences.cpp` · **Checked on:** the files the original wrote under the emulator · **Confidence:** fields confirmed by changing them; ranges inferred

## Overview

`ARMADA.PRF`, in the install's folder, keeps the settings screens'
values ([systems/front-end.md](../systems/front-end.md#the-settings-screens)).
The program looks for it as it starts, using its defaults if it isn't
there, and writes it as it quits.

## Layout

Six lines of text, CR LF at their ends; numbers split by single
spaces, written as C's `%g` writes them (`0.5`, `10`). The file the
original wrote on its first run under the emulator:

```text
9404509
2 3 1 6 1 1 0
10 10 10 8 1 1 1
Reluva DirectDraw
0.5 0.5 5 5 5 4 640 480 16 1 0
0
```

Each field was found by changing one control in the original, quitting,
and comparing the file it wrote:

| Line | Fields, in order |
|---|---|
| 1 | Unknown: the same in every file; set to 0 or 1, it changed nothing seen |
| 2 | Difficulty (1 Easy, 2 Medium, 3 Hard); game speed (1 to 6); mouse scroll speed (tenths, 0 to 2); keyboard scroll speed (0 to 36, a click 4); mouse button interface (1 Right Click, 0 Left Click); mouse camera control; directors cut |
| 3 | Music, effects and voice volumes (0 to 10); sound channels (1 to 8); stereo sound; unit speech; alert audio |
| 4 | The display driver's name, as DirectDraw gives it |
| 5 | Two tenths, 0.5 each (by the screen's words, gamma and background brightness *(guessed)*); four detail levels, the last the Graphics Detail slider (0 to 4); the display mode's width, height and depth; cinematic window *(inferred)*; use alternate font |
| 6 | Unknown: 0 in every file; not the campaign progress |

Switches are 1 for On and 0 for Off. Sound Channels turns from 8 to 1,
then up a click at a time. Game speed stopped at 1, keyboard scroll at 0
and mouse scroll at 1.3 after three clicks of + from 1.

A slider's line is its value times so many pixels: 17 a volume step, 28
a game speed step, 42 a detail step, 80 a mouse scroll unit (8 a
tenth), 28 for 6 of keyboard scroll. The frames show those lengths at
each value seen; the ranges' ends are where the clicks stopped.

## In Reluva

`Preferences` (`src/engines/storm3d/formats/preferences.h`) reads the
lines and keeps them as text, a value found by its line and place. The
settings screens change the values; the game writes the file to its
save folder as it ends, as the original does, its driver "Reluva". The
volumes set the mixer's music, effects and speech volumes.

## Sources and checks

- **Where the facts come from:** the files the original wrote, each
  after a few clicks on one screen; its frames after the clicks.
- **How it was checked:** the unit tests (`storm3d.unit.player`) read
  the original's file and write it back the same, and write numbers as
  it does. Reluva's settings screens, given the original's values, match
  its frames ([systems/front-end.md](../systems/front-end.md)).

## Open questions

- What the first and last lines are.
- Whether a click on Cinematic Window changes it at all: in two runs it
  stayed On, and the file didn't change.
- The scroll speeds' true ranges, and why keyboard scroll's line is 28
  for 6.
