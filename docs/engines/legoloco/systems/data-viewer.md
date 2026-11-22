# LEGO Loco: the data viewer

> **Status:** done · **Code:** `src/engines/legoloco/pages/viewer.cpp`, `src/engines/legoloco/pages/viewers.h` · **Checked against:** the CD's files · **Confidence:** confirmed unless marked

## Overview

Reluva's viewer of the game's files, which the original doesn't have:
`reluva-viewer <name>` or `reluva-viewer <folder>` opens it on a copy
added in the launcher, or on an install's (or the CD's) folder. Tab moves
from page to page, Esc leaves. It needs `Exe/Loco.exe` beside `art-res`,
for the resources' names
([resource-numbers.md](../formats/resource-numbers.md)).

## How it works

The original has nothing like it. What it shows is drawn as
[drawing.md](drawing.md) says the game draws, from the formats on their
pages.

### Boards

The CD's layouts (`art-res\SAVEGAME`), the screen saver's boards
(`art-res\ScrSaver`), `~curr`, and the resource file's area layouts
([sav.md](../formats/sav.md)): each on its backdrop, tiled, with its
pieces' pictures at their squares. A board naming no backdrop gets
`backdrop.bmp` *(guessed)*. S draws each square's value over it, coloured
by what the boards show they mean: 2 scenery, 3 a building, 5 track, 6
road, 7 path *(seen, not traced)*. P and B take the pieces and the
backdrop away; Up/Down and `[` and `]` scroll a board larger than the
screen.

### Pictures

Every `.bmp` and `.but` in the resource file. A picture with a
definition is cut into its frames: the frame is shown large, and the
picture below it with the frame marked. Up/Down choose a frame set,
Space plays it (moving on to the set it names next), `,` and `.` step
frames, W shows the whole picture with the frames' edges, Z zooms and K
shows colour 0 rather than leaving it out.

### Definitions

Every `.dat`: its parts' keywords and values, the frame-set table, the
occupancy grids drawn as coloured cells, and the picture's first frame.

### Sounds

Every `.wav`, played on Space (A plays each as it's chosen). Without an
audio device they're listed and not played.

## In Reluva

- `src/engines/legoloco/pages/`: the pages (`board_viewer.cpp`,
  `picture_viewer.cpp`, `definition_viewer.cpp`, `sound_viewer.cpp`),
  their drawing (`draw.cpp`) and the viewer that runs them
  (`viewer.cpp`), in a library of their own, never in the game.
- `src/engines/legoloco/game_files.cpp`: `GameFiles`, a copy's
  resource file and resources' names.
- `reluva-tool legoloco view <game folder> <page> <item> <out.png> [keys]`
  draws a page's item into a PNG without a window, after pressing the
  page's letter keys (`>` and `<` for Down and Up): for looking at the
  pages where there's no display.

## Sources and checks

- **Where the facts come from:** the formats' pages and
  [drawing.md](drawing.md).
- **How it was checked:** every page opened on the CD's files, and every
  board of the CD drawn with none of its pieces left without a picture;
  `legoloco.engine` takes the viewer through each page on a small
  install of the test's own and records what it draws.
