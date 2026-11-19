# LEGO Alpha Team: running the game's own code

> **Status:** done · **Code:** `src/engines/netimmerse/alphateam/game_process.cpp`, `src/engines/netimmerse/alphateam/player.{h,cpp}` · **Checked against:** the original running in Reluva; recordings of the original · **Confidence:** confirmed unless marked

## Overview

How the game is run: three ways were weighed against what the game's
code is and calls, and the second, B1, chosen: the game's own program and
NetImmerse run in the x86 interpreter, with Windows and DirectX stood in
for. Then how the program was brought to run.

The three ways weighed, the decision and B1's order of work (its steps 1
to 6) are in [PLAN.md](../PLAN.md).

## How it works

### What the game's code is and calls

Measured on the retail files (October 2026) with a PE import census of
every module, the 74 DLLs inside `Bhvr.pac` included.

**The game is mostly compiled code.** Alpha Team is a contraption-puzzle
game. Each puzzle part (`T0001`, `U0188`...) has its own behaviour DLL
in `Bhvr.pac`, and the puzzles are text files (`.PUZ`) that place the
parts. Most missions ship a `_SOLVED.PUZ` too. The game's code adds up to:

| Code | Size |
|---|---|
| `LEGOATeam.exe` and the four `*Comp.dll` | 1.0 MB |
| 74 behaviour DLLs in `Bhvr.pac` | 4.5 MB |

**What that code calls:**

| Called | Unique functions | Notes |
|---|---|---|
| NetImmerse (`Ni*.dll`) | 792 | NiMain 596, NiAnimation 119, NiA3dSound 41, NiCollision 32, NiDirect3d 4 |
| ↳ of which constructors | 58 | 42 NetImmerse classes created directly (NiNode, NiTriShape, NiCamera, NiStream...) |
| ↳ vtables / static data | 19 / 95 | the game subclasses NetImmerse's classes |
| MSVCP60 (C++ library) | 129 | `std::string`, fstreams, locale |

**What everything calls, NetImmerse included**, i.e. what B1 has to supply:

| Library | Functions |
|---|---|
| KERNEL32 | 44: files, file mapping, find-file, critical sections, one event, timers, `LoadLibraryA`/`GetProcAddress`, the CD check |
| USER32 / GDI32 | 27 / 3: one window, its messages, the cursor |
| ADVAPI32 | 5: the registry |
| WINMM | 1: `timeGetTime` |
| DDRAW | 3: `DirectDrawCreate(Ex)` and `Enumerate`. Direct3D 7 is reached through it ("NiDirect3D NetImmerse Version 7.0"). Only `NiDirect3d.dll` uses it |
| DINPUT | 1: `DirectInputCreateA` (LoadComp) |
| OLE32 | 3: `CoCreateInstance` for Aureal A3D (NiA3dSound) and Direct3D |
| MSVCRT | 79: the C library, `_beginthread` (one thread, started by the exe), C++ exceptions (`_CxxThrowException`, LoadComp) |
| MSVCP60 | 156 |

No DirectSound, no other threads, no SSE or MMX needed (the interpreter
reports a Pentium, so any CPU-specific paths stay off).

### Loading and starting the program

- Done: `src/engines/netimmerse/alphateam/game_process.cpp` loads
  `LEGOATeam.exe` and the 9 DLLs it needs from the game folder, each after
  those it imports from: `DDPackedDirFileLib`, `NiMain` (relocated: its
  address is taken), `NiAnimation`, `BaseClassComp`, `BehaviorComp`,
  `NiA3dSound`, `NiCollision`, `NiDirect3d`, `LoadComp`, then the program.
- The process believes it's installed in
  `C:\Program Files\LEGO Media\Games\LEGO Alpha Team\`, which is the
  game folder. What it writes there goes to the target's save folder
  (`GameTarget::savePath`, `GameProcess::setSaveFolder`), and it reads its
  files from the save folder first; it may write nowhere else ([saves.md](saves.md)).
- The CD-ROM drive (D:) has the retail CD's label, `LEGOATEN`.
- `reluva-tool netimmerse at-run <game folder> [-log]` runs all of it and reports how far it
  gets: the imports nothing stands in for, by DLL, and on a failure the
  last instructions (as module+offset), the last stand-ins called, the
  registers and the stack's first words, and the return addresses found on
  the stack. `-files` prints each path the program asks for (and where it
  was found) and each registry value it sets; `-save folder` is its save
  folder (without one it may write nothing). `-from frame` turns the
  tracing that `-watch`, `-calls`, `-log` and `-step` need on only from
  that frame (it's much slower), and `-clock-rate frame@n` makes the fixed
  clock count n instructions a millisecond from that frame on.
- Done: MSVCRT (all 79 imports, `src/emulation/x86/msvcrt.cpp`) and KERNEL32's
  files.
- Done: MSVCP60, as our own DLL ([msvcp60.md](../../../../../common/x86/msvcp60.md)), and the rest of KERNEL32's
  imports (`src/emulation/x86/win32_system.cpp`: system information, the
  performance counter, critical sections and events for one thread, date
  and time text).
- Every DLL's start-up (static constructors and `DllMain`) now runs
  through: 130,057 instructions, NiMain's processor check included.
- Done: USER32 and GDI32 (`win32_user.cpp`), the registry (ADVAPI32) and
  COM start-up (ole32) (`win32_registry.cpp`). Everything the game imports
  is now stood in for except DirectDraw (3 imports) and DirectInput (1).
- What the program does before it needs DirectDraw (`at-run -log` lists
  every stand-in called):
  - checks the CD;
  - registers its window class and makes its window ("LEGO Alpha Team",
    sized with `AdjustWindowRect`), and shows it;
  - confines the pointer to it;
  - finds the `.pac` archives and maps each into memory;
  - asks the desktop's colour depth, then sets the window to 640×480 at
    80,60 (`at-run` reports the window);
  - calls `DirectDrawCreate` from NiDirect3d.
- Done: DirectDraw and Direct3D 7 (`src/emulation/x86/directx/`,
  [directx.md](../../../../../common/x86/directx.md)).
  The game sets its 640×480 16-bit fullscreen mode and makes its renderer:
  - a flipping primary surface with a back buffer;
  - a Z buffer;
  - the device, on the "HAL";
  - 288 multi-texture modes found by its 594 test draws.
  Then it shows its loading screen and plays its attract-mode demo: the
  levels, with lightmaps, water, transparency and animated minifigures,
  under the logo and "Click the mouse to begin!".
- Fixed on the way:
  - `VirtualAlloc` handed out addresses already in use. The behaviour
    DLLs ask for NiMain's and DDPackedDirFileLib's base, 0x10000000.
  - `LoadLibraryA` now loads DLLs from the game folder.
    `LegoLogicComp.dll` is needed only by behaviour DLLs.
  - Export names may be up to 4 KB; one of DDPackedDirFileLib's is 530
    characters.
  - Every DLL stood in for (DDRAW's, MSVCRT's...) has a module handle for
    `GetProcAddress`. NiDirect3d finds `DirectDrawCreateEx` that way.
- `at-run -shots <folder>` saves the frames shown (TGA). 3 billion
  instructions take about 100 seconds and show 74 frames, so the game runs
  at under one frame a second for now; speed is step 6
  ([speed.md](../research/speed.md)).

## In Reluva

### In the engine

- Done: the engine plays the game (`alphateam/player.{h,cpp}`).
  - The program's main loop never returns, so it runs on a thread of its
    own. The engine's thread queues the player's input and takes the
    frames the program shows.
  - The program's thread hands the input to the process at each frame,
    and whenever the program looks for a message and finds none.
  - The mouse is held by the window, and its movement is passed on.
  - F12 pauses the game at its next frame and opens the data viewer.
  - Quitting stops the program with `Cpu::requestStop`; an `exit` of its
    own is told from a failure by `Win32Process::exited`.

## Sources and checks

- **Where the facts come from:** a PE import census of every module of the
  retail files, and `reluva-tool netimmerse at-run`'s reports of the
  running program (above).
- **How it was checked:** the program run to its loading screen and
  attract-mode demo (above).

## Open questions

### Registry keys the game's code names

- Registry keys the game's code names, for later:
  - `HKEY_CLASSES_ROOT\A3d` and the A3D COM class
    `{d8f1eee0-f634-11cf-8700-00a0245d918b}`, which the program registers
    itself;
  - `ALPHABLENDMETHOD` under `HKEY_LOCAL_MACHINE\Software\NVIDIA
    Corporation\RIVA 128\Direct3D`, a driver setting it writes;
  - the installed locales, which LoadComp enumerates;
  - `Software\LEGO Media\LEGO Alpha Team\Version`, read by a behaviour
    DLL, with `DiscPath`, `SoundVolume` and similar names nearby.
