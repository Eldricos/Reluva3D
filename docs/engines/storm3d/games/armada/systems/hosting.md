# Star Trek: Armada: the original, hosted as a reference

> **Status:** partial · **Code:** `src/engines/storm3d/tools/host.cpp` · **Checked against:** the original running in Reluva's x86 emulator · **Confidence:** confirmed unless marked

## Overview

To study what the original does, and later to check Reluva's Armada
against it, the retail `Armada.exe` runs in Reluva's x86 emulator, with
its own DLLs and Reluva's stand-ins for Windows. It's a tool for working
on the engine, never how the game is played: the plan is a native Armada
([PLAN.md](../PLAN.md)).

It runs the program's start-up (the configuration, the display mode, the
sprite tables and their textures, `events.odf`) and its main menu, which
it shows as on Windows and keeps showing, its Bink panels playing
([research/startup-trace.md](../research/startup-trace.md)). With clicks
given it (`-input`), it goes on through the single-player screen and the
mission panel into loading the first mission, up to the game's CD check,
which needs the disc's table of contents.

## How it works

### What the program is and needs

`Armada.exe` is 1.8 MB of code (built 10 March 2000). It imports 526
functions from 18 DLLs:

| DLL | Functions | From |
|---|---|---|
| `MSVCP60.dll` | 156 | Reluva's rebuilt C++ library ([msvcp60.md](../../../../../common/x86/msvcp60.md)) |
| `MSVCRT.dll` | 114 | Reluva's stand-ins |
| `USER32.dll`, `KERNEL32.dll`, `GDI32.dll`, `WINMM.dll`, `ADVAPI32.dll`, `ole32.dll`, `comdlg32.dll`, `SHELL32.dll`, `IMM32.dll`, `COMCTL32.dll`, `AVIFIL32.dll` | 198 | Reluva's stand-ins, as far as they go |
| `DSOUND.dll` | 1 | the host: `DirectSoundCreate` fails, as on a machine with no sound device |
| `WonInterface.dll`, `binkw32.dll`, `Strlkup.dll`, `IMAGEHLP.dll` | 57 | the install's own DLLs, loaded and run |

DirectDraw and Direct3D 7 it loads itself, at run time, from Reluva's
stand-ins ([directx.md](../../../../../common/x86/directx.md)). 69 of the
imports have no stand-in yet (most of them KERNEL32's, WINMM's and
MSVCRT's, and AVIFIL32's movies); a call to one stops the run, naming it,
so they're added in the order the program needs them.

### Setting up the process

- The program believes it's installed in `C:\Program Files\Activision\Star
  Trek Armada`, the installer's default: reads there come from the
  install's folder.
- What it writes (its log, `Logs\logfile.txt`) goes to a folder of the
  host's own, never into the install.
- Its threads run on host threads of their own, with the stand-ins for
  threads, waits and thread-local storage that come with them.
- Each DLL of the install it imports from is loaded after those it
  imports from, then started; the program last.

### What the run shows

- **The files it opens, in order**, and those it looks for and doesn't
  find: the most direct trace of what the game reads and when.
- **The frames it shows**, written as PNG.
- **Where it stops, and why.**

Two facts it settled about the files: textures are read from
`Textures\RGB\` (at least with the display mode it chose), and after each
`.tga` the program opens `.1`, `.2` and on until one is missing: its
mipmap levels ([formats/textures.md](../formats/textures.md)).

And how the front end is built: the main menu is a modal dialog (template
0x123, with no controls) run by the program's own dialog procedure. It
paints the background (`bitmaps\main\mainbk.bmp`) and the labels with
GDI, each label blitted through a mask against the colour key
`0xff00ff`, while four Bink movies play in panels through DirectDraw.
On Windows both land on the display; in Reluva, GDI draws on DirectDraw's
primary surface for the same reason.

## In Reluva

`reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N]
[-save dir] [-input file] [-cd folder] [-cue file] [-cd-label name]` runs
the program for `N` milliseconds of its
clock (20,000 by default), writes every `N`th frame (and the last) to
`dir`, and the program's own files to the save folder (by default under
the system's temporary folder). It prints the imports no stand-in
answers, the files opened, the frames shown, and where it stopped.

The input file is what the player does, a line each, by the program's
clock: `<ms> move <x> <y>`, `<ms> click <x> <y>` (the left button,
released 100 ms later) and `<ms> key <virtual key>`, on the 640 × 480
screen; `#` starts a comment. To start the Federation's first mission:

The CD is the disc's files on D: (`-cd`), its volume label `ARMADA`
unless `-cd-label` says otherwise *(guessed: the disc's label isn't
known)*, and its table of contents from the disc's cue sheet (`-cue`),
for the game's CD check, which reads it through MCI.

The clicks go by the program's clock, which is real time, so a click
can come before the screen it's for is ready, when loading is slow: give
them room.

```text
4000 move 300 300
5000 click 90 18     # Single Player
9000 move 130 150
10000 click 130 150  # the Federation
12000 move 268 395
13000 click 268 395  # OK
```

`reluva-tool storm3d exe-survey <game folder>` lists the program's
exports, which of the missions' and the computer player's DLLs import
each, its imports, and its classes ([research/](../research/README.md)).

## Sources and checks

- **Where the facts come from:** the program running in the emulator;
  its imports and exports from its PE headers.
- **How it was checked:** each stand-in added for it is tested on its own
  in `common.unit.windows` ("a hosted program's needs", "a dialog",
  "GDI's drawing", and `msvcp60` for the C++ library), and the engines
  hosting other programs (`idtech3`, `nfs`, `netimmerse`) pass their tests
  with them. The run's frames were looked at: the background is
  `mainbk.bmp` as it is, the labels sit on it without their key colour,
  and the panels move from frame to frame. It hasn't been set beside the
  game running on Windows.

## Open questions

- The CD check before a mission reads the disc's table of contents
  through MCI; the copy of the disc here has its files only, so its cue
  sheet is still to come.
- Text: GDI measures text but draws none (there are no fonts), so what
  the menu writes with `DrawTextExA` (in red, at the start) isn't seen.
- Its sound, while DirectSound isn't stood in for.
