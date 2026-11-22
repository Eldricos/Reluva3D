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
across the drawings. Whether the game shows it so isn't known.

### Colours

Each picture has its own palette: the CD's 1,511 pictures have 1,268
different ones. Colour 0 is transparent: it's magenta (255, 0, 255) in
1,471 of them *(inferred)*. How the game brings the palettes to its
8-bit screen isn't known yet.

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
is lower ([dat.md](../formats/dat.md#the-frame-set-table)), each frame
shown for its delay. The game's clock is a multimedia timer every 28 ms
(`timeSetEvent` at `0x406e54`, at 14 ms resolution); that a delay counts
these ticks is *(guessed)*.

## In Reluva

The data viewer draws pictures this way
([data-viewer.md](data-viewer.md)): `drawFrame` in
`src/engines/legoloco/pages/draw.cpp` cuts frames by `frameWidth`
(`src/engines/legoloco/formats/object_definition.h`), and the boards
page places pieces as above. The game itself isn't drawn yet.

## Sources and checks

- **Where the facts come from:** the frame rule from the three places in
  `loco.exe` that divide a picture's width by its frame count; the
  square's size and the placing from the CD's data: the pieces' grids
  against their pictures, and the boards drawn.
- **How it was checked:** the CD's boards drawn by the viewer, each
  looking whole (`reluva-tool legoloco view <game folder> boards <n>
  <out.png>`); not yet against the original running.

## Open questions

- How the milk float is drawn, given its definition's frame count.
- How the game maps each picture's palette to the screen's.
- Whether a frame set's delay counts the 28 ms ticks.
