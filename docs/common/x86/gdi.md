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
- **The system colours** (`Gdi::sysColor`, `COLOR_` index), of one of
  two schemes (`Gdi::colours`): Windows 98's standard one (grey faces, a
  navy highlight and caption, a teal desktop), or Windows XP's blue one
  (pale faces, a bright blue highlight and caption, a blue desktop)
  *(published, from public listings of each scheme's values; XP's
  highlight matched to a recording of LEGO Loco)*. Everything painted
  here takes its colours from them.
- **The desktop and frames**, as Windows 98 paints them: `paintDesktop`
  fills what no window covers in the desktop's colour
  (`COLOR_BACKGROUND`) when a window goes, and `paintFrame` a window's
  frame when it's shown, as an active window's:
  - a border of 2 pixels or more raised in two rings, light above and
    left, dark below and right, a border of 1 a black line;
  - a caption bar, a pixel short of the caption, shaded from
    `COLOR_ACTIVECAPTION` to `COLOR_GRADIENTACTIVECAPTION`, the title in
    an 8-point bold sans serif in `COLOR_CAPTIONTEXT`; with a system menu
    its buttons at the right, raised, 16 by 14 (close, and with
    `WS_MINIMIZEBOX` or `WS_MAXIMIZEBOX` maximize, or restore when
    maximized, and minimize), their crosses, windows and bars drawn
    here. The window's icon isn't drawn: its picture isn't read
    (`Win32Process::captionArea` and `captionButtonArea` place them,
    [windows.md](windows.md));
  - a sunken edge inside, two rings or one; scroll bars with their
    arrows, pale track and thumb.

  `paintEdit` paints an edit control's client area (Windows' `EDIT`
  class's painting), its selection in `COLOR_HIGHLIGHT`.
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
  bottom, and `DT_CALCRECT`. Windows' fonts aren't Reluva's to have, so:
  - a proportional font (Arial, the System font, and every face but the
    fixed ones) is drawn in the player's own Arial, where the engine asks
    for it (`Gdi::arial`) and it's installed (`ttf::installedArial`: the
    system's font folders, and the player's, searched for Windows' and
    macOS's file names and the core fonts packages'); else in Liberation
    Sans (`third_party/liberation/`, built into Reluva), whose letters
    are as wide as Arial's in its design. Bold from weight 600,
    unsmoothed as Windows 98 draws text (a pixel set where the letter
    covers half of it or more);
  - its height as `LOGFONT` has it: above 0 the cell, below 0 the em;
    lines are the cell apart. Arial is drawn so, hinted by its own
    programs ([truetype.md](../truetype.md)) *(not yet seen against
    Windows: Reluva's machines haven't Arial)*; Liberation Sans at 0.93
    of the cell, as Windows' hinted Arial is narrower at the sizes
    programs use than Liberation Sans in the same cell *(inferred:
    matched to a recording of LEGO Loco, whose lines then break as
    there)*;
  - a fixed-pitch font (`FIXED_PITCH`, Courier, Fixedsys, Terminal,
    Lucida Console, and the stock `OEM_FIXED_FONT`, `ANSI_FIXED_FONT`
    and `SYSTEM_FIXED_FONT`) in Reluva's own small font
    (`src/graphics/font.h`), scaled to a whole multiple near its height.
  The stock `SYSTEM_FONT`, a device context's own, is bold, with a cell
  of 16.
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
  system colours. Liberation Sans's sizes against Arial's from a
  recording of LEGO Loco, played on Windows.
- **How it was checked:** `tests/common/gdi.cpp` (`common.unit.gdi`)
  makes objects and selects them, fills, draws lines, pixels and an
  ellipse into a 16-bit target, measures text in a fixed font and in
  Arial, draws it, loads an 8-bit bitmap from
  a drive and stretches it across, and draws through a window's DC with
  and without a screen, and through a window's own, within its client
  area; a framed window's border is painted raised as it's shown, a
  bordered one's as a line, and the desktop, teal, where they were once
  they've gone. Its `look` case checks both schemes' colours, text
  drawn in an Arial given in Liberation Sans's place, sized as Windows
  sizes it, and finding a font's file among folders.
  `tests/common/windows.cpp`'s `caption` case checks a captioned
  window's frame painted (its border, its bar, its title, a button) and
  its buttons. LEGO Loco's windowed mode was looked at in both schemes.
- **Confidence:** published unless marked.

## Used by

- **`legoloco`**: LEGO Loco's program, which imports 19 of GDI32's
  functions and draws text with `DrawTextA` from 25 places
  ([running.md](../../engines/legoloco/systems/running.md)).
