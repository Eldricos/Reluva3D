# LEGO Loco: how pictures are drawn

> **Status:** partial · **Code:** `src/engines/legoloco/pages/draw.cpp`, `src/engines/legoloco/pages/board_viewer.cpp` · **Checked against:** the code and the CD's data; not yet the original running · **Confidence:** from the code unless marked

## Overview

Everything the game shows is a picture from the
[resource file](../formats/resource.md): an 8-bit BMP, cut into frames by
its [definition](../formats/dat.md), and placed on the board's squares.

## How it works

### Frames

In `loco.exe`: `0x42574e`, and the same at `0x42a020` and `0x43753e`.

A picture's frames sit side by side. A frame is as wide as the picture's
width divided by `total_number_of_frames`, rounded down, and as tall as
the picture; pixels left over at the right aren't used. A picture
without a definition is one frame.

The pieces bear this out: in 219 of the 220 definitions with a
`bitmap_occupancy`, a frame is exactly that grid's columns and rows of
16-pixel squares (`building\house104`'s picture is 16 pixels shorter).
The milk float's definition (`vehicles\milkflot.dat`) says 20 frames
where its picture has 16 drawings 24 pixels apart, as the other four
vehicles' say 16: cut by the rule, its frames are 19 pixels wide and fall
across the drawings. Its frame sets are those of a vehicle of eight
directions (`W` to `NW`, frames 0 to 7), where the other vehicles' have
sixteen over sixteen drawings, and its zoom picture is named as the
van's (resource 8199, `zooms\vehicles\van`): an unfinished vehicle
*(inferred)*. The game reads its name with the others' at its start;
it wasn't seen on a board, so how it would look isn't known.

### Colours

Each picture has its own palette: the CD's 1,511 pictures have 1,268
different ones. Colour 0 is transparent: it's magenta (255, 0, 255) in
1,471 of them *(inferred)*.

The game draws at the desktop's colour depth, 16 bits
([running.md](running.md)), so the palettes are never put together:
loading an 8-bit picture (`0x42af30`), it makes the picture a table of
its 256 colours in the screen's own pixels, each colour cut to the
screen's 5-6-5 or 5-5-5 (red and green shifted into place and masked,
blue shifted down 3; the shifts set when it found the screen's format,
`0x45b6a1`), and draws by looking the pixels up in it. Magenta becomes
the screen's magenta, which surfaces are colour-keyed with, so colour 0
shows what's below *(from the code)*. Some pictures share one table,
the first made of those asked for so (`0x48524c`) *(from the code; which
they are isn't known)*.

### Squares and placing

A board square is 16 pixels: the screen saver's boards are 64, 72 and
80 squares wide for screens 1024, 1152 and 1280 pixels wide *(inferred)*.
A piece stands at its board column and row, the top left of its
`physical_occupancy`; its picture's foot is on the bottom of the squares
it fills, and a taller picture rises above them *(inferred)*. Drawn so,
with nearer rows over farther ones, the CD's boards come out whole:
track meets track, and buildings sit on their plots.

### Animation

A frame set runs from its start frame to its end, backwards if the end
is lower, each frame shown for its delay, then holds its end for its
restart delay before the next set
([dat.md](../formats/dat.md#the-frame-set-table)). The game's clock is a
multimedia timer every 28 ms (`timeSetEvent` at `0x406e54`, at 14 ms
resolution, as seen running), which only raises a flag (`0x45c520`);
the main loop, finding it raised, runs one tick of the game
(`0x45c3c0`), whose updates step each animation once (`0x405c40`), so a
delay counts these ticks *(from the code)*. The restart delay counts
seconds, by the C runtime's `time()`.

The station master in the tutorial's bubbles is the exception: he
talks on a window timer of his own, not the tick. His bubble sets
`SetTimer` id `0x54` every 10 ms (`0x45035b`); each `WM_TIMER` for it
(`0x451880`) counts up to 100 and round again (`+0x3064`), and on every
second one (`0x450450`), while his voice's buffer still says it's
playing (`GetStatus`, by `0x40eeb0`), his frame steps from the set's
start to its end and round; once it has stopped, he shows the second
set's start frame. `tutorial\tutor.dat`'s sets, frames 2 to 4 and 2
alone, give the frames; their delay of 5 isn't used *(from the code)*.
In a published recording of the original, on Windows XP, his face
changes about 23 times a second while he speaks, not the 50 the timer
asks for: Windows gives `WM_TIMER` no more often than its clock ticks,
and only when nothing else is queued *(seen, not traced)*.

## In Reluva

The data viewer draws pictures this way
([data-viewer.md](data-viewer.md)): `drawFrame` in
`src/engines/legoloco/pages/draw.cpp` cuts frames by `frameWidth`
(`src/engines/legoloco/formats/object_definition.h`), the boards page
places pieces as above, and the pictures page plays frame sets as the
game steps them, a tick of 28 ms at a time, split frames and the
restart delay too. Its colours are the pictures' own, not cut to 16
bits. The game itself draws in its own code, run in the emulator
([running.md](running.md)).

## Sources and checks

- **Where the facts come from:** the frame rule from the three places in
  `loco.exe` that divide a picture's width by its frame count; the
  colours from its picture loader and its look at the screen's format;
  the animation from its timer, its main loop and the stepper, at the
  addresses given; the
  square's size and the placing from the CD's data: the pieces' grids
  against their pictures, and the boards drawn.
- **How it was checked:** the CD's boards drawn by the viewer, each
  looking whole (`reluva-tool legoloco view <game folder> boards <n>
  <out.png>`). Against a published recording of the original, at 60
  frames a second: its screen changes at most about 36 times a second,
  as ticks of 28 ms give; the station master's talking as above.

## Open questions

- How the milk float would look on a board, if the game ever puts one
  there.
- Which pictures share one colour table.
- How often Windows XP and 98 gave the station master's 10 ms timer its
  `WM_TIMER`, which sets how fast he talks.
