# LEGO Alpha Team: running the game's own code

How the game is run: three ways were weighed against what the game's
code is and calls, and the second, B1, chosen: the game's own program and
NetImmerse run in the x86 interpreter, with Windows and DirectX stood in
for. Then how the program was brought to run.

## What the game's code is and calls

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

## The options

### A. Reimplement the game natively

Pros:
- Fastest at run time, and the most portable (no interpreter).
- Free to improve: widescreen, higher resolutions, modern input.
- The data side is needed anyway ([the data viewer](data-viewer.md)), so it isn't wasted.

Cons:
- 5.5 MB of compiled C++ to rewrite: 74 puzzle parts' behaviours, plus
  the rules, physics and menus in the four `*Comp.dll`.
- A contraption puzzle only works if every part behaves exactly as the
  original's did (timings, momentum, electricity). Small differences
  break the shipped puzzles' solutions.
- Needs a complete native NetImmerse too: scene graph, animation,
  collision, NIF loading.
- By far the most work, with the least certain result.

### B1. Run all the original code in the x86 interpreter, NetImmerse included; stand in for Windows and DirectX

Pros:
- Faithful by construction. Every behaviour DLL, NetImmerse's own physics,
  collision and animation run as shipped. The `_SOLVED.PUZ` files give a
  ready test: load one, run it, see it solve.
- The Windows surface is small and fully documented: about 170 plain
  functions plus three COM areas (DirectDraw/Direct3D 7, DirectInput,
  A3D). Elite Force's Win32 process and PE loader already cover the
  loading, relocation, imports and much of KERNEL32.
- Every interface it implements is public (Microsoft's API docs, the
  DirectX 7 SDK docs, Aureal's A3D SDK docs). No NetImmerse internals
  need to be guessed.
- The work carries over to any later LEGO game built the same way, and to
  other Win9x/DirectX 7 games.
- It can become native piece by piece. Any export of `NiMain.dll` (matrix
  maths, bounds, culling) can later be swapped for a native function where
  profiling shows the time goes.

Cons:
- Speed. The interpreter runs about 50 million instructions a second.
  The game asked for a Pentium II 233, roughly 4 to 6 times that, and
  here NetImmerse's per-frame scene-graph work runs interpreted too. It
  needs a decoded-instruction cache (already noted in x86.md as the next
  speed-up) or a simple JIT, plus native replacements for hot NiMain
  exports.
- MSVCP60 and MSVCRT aren't in the install. MSVCRT can be stood in for
  natively (C functions). MSVCP60 is C++ with object layouts
  (`std::string`, streams) that must match VC6's. That could be a native
  stand-in, or a small replacement DLL built for x86 with clang, like the
  tests' DLL, and run in the interpreter.
- New interpreter features:
  - C++ exceptions, i.e. structured exception handling dispatch, which is
    listed as a known limit today;
  - one extra thread (`_beginthread`), run cooperatively;
  - COM objects (vtables of native functions);
  - `GetVolumeInformationA` answering the CD check.
- Direct3D 7's fixed-function pipeline (render states, texture stages,
  vertex formats) must be mapped onto `gfx::Renderer3D`, which may need a
  few additions.

### B2. Run only the game's code in the interpreter, with a native NetImmerse

Pros:
- NetImmerse's heavy per-frame work runs natively, so speed isn't a worry.
- Little of Windows or DirectX is needed: just what the game's own
  modules call.

Cons:
- The boundary is C++, not C. The game creates 42 NetImmerse classes
  itself, subclasses them through their vtables and reads 95 of their
  static data items. Its compiled code also reads and writes their fields
  directly through inlined accessors. A native NetImmerse would need every
  class's binary layout and vtable order to match the original exactly,
  with nothing to check against but the binaries.
- 792 functions to reimplement with matching behaviour, including NIF
  streaming (the game registers its own loaders with `NiStream`) and
  NetImmerse's collision and animation, which the puzzles' physics depend
  on.
- It amounts to rewriting NetImmerse to be binary-compatible: more work
  than B1 and less faithful, while still needing the interpreter.

## The decision

**Decision: B1.** It's the only option whose hard parts are all documented
public interfaces, it keeps the puzzles' physics exact, and the
`_SOLVED.PUZ` files give an automatic check. Its main risk, speed, has
known remedies that can be measured as the work goes. The data viewer's native
readers ([data-viewer.md](data-viewer.md)) (`.pac`, NIF, TGA, fonts) are still worth having, for tools and
the viewer, and as the start of native replacements later.

B1's order of work:

1. `.pac` reader and the viewer's images, fonts and puzzles
   ([data-viewer.md](data-viewer.md)).
2. Load `LEGOATeam.exe` and every DLL in the interpreter, with stand-ins
   for the libraries above. Then let `DDPackedDirFileLib`'s loader pull a
   behaviour DLL out of `Bhvr.pac`. `reluva-tool x86-run` shows how far it
   gets.
3. MSVCRT, MSVCP60, C++ exceptions and the extra thread, until the game
   reaches its first `DirectDrawCreateEx`.
4. DirectDraw/Direct3D 7: the menus first, then a puzzle. Done in
   software first (see [directx.md](../../../../../common/x86/directx.md)); `gfx::Renderer3D` can come later, for
   speed.
5. DirectInput on `System` events, then A3D on the mixer.
6. Speed: an instruction cache, then native NiMain hot spots, measured
   against a puzzle's frame time.

## Loading and starting the program

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
- Done: MSVCRT (all 79 imports, `src/common/x86/msvcrt.cpp`) and KERNEL32's
  files.
- Done: MSVCP60, as our own DLL ([msvcp60.md](../../../../../common/x86/msvcp60.md)), and the rest of KERNEL32's
  imports (`src/common/x86/win32_system.cpp`: system information, the
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
- Done: DirectDraw and Direct3D 7 (`src/common/x86/directx/`,
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

## In the engine

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

## Registry keys the game's code names

- Registry keys the game's code names, for later:
  - `HKEY_CLASSES_ROOT\A3d` and the A3D COM class
    `{d8f1eee0-f634-11cf-8700-00a0245d918b}`, which the program registers
    itself;
  - `ALPHABLENDMETHOD` under `HKEY_LOCAL_MACHINE\Software\NVIDIA
    Corporation\RIVA 128\Direct3D`, a driver setting it writes;
  - the installed locales, which LoadComp enumerates;
  - `Software\LEGO Media\LEGO Alpha Team\Version`, read by a behaviour
    DLL, with `DiscPath`, `SoundVolume` and similar names nearby.
