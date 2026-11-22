# LEGO Loco: running the game's own program

> **Status:** partial · **Code:** `src/engines/legoloco/host/game_process.cpp`, `src/engines/legoloco/tools/run.cpp` · **Checked against:** the original program running in Reluva, as far as it gets · **Confidence:** from the code unless marked

## Overview

The game is played by running its own `loco.exe` in Reluva's x86
emulator ([docs/common/x86/](../../../common/x86/README.md)), with what it
asks of Windows and DirectX stood in for: the plan's choice
([PLAN.md](../PLAN.md)). This page says how the program is set up, and
how far it gets.

## How it works

### What the program is and calls

`loco.exe` is one program of 1.1 MB, built with Visual C++ 5 and its C
runtime linked in; it loads no DLL of the game's
([the first survey](../research/first-survey.md)). It has no relocations,
so it's loaded at its own address, 0x400000. It starts threads
(`CreateThread`) and a periodic multimedia timer of 28 ms (`timeSetEvent`
at `0x406e54`), which wait on events.

### Where it finds its files

- The registry's `HKEY_LOCAL_MACHINE\SOFTWARE\Intelligent Games\LEGO
  LOCO`, `Path`: the program's folder, as the installer writes it.
- `Exe\LEGO.INI`, read through `GetPrivateProfileStringA` (`0x446070`):
  `[DIRECTORIES]`'s `Res` (the `art-res` folder) and `ResFile` (the
  [resource file](../formats/resource.md)), `[Video]`'s `Dir` (the intro
  movie) and `[Locale]`'s `Language`. The installer writes the folder it
  installed to into them (`C:\Program Files\LEGO Media\Constructive\LEGO
  LOCO\...`); the CD's copy names `d:\loco\...`.

## In Reluva

- `src/engines/legoloco/host/game_process.cpp`: `GameProcess` sets up the
  process:
  - the game's folder is drive C:, the program `C:\Exe\Loco.exe`, and what
    it writes goes to the save folder (`GameTarget::savePath`), read back
    from there first;
  - the registry's `Path` is `C:\Exe\`;
  - the save folder's `Exe\LEGO.INI`, which the program reads first, is
    the copy's own with its folders made C:'s
    ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#by-choice));
  - its threads run on host threads of their own
    (`Win32Process::useHostThreads`), and DirectDraw is stood in for by
    the shared `x86::DirectX`.
- `reluva-tool legoloco run <game folder> -save <folder>` runs the
  program and reports how far it gets: the imports nothing stands in for,
  and where it stopped, with the last stand-ins called (`-log` prints each
  call, `-limit` stops after so many million instructions).

### How far it gets

The C runtime's start-up runs through, and the program starts its own
static constructors. It stops at its first GDI call, `CreateSolidBrush`
(from `0x44806e`), about 24,000 instructions in. Still to stand in for, of
its 115 imports that had nothing at first:

| DLL | Functions | What |
|---|---|---|
| USER32 | 54 | dialogs, rectangles, painting, timers, scroll bars, `LoadImageA`, `DrawTextA` |
| GDI32 | 19 | brushes, pens, fonts, device contexts, drawing |
| DSOUND, DPLAYX | 2, 2 | DirectSound and DirectPlay, by ordinal |
| WINMM, MSVFW32 | 2, 1 | `PlaySoundA`, the MCI movie window |
| comdlg32, SHELL32 | 2, 1 | the Open and Save dialogs, `ShellExecuteA` |

KERNEL32's, ADVAPI32's and VERSION.dll's are all stood in for now, in the
shared code ([windows.md](../../../common/x86/windows.md)).

## Sources and checks

- **Where the facts come from:** `loco.exe`'s imports and strings, and
  what it asks for running; `LEGO.INI` as the installer leaves it.
- **How it was checked:** `reluva-tool legoloco run` on the English
  install.

## Open questions

- Whether the program accepts being in `C:\Exe\` rather than the
  installer's folder, past its start: it reads `Path` and `LEGO.INI`, but
  hasn't run far enough to show it.
