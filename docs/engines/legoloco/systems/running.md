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
   windows shown.
10. Three movies, each in a movie window of its own, a child of its main
    window (`0x454380`): `art-res\video\legoSpin.avi`, then `IgSpin.avi`,
    then `[Video]`'s `LocoIntr.avi`. Each is opened (`MCIWNDM_OPENA`),
    placed (`MCIWNDM_PUT_DEST`) and played through `mciSendCommandA`
    with `MCI_NOTIFY`, the callback the main window's handle, its low
    word only (`0x454391`). `MM_MCINOTIFY` at its main window
    (`0x420f66`) closes the movie's window and starts the next.
11. Its sound (`0x412c50`): with `[Sound]`'s `SelectBestDevice` (1 by
    default), it lists the devices (`DirectSoundEnumerateA`) and, in its
    callback (`0x412fb0`), makes each that has a GUID and keeps the one
    with the most capabilities (`GetCaps`'s flags; a certified driver
    first). It makes that one, sets the priority level, and sets the
    primary buffer to 22,050 Hz, 16-bit stereo. Then `[Sound]`'s
    `VolumeLow`, `VolumeMed` and `VolumeHigh`.
12. The intro's last step (`0x4208f0` with 7): its two panels hidden and
    their timers killed, and `art-res\video\music.wav` played looping
    through `PlaySoundA`. The game's state (`0x4851f4`) is still 2, in
    which its main loop (`0x463253`) only waits for messages.
13. The front screen: its windows are popups made maximized (style
    `0x87000000`, the place given left unset: Windows ignores it), each
    filling the screen, the front window (`FRONTWINDOWCLASS`) over the
    main one. When the last movie's window is destroyed, the front window
    is painted again (`WM_PAINT`), and draws itself by blits onto the
    primary surface: the LEGO and LOCO logos, the station master, a case
    for one player and one for two, a way back and a tick. It draws its
    own pointer, a hand, where the mouse is.
14. A case chosen and the tick clicked, the game's own loop runs: the
    first layout, and the station master's welcome, its text drawn by
    GDI. (Its main loop, in a state other than 2, peeks messages and
    draws a frame each time the 28 ms timer has set its flag,
    `0x485444`; that it's that path now is *inferred* from the frames.)

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
- `src/engines/legoloco/host/player.cpp`: `Player` runs it on a thread of
  its own, for the engine (`legoloco.cpp`), which shows its 800 by 600
  screen:
  - each frame presented (a blit or flip onto the primary surface, in the
    game's window) is kept for the engine; a movie's frames, drawn into
    the screen's pixels, are presented as they're drawn
    (`DirectX::show`);
  - its sound, DirectSound's buffers with `PlaySoundA`'s music, plays on
    the engine's mixer, as do the movies';
  - the mouse is passed on where it is on the screen (the game draws its
    own pointer, so the system's is hidden), with its buttons, the wheel
    and the keys, when the program looks for a message and finds none or
    when it presents a frame.
- `reluva-tool legoloco run <game folder> -save <folder>` runs the
  program and reports how far it gets: the imports nothing stands in for,
  and where it stopped, with the last stand-ins called (`-log` prints each
  call, `-limit` stops after so many million instructions, `-seconds`
  once the program's clock has passed so many seconds). `-click s x y`
  clicks at (x, y) once the clock has passed s seconds, as often as
  given; `-shot file.ppm` saves the last frame shown.

### How far it gets

The program runs through its start ([above](#its-start-as-it-runs)),
plays its three movies (by the clock: about two minutes;
[mci.md](../../../common/x86/mci.md)), sets up its sound and starts its
music ([dsound.md](../../../common/x86/dsound.md)), and shows its front
screen, about 509 million instructions; then it waits for the player.
Clicked through (the one-player case, then the tick), it plays: the
first layout and the station master's welcome.

```bash
reluva-tool legoloco run <game folder> -save <folder> -limit 400000 -seconds 150 -click 127 365 345 -click 130 718 520 -shot board.ppm
```

DirectPlay is stood in for without a network
([dplay.md](../../../common/x86/dplay.md)), so it finds no network
connection. Of its 115 imports that had nothing at first, 4 are left,
none called so far:

| DLL | Functions | What |
|---|---|---|
| comdlg32, SHELL32 | 2, 1 | the Open and Save dialogs, `ShellExecuteA` |
| USER32 | 1 | `LoadCursorFromFileA` (its animated cursors) |

The rest are stood in for in the shared code: KERNEL32's, ADVAPI32's,
VERSION.dll's and USER32's ([windows.md](../../../common/x86/windows.md)),
GDI32's ([gdi.md](../../../common/x86/gdi.md)), the movie window's and
MCI's ([mci.md](../../../common/x86/mci.md)), and DirectSound's and
`PlaySoundA` ([dsound.md](../../../common/x86/dsound.md)). How much of
the game plays beyond the first layout hasn't been tried yet.

## Sources and checks

- **Where the facts come from:** `loco.exe`'s imports and strings, and
  what it asks for running; `LEGO.INI` as the installer leaves it.
- **How it was checked:** `reluva-tool legoloco run` on the English
  install, its frames saved (`-shot`) and looked at.

## Open questions

- Whether the program runs on without a network past its start: it
  finds none of its four connections, and goes on to its front screen
  and the first layout.
