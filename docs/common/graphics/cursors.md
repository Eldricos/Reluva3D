# Windows' pointer files (`.cur`, `.ico`, `.ani`)

## What it is

The pictures Windows shows as the mouse pointer: a cursor (`.cur`), one
picture with the point that's the mouse's place (its hotspot); an icon
(`.ico`), the same without one; and an animated cursor (`.ani`), a RIFF
form of cursors shown in turn. Read by `src/graphics/cursor_file.cpp`
(`readCursorFile`) into `Cursor` pictures in RGBA
(`src/graphics/cursor.h`).

## What Reluva supports

- Cursors and icons of 1, 4, 8, 24 and 32 bits a pixel, uncompressed,
  up to 256 by 256; of a file's pictures, the one nearest 32 by 32, the
  pointer's size.
- Animated cursors whose frames are cursor or icon files, with their own
  rates and order or the header's.
- Not read: pictures stored as PNG, compressed ones, and animated cursors
  of raw pictures; the file's refused, with why.
- `standardArrow()`: an arrow of Reluva's own drawing, for programs that
  show Windows' standard cursors, whose pictures aren't Reluva's to have.

## The format

Numbers little-endian.

### Cursors and icons

A directory, then the pictures it points to:

| Offset | Size | Value |
|---|---|---|
| 0 | 2 | 0 |
| 2 | 2 | 1 for an icon, 2 for a cursor |
| 4 | 2 | how many pictures |
| 6 | 16 each | an entry for each picture |

An entry:

| Offset | Size | Value |
|---|---|---|
| 0 | 1 | width (0 for 256) |
| 1 | 1 | height (0 for 256) |
| 2 | 1 | colours, 0 for 256 or more |
| 3 | 1 | 0 |
| 4 | 2 | a cursor's hotspot x; an icon's planes |
| 6 | 2 | a cursor's hotspot y; an icon's bits a pixel |
| 8 | 4 | the picture's size |
| 12 | 4 | where it is in the file |

A picture is a `BITMAPINFOHEADER` whose height counts two bitmaps, then
its colours (for 8 bits or fewer: as many as the header says, else all
the bits allow; blue, green, red and a spare byte each), then the colour
bitmap and the 1-bit mask, each bottom row first, rows padded to 4
bytes. Where the mask's bit is set the screen shows through: unchanged
where the colour's black, inverted where it isn't. A 32-bit picture
whose fourth bytes aren't all 0 has its own alpha there, and its mask
isn't used.

In Reluva an icon's hotspot is its middle *(guessed)*, and a pixel that
would invert the screen is black, as a picture can't invert what's under
it: as it shows on a light screen.

### Animated cursors

`RIFF 'ACON'` holds:

| Chunk | Contents |
|---|---|
| `anih` | 36 bytes: its size, frames, steps, width, height, bits, planes (those four unused when the frames are files), the default rate, flags (1: the frames are cursor or icon files; 2: there's a `seq ` chunk) |
| `rate` | each step's time, in sixtieths of a second (optional: the default rate for each) |
| `seq ` | each step's frame (optional: the frames in order) |
| `LIST 'fram'` | the frames, each an `icon` chunk holding a whole cursor or icon file |

The steps run in turn, then again from the first. Some files' RIFF
size counts its own 8 bytes too, running past the file's end; Reluva
cuts it to the file.

## Sources and checks

- **Where the facts come from:** Microsoft's documentation of the icon
  and cursor resources and `BITMAPINFOHEADER`; the animated cursor's
  chunks as they're published (the RIFF form registered as `ACON`, and
  its description in Microsoft's Multimedia Programmer's Reference).
  The RIFF size counting its own header is LEGO Loco's `busy.ani`.
- **How it was checked:** `tests/common/cursors.cpp`
  (`common.unit.cursors`) reads files made in the test: a cursor of 24
  bits with its mask, one of 32 with its alpha, an icon of 4 bits from
  its palette, an animated cursor's frames at their times with and
  without rates, and files cut short or naming a frame they haven't.
  LEGO Loco's `CURSORS\busy.ani` reads as six frames of 32 by 32 in 24
  bits, a tenth of a second each.
- **Confidence:** published unless marked.

## Used by

- **The x86 emulator's Windows** (`Win32Process::pointer`,
  [windows.md](../x86/windows.md)): `LoadCursorFromFileA`'s files, and
  the arrow for Windows' own cursors.
- **`legoloco`**: LEGO Loco's busy pointer while it saves.
