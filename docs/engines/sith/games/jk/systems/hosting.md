# Jedi Knight: running JK.EXE in the emulator

> **Status:** partial · **Code:** `src/engines/sith/tools/host.cpp` · **Checked against:** the retail `JK.EXE` run in Reluva's x86 emulator, and its code · **Confidence:** seen, not traced unless marked

## Overview

The trial of the plan's way B ([PLAN.md](../PLAN.md#two-ways-to-play-the-game)):
the retail `JK.EXE` run as it shipped in Reluva's x86 emulator
([docs/common/x86/](../../../../../common/x86/README.md)), with
`SMACKW32.DLL` from the install, to see how far it gets and what it
needs that the emulator hasn't. It gets through its start-up, reading its
archives, strings, fonts and the menus' pictures, and stops where it
looks for Direct3D's devices: it asks for Direct3D's first interface,
which the emulator doesn't have.

## How it works

### What it imports

`JK.EXE` (1,387,520 bytes, linked 11 September 1997, by Visual C++ 5's
linker, with no relocations) imports DirectDraw, DirectSound, DirectPlay
(by ordinal 4), DirectInput, `smackw32.dll` (9 functions by ordinal),
WINMM (timers, joysticks, the CD's audio through MCI, mixer volumes),
ole32 and the usual KERNEL32, USER32, GDI32 and ADVAPI32. The interfaces
it uses are DirectX 5's, by the identifiers in its data:

| Part | Interfaces |
|---|---|
| DirectDraw | `IDirectDraw`, `IDirectDraw2`; surfaces 1 to 3 |
| Direct3D | `IDirect3D`, `IDirect3D2`; devices 1 and 2; textures 1 and 2; the Ramp, RGB, MMX and HAL devices |
| DirectSound | 3D buffers and listener |
| DirectPlay | `IDirectPlay3`, `IDirectPlayLobby2A` |

`SMACKW32.DLL` plays sound through WINMM's `waveOut`, and asks for
0x400000 as `JK.EXE` does (it can be relocated; `JK.EXE` can't).

### Its start-up, as run

1. It writes `Version` under
   `HKEY_LOCAL_MACHINE\Software\LucasArts Entertainment Company\JediKnight\v1.0`,
   and reads `CD Path`, `InstallType` and `bHighResGraphicsInstall` there,
   the names beside the key in its strings *(from the code)*.
2. It registers its window class (`RegisterClassExA`) and the common
   controls.
3. It opens `Resource\Res1hi.gob` and `Resource\Res2.gob`. For each file
   it wants, it first tries a loose file: at the install's root
   (`C:\ui\jkstrings.uni`), in its `resource` folder, then on the CD
   (`D:\gamedata\resource\ui\jkstrings.uni`); then the archives.
4. It reads `ui\jkstrings.uni`, and says so for a debugger: `Premature
   'END' found after only 787 lines in 'ui\jkstrings.uni'. Check number in
   'MSG xxx' header.`: the count is a limit, as
   [formats/uni.md](../formats/uni.md) has it.
5. It reads the fonts (`small0.sft`, `small1.sft`...) and the menus'
   pictures (`bkEsc.bm`, the arrows, sliders and buttons).
6. At `0x429310` it asks its DirectDraw for `IDirect3D` and lists
   Direct3D's devices with a callback at `0x42ba60`; with no `IDirect3D`
   it returns 0 *(from the code)*. The list of devices it builds
   (`0x421fc0`, `0x4273e0`; entries of 680 bytes) is then empty, and it
   reads through its null pointer at `0x414396`, 51 million instructions
   in.

On the real Windows of the time Direct3D always had its software
devices, so the program never met an empty list.

### Speed

The 51 million instructions ran in 0.1 to 0.2 seconds, 200 to 500
million a second with the emulator's native code: well beyond the
Pentiums of 1997 the program was written for.

## In Reluva

`reluva-tool sith host <install>` runs `JK.EXE` from the install (drive
C) with CD 1's copy as drive D, for up to `-seconds` seconds, and says
what it opened, wrote to the registry, printed for a debugger or showed
in a message box, and where it stopped (registers, and the return
addresses on the stack). `-png` saves the last frame. `reluva-tool sith
disasm <JK.EXE> <address>` shows the program's instructions from an
address.

For the trial, `host.cpp` answers a few things itself, each as Windows
would: no sound card (`DirectSoundCreate`, `waveOut`), no joystick, no CD
audio (MCI), no DirectPlay lobby, no COM classes; a few USER32, KERNEL32
and COMCTL32 calls the emulator lacks (`RegisterClassExA`, the console,
time conversions, painting). None of this is how the game would be run:
it's to see how far it gets.

What it would take to run the game this way, from what the run and the
imports show:

| Need | What | How much |
|---|---|---|
| Direct3D 1 and 2 | `IDirect3D` and `IDirect3D2`, their devices with execute buffers (`IDirect3DExecuteBuffer`, which version 1 draws with), viewports, materials and textures 1 and 2, on the emulator's Direct3D 7 rasterizer | The most; new shared code, for any DirectX 5 game |
| DirectSound | Buffers and 3D buffers on Reluva's mixer | New shared code |
| Windows | About 75 calls the emulator lacks: GDI's palettes, fonts and DIB sections, dialogs, `GetMessageA`, `CompareString`, WINMM's timer events for Smacker | Small each, in the shared stand-ins |
| CD audio | MCI's `cdaudio` device, playing the CDs' music tracks, which copies of their files don't hold | To decide: from a CD image's tracks, or none |
| DirectPlay 3 | Multiplayer sessions, on Reluva's network code | Later |

## Sources and checks

- **Where the facts come from:** the runs of `reluva-tool sith host` on
  the retail install with CD 1's copy; `JK.EXE`'s imports, and the
  DirectX interface identifiers found in its data; its code at the
  addresses above, read with `reluva-tool sith disasm`.
- **How it was checked:** each run's stop explained by the code where
  it stopped, as above.

## Open questions

- Whether `JK.EXE` draws through Direct3D 1's execute buffers or
  Direct3D 2's `DrawPrimitive` (it has both devices' identifiers), and
  what its own software renderer needs of DirectDraw.
- What it does once it has its devices: the menus, a level.
