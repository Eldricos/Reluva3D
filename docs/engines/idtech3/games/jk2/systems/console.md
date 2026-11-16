# Jedi Outcast: the multiplayer console

> **Status:** done · **Code:** `src/engines/idtech3/client/console_drawer.cpp`, `client/play_page.cpp` · **Checked against:** `jk2mp.exe` 1.02a's console drawing, read in its code; Reluva's screens · **Confidence:** from the code unless marked

## Overview

The engine's console, its notify lines and the chat line being typed are
drawn by `jk2mp.exe` itself, not by the game's code: with its own
character grid (`gfx/2d/charsgrid_med`), its `console` shader (the
scrolling stars and the title, in `shaders/gfx.shader`) and the `white`
shader. The GPL Jedi Outcast multiplayer source's `cl_console.cpp`,
`cl_scrn.cpp` and `cl_keys.cpp` were read as a guide to the routines'
shape; every size and colour below was found in 1.02a's code.

## How it works

Positions are in the screen's own pixels unless they're said to be in
the 640 x 480 screen the 2D pictures are drawn in.

- **Small characters** (0x40ef40) are 8 by 16 pixels whatever the
  screen's size: the pixel position is scaled by 640 / width and
  480 / height into the 640 x 480 screen (0x81d828, 0x81d82c). The grid
  is 16 by 16 cells of 1/16 of the texture each, a character's code its
  row (high four bits) and column; only each cell's left half (0.03125
  across, 0x4e4390) is drawn. Spaces aren't drawn.
- **Big characters** (the chat line) are 16 by 16 in the 640 x 480 screen,
  from the same half cells, each with a black shadow 2 down and right.
- **Colours** follow `^0`–`^7` (black, red, green, yellow, blue, cyan,
  magenta, white), from white at the start of each print; a line wrapped
  keeps the colour in force.
- **The width** is the screen's width / 8 - 2 characters. A printed line
  wraps where a word (counted from each character to the next space, at
  most a line's width) would reach the edge, or at the edge itself.
  Because the count starts again at each character, the rest of a cut
  word that no longer fits moves down as well.
- **Each x** is the original's 640 / width added to the position in
  pixels, then rounded down: at 640 x 480 the text starts at 9.

The console (0x406400, `Con_DrawSolidConsole` in the GPL source) slides
at `scr_conspeed` (3) screens a second to half the screen open, or shut.
Open to a fraction *f*:

- the `console` shader over the 640 x 480 screen from the top down to
  *f* x 480 - 2 (nothing if that's under 1);
- a 2-high line under it in (0.509, 0.609, 0.847) (0x406477), the
  `white` shader;
- the version, `JK2MP: v1.02a` (0x4ef690), in the line's colour, ending
  at the screen's right edge, 24 pixels above the console's foot
  (*f* x height pixels);
- the lines printed, the newest 48 pixels above the foot and the rest
  above it, 16 apart; scrolled back, a row of red `^` every four
  characters takes the newest line's place;
- while it has the keys, `]` and the line being typed 32 pixels above
  the foot, at most 78 characters of it shown, scrolled to keep the
  cursor (character 10, blinking every 256 ms) in sight.

Shut, during a match:

- **notify lines:** the last four lines printed within `con_notifytime`
  (3) seconds, from the top of the screen, 16 apart;
- **the chat line** below them: `say:` (or `say_team:`) in big
  characters at x 8, then what's typed from the fifth (or eleventh)
  character's place, at most 30 (or 25) characters shown, and its
  cursor.

Neither is drawn while a menu has the keys (the game's menus, or the
client game's own).

## In Reluva

`consolePictures` lays the console out as the 640 x 480 pictures above,
and the Play page draws them after everything else in the frame, so a
screenshot keeps them as the original's do. Games without
`GameInfo::consoleVersion` (Elite Force so far) keep Reluva's own
console.

Differences:

- The typing line's scroll is worked out from the cursor alone; the
  original's moves only when the cursor leaves the part shown, so after
  moving the cursor back the two can show a long line from different
  places.
- Lines are wrapped as they're drawn, from Reluva's lines as printed; a
  colour set in one print and carried to its next line in the original
  starts white again in Reluva.
- Scrolling back moves by Reluva's lines (four a key), not the
  original's two.

## Sources and checks

- **Where the facts come from:** `jk2mp.exe` 1.02a: the console routine at
  0x406400 (the background, the line's colour, the version and the rows'
  places), the small character routine at 0x40ef40 (its sizes and the
  half cells), the version string at 0x4ef690; the shaders and the grid
  from `assets0.pk3`. The GPL Jedi Outcast multiplayer source as a guide
  to the routines, *(published)* for the big characters, the chat line's
  widths and the typing line's scroll, which weren't traced in 1.02a's
  code.
- **How it was checked:** a unit test (`idtech3.unit.input`, "console")
  lays out the console half open and shut and checks the places, sizes,
  colours and the grid's cells, and the wrapping; in the Play page the
  console, notify lines and chat line were seen drawn this way and kept
  in screenshots (November 2026). Not yet compared with the original's
  screens.
