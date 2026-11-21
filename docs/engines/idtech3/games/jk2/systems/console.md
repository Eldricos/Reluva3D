# Jedi Outcast: the console

> **Status:** done · **Code:** `src/engines/idtech3/client/console_drawer.cpp`, `client/console_state.cpp`, `client/play_page.cpp`, `client/sp_play_page.cpp` · **Checked against:** `jk2mp.exe` 1.02a's console drawing, read in its code; `jk2sp.exe` 1.02a's toggle and version; Reluva's screens · **Confidence:** from the code unless marked

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
  magenta, white; any other character after `^` but `^` itself picks by
  its low three bits), from white at the start of each print
  (`CL_ConsolePrint`, 0x406040, 0x4060ac); the colour goes on past a
  line's end to the print's next line, and a line wrapped keeps it too.
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
  the foot, 78 characters of it shown from its scroll, and the cursor
  (character 10, or 11 while typing over; blinking every 256 ms).

**Scrolling back** (0x4067f0 on, from `Console_Key`, 0x408d6c on) is by
the lines as wrapped: Page Up two back (no further than the oldest
kept), Page Down two on, Ctrl+Home to the oldest, Ctrl+End to the
newest. A line printed moves the view along only when it's at the
newest (`Con_Linefeed`, 0x405fe4), so scrolled back it stays on the
lines it shows; opening the console and Enter leave it where it is.
`clear` empties it and goes to the newest (0x405bd0).

**The line being typed** (1.02a's `Field_KeyDownEvent` 0x408710 and
`Field_CharEvent` 0x408840, the older Quake III ones) keeps a scroll,
the first character shown, moved a character at a time: on one as a
character typed or Right takes the cursor past the shown width, back
one as Left or Backspace takes it before the scroll. Home and End move
only the cursor; Ctrl+A goes to the start (scroll 0), Ctrl+E to the end
(scroll the length less the width); Ctrl+C clears the line, Ctrl+V and
Shift+Insert paste, Insert flips typing over. As it's drawn
(`Field_Draw`, 0x4084c0), its end counted as a character, all of it is
shown if it fits; else from the scroll, which is pulled back, and kept,
where the line ends before the shown width does. At most 255 characters.
History brings back a line as it was left, its cursor and scroll too.
The chat line is the same field, 30 characters wide (25 for team chat).

Shut, during a match:

- **notify lines:** the last four lines printed within `con_notifytime`
  (3) seconds, from the top of the screen, 16 apart;
- **the chat line** below them: `say:` (or `say_team:`) in big
  characters at x 8, then what's typed from the fifth (or eleventh)
  character's place, at most 30 (or 25) characters shown, and its
  cursor.

Neither is drawn while a menu has the keys (the game's menus, or the
client game's own).

### The console key

Multiplayer's engine opens the console only with Shift held: the
console key alone, while the console is shut, does what it's bound to
instead (1.02a, `~` or `` ` ``, 0x4099dc). While it's open the key alone
shuts it. 1.03 and 1.04 have the key under Escape as `CONSOLE`, whatever
character it gives (scan code 0x29), opening the console the same way,
but alone it does nothing: it's never bound or sent to the menus
(0x409cc2, 0x409cd2; [syscalls.md](syscalls.md), Patches). Typing,
`` ` `` and `~` are never typed into the line (0x409a00).

### Single player

`jk2sp.exe` draws its console the same way, with the same character
grid and shaders, its version `JK2: v1.02a` (0x4d56b4). Its default
config binds `` ` `` and `~` to `toggleconsole` (0x405200), which flips
the console's key catcher, clears the line typed and the notify lines;
the game isn't paused under it. Every line typed is a command, a
leading `/` or `\` left out.

## In Reluva

`ConsoleState` keeps what both Play pages share: the lines printed (the
last thousand, each print stream keeping its unfinished line), the
notify lines (the last four), the line being typed with its history and
how far it's dropped down. `consolePictures` lays the console out as the 640 x 480 pictures above,
and the Play page draws them after everything else in the frame, so a
screenshot keeps them as the original's do. Games without
`GameInfo::consoleVersion` (Elite Force so far) keep Reluva's own
console.

The single-player Play page has the console too: what the game, the
client game and the menus print, and Reluva's own messages (a save,
a load, a command's error); the console key opens it over the game or
the menus, Esc or the key shuts it, and each line typed runs as the
page's commands do (`map`, `save`, `load`, `echo`, `clear`... and the
game's own). Drawn as jk2sp.exe's for Jedi Outcast
(`GameInfo::spConsoleVersion`), Reluva's own for Elite Force. Seen
running in Reluva's window: dropped down over the main menu and over
`kejim_post`, where `god` gets the game's answer.

Lines are kept as printed and wrapped as they're drawn; a line carried
past a print's newline starts with its colour, and one continued from
an earlier print goes back to white, so they're drawn in the original's
colours. Scrolling counts the rows each line wraps to at the console's
width.

The multiplayer Play page opens the console with Shift and the console
key, as above; Elite Force's opens it with the key alone, as its
engine does.

Differences: Ctrl+V and Shift+Insert don't paste (Reluva has no
clipboard).

## Sources and checks

- **Where the facts come from:** `jk2mp.exe` 1.02a: the console routine at
  0x406400 (the background, the line's colour, the version and the rows'
  places), the small character routine at 0x40ef40 (its sizes and the
  half cells), the version string at 0x4ef690, the printing
  (0x406040), the line feed (0x405fe4), the scrolling (0x4067f0 to
  0x40686a) and its keys (0x408d6c on), `clear` (0x405bd0), the typing
  field's keys, characters and drawing (0x408710, 0x408840, 0x4084c0);
  the shaders and the grid from `assets0.pk3`. The GPL Jedi Outcast
  multiplayer source as a guide to the routines, *(published)* for the
  big characters and the chat line's widths, which weren't traced in
  1.02a's code.
- **How it was checked:** a unit test (`idtech3.unit.input`, "console")
  lays out the console half open and shut and checks the places, sizes,
  colours and the grid's cells, and the wrapping; another ("console
  state") the scrolling by wrapped rows and its keys, the colours carried
  and reset, the field's scroll, Ctrl+A and typing over; in the Play page the
  console, notify lines and chat line were seen drawn this way and kept
  in screenshots (November 2026). Not yet compared with the original's
  screens.
