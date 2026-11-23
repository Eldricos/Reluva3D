# Star Trek: Armada: the original, hosted as a reference

> **Status:** partial · **Code:** `src/engines/storm3d/tools/host.cpp` · **Checked against:** the original running in Reluva's x86 emulator · **Confidence:** confirmed unless marked

## Overview

To study what the original does, and later to check Reluva's Armada
against it, the retail `Armada.exe` runs in Reluva's x86 emulator, with
its own DLLs and Reluva's stand-ins for Windows. It's a tool for working
on the engine, never how the game is played: the plan is a native Armada
([PLAN.md](../PLAN.md)).

So far it runs the program's start-up: the configuration, the display
mode, the sprite tables and their textures, up to reading `events.odf`
([research/startup-trace.md](../research/startup-trace.md)).

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
stand-ins ([directx.md](../../../../../common/x86/directx.md)). 148 of the
imports have no stand-in yet; a call to one stops the run, naming it, so
they're added in the order the program needs them.

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

## In Reluva

`reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N]
[-save dir]` runs the program for `N` milliseconds of its clock (20,000
by default), writes every `N`th frame (and the last) to `dir`, and the
program's own files to the save folder (by default under the system's
temporary folder). It prints the imports no stand-in answers, the files
opened, the frames shown, and where it stopped.

`reluva-tool storm3d exe-survey <game folder>` lists the program's
exports, which of the missions' and the computer player's DLLs import
each, its imports, and its classes ([research/](../research/README.md)).

## Sources and checks

- **Where the facts come from:** the program running in the emulator;
  its imports and exports from its PE headers.
- **How it was checked:** each stand-in added for it is tested on its own
  in `common.unit.windows` ("a hosted program's needs", and `msvcp60` for
  the C++ library), and the engines hosting other programs (`nfs`,
  `netimmerse`) pass their tests with them.

## Open questions

- The C++ library's string streams (`basic_istringstream`,
  `basic_stringstream`, `strstream`), which the program builds inline: their
  layouts have to be measured from its code before Reluva's library can
  have them. The run stops there now.
- Its sound, while DirectSound isn't stood in for.
