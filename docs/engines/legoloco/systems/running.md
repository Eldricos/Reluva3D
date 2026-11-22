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

### Its start, as it runs

Seen running in Reluva (`reluva-tool legoloco run`):

1. The C runtime's start-up, then a splash window: dialog 113 of its
   resources, made modeless (`CreateDialogParamA` at `0x462ed0`), a
   static control showing bitmap resource 117.
2. Its version (`VerQueryValueA`, `\StringFileInfo\080904B0\FileVersion`),
   the registry's `Path`, set again to its folder, and `LEGO.INI`
   ([below](#where-it-finds-its-files)); `0x4068d0` fails the start, with
   "An error occurred while loading", unless `Res`'s folder is there.
3. A mouse (`GetSystemMetrics(SM_MOUSEPRESENT)`), else "No mouse
   installed, this application requires a mouse."
4. Its folders for postcards under `art-res\PostBag` (`Easter`, `Sort\In`,
   `Sort\Out`, `Sort\Bag`, `AlbIndex`, `Album`, `Att_In`, `Att_Out`),
   made if they're not there; the player's name (`LEGO.INI`'s `[USER]`
   `Name`, else the Windows user's) and `<name>.usr`; `NetSettings.dat`.
5. Its window, class "LEGO LOCO", and [`EE.INI`](../formats/ee-ini.md)'s
   events.
6. DirectDraw (`0x45b516`): its cooperative level normal, for no window
   (it runs in a window on the desktop, at the desktop's colour depth);
   a primary surface and others, colour-keyed with the desktop's
   magenta (`0xf81f` at 16 bits); a clipper made for the primary when it
   has none (`GetClipper` failing with `DDERR_NOCLIPPERATTACHED`).
7. The resource file read through its own code, then its game loop: an
   event, "GameLoop", and the multimedia timer at 28 ms (`timeSetEvent`
   at `0x406e54`).
8. DirectPlay: `DPLAYX.dll`'s ordinal 4, `DirectPlayLobbyCreateA`, whose
   lobby it uses without looking at what the call returned
   (`IDirectPlayLobby3A`, `0x45e576`). Then `DirectPlayCreate` for each
   service provider, asking each `IDirectPlay4A` for the local machine's
   address (`GetPlayerAddress` for player 0, `0x45ef7c`): one that can't
   give it isn't there. The serial ports, `COM1` then `COM0`, for its
   modem and serial connections.
9. Its threads, a timer of 120 ms (`SetTimer`), the mouse captured, its
   windows shown, and the movie window (`MCIWndRegisterClass`).

### Where it finds its files

- The registry's `HKEY_LOCAL_MACHINE\SOFTWARE\Intelligent Games\LEGO
  LOCO`, `Path`: the program's folder, as the installer writes it.
- `Exe\LEGO.INI`, read through `GetPrivateProfileStringA` (`0x446070`):
  `[DIRECTORIES]`'s `Res` (the `art-res` folder) and `ResFile` (the
  [resource file](../formats/resource.md)), `[Video]`'s `Dir` (the intro
  movie) and `[Locale]`'s `Language`. It keeps its own state there too:
  `[WINDOW_ATTRIBUTES]` (the window's place), `[PROCESS]` `CleanExit`,
  `[CLIENT]` `NextId`, `[MOUSE]` `Setting1`, `[USER]` `Name`. The installer writes the folder it
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

The program runs through its start ([above](#its-start-as-it-runs)), about
500 million instructions, and stops at the movie window,
`MCIWndRegisterClass`, which nothing stands in for yet. DirectPlay is
stood in for without a network ([dplay.md](../../../common/x86/dplay.md)),
so it finds no network connection. Of its 115 imports that had nothing at
first, 9 are left:

| DLL | Functions | What |
|---|---|---|
| DSOUND | 2 | DirectSound, by ordinal |
| WINMM, MSVFW32 | 2, 1 | `PlaySoundA`, the MCI movie window |
| comdlg32, SHELL32 | 2, 1 | the Open and Save dialogs, `ShellExecuteA` |
| USER32 | 1 | `LoadCursorFromFileA` (its animated cursors) |

The rest are stood in for in the shared code: KERNEL32's, ADVAPI32's,
VERSION.dll's and USER32's ([windows.md](../../../common/x86/windows.md)),
and GDI32's ([gdi.md](../../../common/x86/gdi.md)). Nothing is shown yet:
the engine doesn't show the program's frames.

## Sources and checks

- **Where the facts come from:** `loco.exe`'s imports and strings, and
  what it asks for running; `LEGO.INI` as the installer leaves it.
- **How it was checked:** `reluva-tool legoloco run` on the English
  install.

## Open questions

- Whether the program runs on without a network past its start: it
  finds none of its four connections, and goes on as far as the movie
  window.
