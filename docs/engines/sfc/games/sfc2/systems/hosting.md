# Star Trek: Starfleet Command II: running the game's own program

> **Status:** partial · **Code:** `src/engines/sfc/host/game_process.cpp` · **Checked against:** the English 1.0 program run in Reluva's x86 emulator · **Confidence:** from the code unless marked

## Overview

Reluva means to run the game's own `StarFleet2.exe` in its x86 emulator,
supplying the parts of Windows and DirectX it calls
([PLAN.md](../PLAN.md#decisions)). This page is the hosting spike's
record, the plan's gate: how far the program gets, what it asks for,
and how fast it runs.

It gets through its start-up and reads its first files, then stops in
its 3D start-up test, asking DirectDraw for Direct3D 3, which Reluva
doesn't have yet. No frame is drawn.

## How it works

What the program does as it starts, in the order the hosted run shows.
The addresses are in the English 1.0 `StarFleet2.exe`.

### The C runtime's start-up

From its entry point (0x6c6a9d), Visual C++'s C runtime, linked into the
program: thread-local storage, its heap, the locale (it asks for the
ANSI code page), the time zone. About 15.8 million instructions, then
WinMain.

### Its Windows version, and InterlockedCompareExchange

The program keeps InterlockedCompareExchange behind a wrapper of its own,
`tOSVersion::_OS_InterlockedCompareExchange` (named in the error it
builds at 0x52830d, with `OSVersion.cpp` and line 0x143). Told it's
running on Windows XP, it asks KERNEL32 for `InterlockedCompareExchange`
by name, and throws `nException::tInvalidAccess` when it can't have it.
It also loads `OSSpecific98.dll`; the install has a `95` and a `98`,
each exporting `OS_InterlockedCompareExchange`, so it picks one by the
Windows version *(inferred)*.

### Its files and settings

It reads `Assets/specs/ftrlist.txt` first, then looks six times each for
the folders `dmp3dDLL_Release`, `DLL` and `DRIVERS` in its own (the
install has `Drivers`, with Miles' sound providers), then reads
`SFC.INI` and `assets/strings/strings.txt`. Files are opened with the C
runtime's `open`, which closes a handle whose type `GetFileType` doesn't
say is a disk file. The loader at 0x62f580 opens a file whole through a
reader object, and throws a byte (0) when the reader can't open it.

### Its window and DirectDraw

It makes its window (with a menu, which it later takes off), then:

1. `DirectDrawCreate`, and from it `IDirectDraw2`; it sets the
   co-operative level and makes a surface, asking it for
   `IDirectDrawSurface3` and `IDirectDrawSurface4`.
2. Through `DirectDrawEnumerateExA`, each device's DirectDraw, asking
   it for `IDirectDraw4` and its display mode.
3. `IDirectDraw4::QueryInterface` for `IID_IDirect3D3`
   ({bb223240-e72b-11d0-...}), twice.

When that fails, its 3D test fails, and it throws its error type
`tErrInfo` holding "Line: 149, Module: ...\shellApp.cpp" and
"fTest3D.mInit() failed". That's where the hosted run stops, after about
44.06 million instructions.

### Errors as C++ exceptions

The program reports failures by throwing C++ exceptions: the two types
above, and plain values. All three seen so far are on its way to
failing, and none was caught; whether it also throws and catches in
normal running isn't known yet.

## In Reluva

- **`GameProcess`** (`src/engines/sfc/host/game_process.h`): a
  `Win32Process` with the install as `C:\SFC2\`, read-only; host threads
  (`useHostThreads`); the shared DirectDraw, Direct3D 7 and DirectInput
  stand-ins; and the Windows functions the shared layer lacks, in
  `src/engines/sfc/host/system.cpp`: interlocked operations, global
  memory, locale and clock queries, INI files (`GetPrivateProfileString`,
  reading `sfc.ini`'s comments after a value as comments), disk space,
  the window's menus and longs, GDI's text calls as no-ops, and a
  `RaiseException` that names the C++ type thrown and the text in a
  `tErrInfo`.
- **Shared fixes the spike made** (`src/emulation/x86/`): semaphores
  (`CreateSemaphoreA`, `ReleaseSemaphore`, and waiting on them), and a
  `GetFileType` for files opened through the path mapping.
- **Commands:** `reluva-tool sfc host <game folder>` runs the program
  headless and reports what it looks for, the imports nothing stands in
  for, where it stopped, and how many instructions it ran; `-calls
  <address>` lists every call into a stand-in from that address on, with
  the interface each `QueryInterface` asks for; `-frame <out.png>` saves
  the last frame. `reluva-tool sfc disasm <game folder> <start> <end>`
  disassembles the program.
- **Not yet:** the game doesn't run in the launcher; nothing past the
  3D test.

## Sources and checks

- **Where the facts come from:** the hosted run of the owner's English
  1.0 `StarFleet2.exe`: the stand-ins called, the files looked for, and
  the exceptions' types and texts as the program builds them; the
  program's code at the addresses given, disassembled; the exception
  layout of Visual C++ (a type's name at its type descriptor's +8), as
  published write-ups on reversing Visual C++ programs describe it.
- **How it was checked:** each run repeated after each change, the
  instruction count steady (44,063,247 three times running; a few hundred
  more when threads change over differently).

### The spike's numbers

| What | Measured |
|---|---|
| Instructions to the 3D test | 44.06 million |
| Speed | 60 to 62 million instructions a second (the shared JIT on, 4 cores, one busy) |
| Imports | 111 of the program's not stood in for at the stop: 37 KERNEL32 (wide-character files, consoles, processes, file mappings), 10 USER32 (dialogs, clipboard, `GetMessageA`), 3 GDI32 (DIB sections), 25 WSOCK32 and 1 DSOUND (by ordinal), 25 Miles (`mss32.dll`), 5 COMDLG32, 4 ADVAPI32, `DirectDrawCreateClipper` |

How fast the game needs to run once it's in a mission isn't measured
yet: nothing past start-up has run.

## Open questions

- Whether the program throws and catches C++ exceptions in normal
  running: if so, Reluva's emulator has to dispatch them, which it
  doesn't yet.
- What the folders `dmp3dDLL_Release`, `DLL` and `DRIVERS` are looked for,
  and what it loads from them.
- How many instructions a second a mission needs.
