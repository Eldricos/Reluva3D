# GDI

## What it is

`src/emulation/x86/gdi.{h,cpp}`: GDI32's objects and device contexts for
programs run in the [x86 emulator](README.md), and the drawing USER32
does with them, written from Microsoft's documentation of each function.
`x86::Gdi` is made with the Windows process (`Win32Process::gdi()`). A
device context draws straight into pixels in the program's memory: a
bitmap's, a DirectDraw surface's (its `GetDC`,
[directx.md](directx.md)), or the screen's.

## What Reluva supports

- **Objects:** solid and hatched brushes (a hatched one fills as its
  colour), pens (solid or null, of any width), fonts (their height, weight
  and face kept), bitmaps, and the stock objects. `GetObjectA` gives
  `LOGBRUSH`, `LOGPEN`, `LOGFONTA` and `BITMAP`; `DeleteObject` leaves the
  stock objects be.
- **Device contexts:** memory ones (`CreateCompatibleDC`, drawing into
  the bitmap selected), a window's (`GetDC`, `BeginPaint`), and those the
  engine makes for pixels of its own (`createDc`: a DirectDraw surface's).
  A window's draws in its client area of the pixels `Gdi::screen` gives
  (a DirectDraw primary surface's), clipped to it (one reaching off the
  screen's left or top starts at the edge instead); `GetDC(0)` draws on
  the whole screen; nowhere without `Gdi::screen`. Text and background colours,
  the background mode and the current position are kept.
- **Pixels:** 8 bits (the colour nearest in the target's palette), 16
  (by its masks; 5-6-5 without), 24 and 32.
- **Drawing:** `FillRect` and `FrameRect` (a brush, or a system colour's
  index + 1), `DrawEdge` (raised and sunken, in Windows 98's 3D colours),
  `MoveToEx` and `LineTo` (the end left out), `SetPixel`, `Ellipse`
  (filled with the brush, outlined with the pen), and `StretchBlt` (the
  nearest pixel; raster operations other than `SRCCOPY` drawn as it, and
  reported).
- **Text:** `DrawTextA`, with line feeds, word breaks, `&` prefixes,
  centring and right alignment, a single line centred upright or at the
  bottom, and `DT_CALCRECT`. It's drawn in Reluva's own small font
  (`src/graphics/font.h`), scaled to a whole multiple near the font's
  height: Windows' fonts aren't Reluva's to have, so text looks
  different, and takes different room.
- **Bitmaps from files:** `LoadImageA` with `LR_LOADFROMFILE`, stretched
  to the size asked if one is. Bitmaps, icons and cursors from a
  program's resources aren't loaded.
- Not yet: regions and clipping, other shapes (`Rectangle`, polygons),
  `BitBlt`, `TextOutA`, palettes of GDI's own.

## How it works

Objects and device contexts are kept by handle in `x86::Gdi`, apart from
Windows' other handles (from `0x5a000000` and `0x5d000000`; the stock
objects are `0x5a000000` and on). Drawing is done by the stand-ins
themselves, pixel by pixel, through the processor's memory, so what a
program locks and reads afterwards (a surface's pixels) is what was
drawn.

## Sources and checks

- **Where the facts come from:** Microsoft's documentation of each
  function and structure, and Windows 98's standard colour scheme for the
  system colours.
- **How it was checked:** `tests/common/gdi.cpp` (`common.unit.gdi`)
  makes objects and selects them, fills, draws lines, pixels and an
  ellipse into a 16-bit target, measures text, loads an 8-bit bitmap from
  a drive and stretches it across, and draws through a window's DC with
  and without a screen, and through a window's own, within its client
  area.
- **Confidence:** published unless marked.

## Used by

- **`legoloco`**: LEGO Loco's program, which imports 19 of GDI32's
  functions and draws text with `DrawTextA` from 25 places
  ([running.md](../../engines/legoloco/systems/running.md)).
