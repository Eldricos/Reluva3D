# Star Trek: Birth of the Federation: the galaxy screen

> **Status:** partial · **Code:** `src/engines/botf/ui/galaxy_screen.cpp` · **Checked against:** a screenshot of the English retail game at a new game's first turn, as the Federation on a small galaxy · **Confidence:** confirmed unless marked

## Overview

The screen a game is played on: the galaxy's map in the middle, with the
turn and summary buttons, the empire's figures and its task forces down
the left, the map's modes along its bottom, and the selected system along
the screen's bottom. Each empire has its own layout, `?maingal.wdf` by
its letter (`hmaingal.wdf` for the Federation), and its own pictures.
Reluva draws the frame and the map from a saved game
([formats/sav.md](../formats/sav.md)), opened by Load Game's Accept or
the main menu's Continue; the panels and what the screen does are to
come.

## How it works

### The layout

`hmaingal.wdf`'s widgets, by their place and pictures:

| Widget | Place | What |
|---|---|---|
| 1001, 1006 | 63, 6 and 63, 42 | the Turn and Summary buttons (`h_turn`, `h_summ`) |
| 1021 | 63, 76, 100x30 | the turn: "TURN 1", in `human28.fnt` |
| 1027, 1003, 1005, 1004, 1002 | along 432, from 275 | the map's modes: Overlay, Economic, Events, Military, Map; Military lit at the start |
| 1017 | 249, 36, 526x391 | the map |
| 1020 | 28, 110, 196x380 | the empire's panel and its task forces (`h_eicon.wdf`, `h_tfinfo.wdf`) |
| 1014 | 7, 459, 786x134 | the selected system (`h_starmp.wdf`, `h_sysmap.wdf`) |
| 1026, 1025 | 273, 198 and 564, 353 | the overlay's choices (`h_ovrlay.wdf`) and zooming (`h_zoom.wdf`) |
| 1038, 1039, 1040 | 9 to 33, 154 to 499 | the task forces list's scroll bar: not shown with the first turn's two |

The turn is in (24, 101, 189), the options screen's blue, measured; the
words are `lexicon.dic`'s ("Turn", entry 1370, in capitals). The Overlay
and Map buttons' pictures mark what isn't drawn with yellow, not magenta
([formats/wdf.md](../formats/wdf.md#pictures-and-transparency)). Drawn
so, the frame, its buttons and the turn match the screenshot pixel for
pixel, but for the pointer.

### The map

Measured on the screenshot, a small galaxy of 13 by 10 sectors:

- **The grid:** sectors 39 pixels square, in the middle of the map's
  place (from 258, 36), with lines of (0, 0, 8) between them.
- **The backdrop**, `hm_glmap.tga` (526x391, the map's size), is drawn
  from the grid's corner, 9 pixels into the map's place, and cut to it:
  so its right edge is hidden and the place's left 9 pixels are blank
  there.
- **What the empire's scanners reach:** at the first turn the 3 by 3
  sectors round Sol, darker, the backdrop at about a fifth of its
  brightness, with the grid dotted in (24, 73, 173). The save doesn't hold
  it: its explored flags have only Sol's sector. Reluva counts a sector
  round each of the empire's systems and task forces *(inferred: the
  ranges of scanners, and of ships, are the data tables')*.
- **Stars:** each system in the scanners' range is drawn with its star's
  small picture (`s-y-maps.tga` for a yellow star, 10x10, black its
  background), not the 19x19 `s-y-map.tga`. In its sector it's placed by
  its place on the galaxy's small map (`systInfo`, 5 steps a sector), 7
  pixels a step less 3: Sol (2, 3 into its sector) at 11, 18, its
  neighbours Thelka (2, 2) at 11, 11 and Ramatis (3, 2) at 18, 11, each
  matching the screenshot's stars pixel for pixel.
- **Names:** a system the empire has explored has its name under its
  star, centred, in `human9.fnt`, in (0, 251, 255): Sol's, but not
  Thelka's or Ramatis', which are only in range.

## In Reluva

`GalaxyScreen` (`src/engines/botf/ui/galaxy_screen.cpp`) loads the
player's empire's layout, and draws its frame, the map's backdrop, grid,
the scanners' sectors, stars and names, and the turn. The engine opens it
with the chosen slot's save from Load Game, or with `Auto.sav` from
Continue; Escape goes back to the main menu. The unit tests
(`botf.unit.screens`: "galaxy") cover the scanners' range and where stars
and names are drawn.

Not drawn yet, all in the screenshot: the task forces on the map (the
Federation's emblem in the corner of Sol's sector), the orange box round
the selected sector, the coloured borders of the ships' ranges (green,
yellow and red), the empire's panel and task forces, and the selected
system along the bottom.

## Sources and checks

- **Where the facts come from:** `hmaingal.wdf` and its pictures in the
  English `stbof.res`; a screenshot of the game by the user, November
  2026, at the first turn of a new game (Federation, small galaxy, few
  minor races, Simple, random events off), with its save.
- **How it was checked:** drawn by Reluva from that save, the frame and
  turn match the screenshot pixel for pixel, and the map away from what
  isn't drawn yet to within the 16-bit screen's rounding: the backdrop
  where the scanners don't reach, the three stars and Sol's name.

## Open questions

- How far an empire's scanners reach, and why only Sol's sector is
  explored: the systems' and ships' scan ranges (`shiplist.sst`), and
  when a sector counts as explored.
- The grid lines' exact shade, which varies a little with the backdrop
  in the screenshot.
- Larger galaxies: whether the map scrolls, and how zooming changes the
  sectors' size.
