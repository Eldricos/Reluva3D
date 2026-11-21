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

### The empire's panel

Down the left, in the panel's place (28, 110), four layouts one under
another, measured on the screenshot: `hinfobox.wdf` at its top,
`h_eicon.wdf` 90 down, `hinf_tf.wdf` 130 down and a `h_tfinfo.wdf` row
for each task force from 147 down, 40 apart.

- **The empire's information** (`hinfobox.wdf`): the "FEDERATION INFO"
  bar is its button's picture (`h_info.tga`). Its text box (1018, at 45,
  140, `human15.fnt`) has three lines 17 pixels apart (the font's 16 and
  one): "TOTAL CREDITS" and the credits, "CREDITS PER TURN" and the
  credits a turn with its sign, and the dilithium a turn with its sign,
  "DILITHIUM" 6 pixels after it (so after "+1", and after "0" in another
  screenshot), and the last turn's events, "No Events". The words are in
  orange (247, 121, 33), the figures in green (173, 227, 115), the
  figures ending 6 pixels short of the box's right edge; the words are
  `lexicon.dic`'s (entries 1361, 194, 239, 833). The figures are the
  empire's (`empsInfo`, [formats/sav.md](../formats/sav.md)).
- **The other empires' emblems** (`h_eicon.wdf`): `i_c26.tga`,
  `i_f26.tga`, `i_k26.tga`, `i_r26.tga` (26x30), each at the top of its
  place (1050 for the Cardassians to 1054 for the Romulans; the player's
  has none), across its middle.
- **The task forces' bar** (`hinf_tf.wdf`): its backing picture,
  `hm_bktfi.tga`, is black but for a tab at its left, and the game shows
  the background through the black; its tab is `hm_tab22.tga` where the
  layout names `hm_tab20.tga` (in the screenshot; `hm_tab32.tga` is the
  same picture).
- **A task force's row** (`h_tfinfo.wdf`): the empire's emblem
  (`i_h26.tga`) across the middle of its place, and two lines in purple
  (99, 77, 181), 17 apart: its mission ("ENGAGE" for 6 and "AVOID" for 5,
  `lexicon.dic`'s 658 and 652) and its first ship's class
  (`shiplist.sst`'s name: "Science Vessel", "Colony Ship"), in the order
  `GWTForce` lists them.

Drawn so from the save, the panel matches the screenshot pixel for pixel,
and the screenshot five turns on (2,449 credits, +92). Two screenshots
of a game loaded from the first save show "2 Events", and one "0
DILITHIUM" with a destroyer queued, where Reluva shows what the save holds
and "No Events" *(not known yet: the events, and the dilithium the queue
uses)*.

## In Reluva

`GalaxyScreen` (`src/engines/botf/ui/galaxy_screen.cpp`) loads the
player's empire's layout, and draws its frame, the map's backdrop, grid,
the scanners' sectors, stars and names, the turn, and the empire's
panel. The engine opens it
with the chosen slot's save from Load Game, or with `Auto.sav` from
Continue; Escape goes back to the main menu. The unit tests
(`botf.unit.screens`: "galaxy") cover the scanners' range, where stars and
names are drawn, and the panel's parts.

Not drawn yet, all in the screenshot: the task forces on the map (the
Federation's emblem in the corner of Sol's sector), the orange box round
the selected sector, the coloured borders of the ships' ranges (green,
yellow and red), and the selected system along the bottom.

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
- The panel's events, and its dilithium with a ship queued; the words for
  the other missions; a task force's second line when it has several
  ships.
