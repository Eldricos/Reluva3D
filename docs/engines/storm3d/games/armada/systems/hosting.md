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
given it (`-input`) and the CD (`-cd`, `-toc`), it goes on through the
single-player screen and the mission panel, past the game's CD check,
into "Premonitions", the mission first on the Federation's panel (its map
`bzn\fed1.bzn`), which it runs in 3D: its opening scenes, its interface
and objectives, and play once they're closed.

## How it works

### What the program is and needs

`Armada.exe` is 1.8 MB of code (built 10 March 2000). It imports 526
functions from 18 DLLs:

| DLL | Functions | From |
|---|---|---|
| `MSVCP60.dll` | 156 | Reluva's rebuilt C++ library ([msvcp60.md](../../../../../common/x86/msvcp60.md)) |
| `MSVCRT.dll` | 114 | Reluva's stand-ins |
| `USER32.dll`, `KERNEL32.dll`, `GDI32.dll`, `WINMM.dll`, `ADVAPI32.dll`, `ole32.dll`, `comdlg32.dll`, `SHELL32.dll`, `IMM32.dll`, `COMCTL32.dll`, `AVIFIL32.dll` | 198 | Reluva's stand-ins, as far as they go |
| `DSOUND.dll` | 1 | Reluva's silent DirectSound ([directx.md](../../../../../common/x86/directx.md)) |
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
  find: the most direct trace of what the game reads and when. With them,
  the files it reads from inside its ZFS archives (`assets.zfs >
  fed.odf`), which it reads itself: each archive entry a read reaches,
  found from where the read was in the archive.
- **The frames it shows**, written as PNG. With `-every-ms`, while a
  dialog of the program's is up (its in-game menu) and nothing is
  presented, the screen as it is is written once a whole period has gone
  by without a frame.
- **Where it stops, and why.**
- **A frame's draws**, with `-draws <frame> <file>`: each Direct3D draw
  that makes it, with its texture (where its pixels are, and its size),
  where its vertices are in memory
  its blending (the D3DBLEND factors, 0 when off) and depth test and
  write, and its vertices as Storm3D gives them, already on the screen:
  x, y, depth, 1/w, colour, and texture coordinates. Storm3D lights its
  vertices itself, so their colours are its lighting.
- **What writes a place in memory**, with `-watch <address> <bytes>`:
  each store into it, by an instruction or by a native function (as
  `memcpy`), with where it was made, the registers then, and the stack's
  top words, among them its callers' return addresses.
- `-draws-ms <ms> <file>` writes the draws of the first frame shown from
  that moment of the program's clock, for when the frame's number isn't
  known Either file starts with the frame's number and its
  time on the program's clock (`frame 4123 at 45512 ms`).
- **Its memory**, with `-dump <frame> <file>` or `-dump-ms <ms> <file>`
  (as often as wanted): the whole of it, written as that frame is shown
  or at that moment of its clock, to look for what it keeps there (its
  camera, say).

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
[-every-ms N] [-save dir] [-input file] [-cd folder] [-toc file]
[-cd-label name] [-fonts folder] [-clock instructions|real] [-dump frame
file] [-dump-ms ms file] [-draws frame file] [-draws-ms ms file] [-watch
address bytes]` runs the program for `N` milliseconds of its clock
(20,000 by default), writes every `N`th frame (and the last) to `dir`,
or with `-every-ms` the first frame after each `N` milliseconds of its
clock, named by the moment (`ms012300.png`), and the program's own files to the save folder
(by default under the system's temporary folder). It prints the imports
no stand-in answers, the files opened, the frames shown, and where it
stopped.

The program's clock follows the instructions it runs, 300,000 a
millisecond, as a Pentium II of 300 MHz of the game's time *(guessed)*,
and moves on a millisecond each time every thread waits; its date starts
at noon (UTC) on 15 March 2000. So a run comes out the same each time,
its input landing at the same moments, and the time it takes is the
emulator's, not the game's: waiting costs nothing. `-clock real` runs on
the real clock and date instead.

The CD is the disc's files on D: (`-cd`), its volume label `ARMADA`
unless `-cd-label` says otherwise *(guessed: the disc's label isn't
known)*, and its table of contents (`-toc`) from the disc's cue sheet or
Alcohol 120% media descriptor (`.mds`), for the game's CD check, which
reads it through MCI and plays the audio track.

Text is drawn in the fonts the program asks for (Arial Bold), looked for
in the folders given (`-fonts`, which can be a Windows fonts folder),
then in the system's: Windows' own files if they're there, else ones
made to the same widths, as Liberation Sans for Arial. So the letters
may differ from Windows' a little, but not where text breaks or how wide
it is.

The input file is what the player does, a line each, by the program's
clock: `<ms> move <x> <y>` (the pointer to a place), `<ms> motion <dx>
<dy>` (the mouse moved by that much), `<ms> click [<x> <y>]` (the left
button, released 100 ms later, at the place given or where the pointer
is), `<ms> down` and `<ms> up` (the left button pressed and let go, for
a drag, with a place or not), `<ms> rclick` (the right button, as click)
and `<ms> key <virtual key> [held ms]` (100 ms unless said), on the
640 × 480 screen; `#` starts a comment. A key goes with its scan code
as a US keyboard gives it, the grey keys (the arrows' block, Insert, the
keypad's /) as extended keys: `Input.map` tells them from the keypad's.

The front end's dialogs follow Windows' cursor, so `move` and `click`
with a place work there. A mission doesn't: it keeps a pointer of its
own, and each frame takes the cursor's distance from the screen's centre
as the mouse's movement, then puts the cursor back in the centre
(`GetCursorPos`, `SetCursorPos(320, 240)`); its buttons come as window
messages. Its pointer moves half as far as the mouse *(seen, not traced:
it may be the game's mouse speed setting)*, and stops at the screen's
edges, where the map scrolls. So in a mission the pointer is moved with
`motion`, which the host breaks into steps the screen holds, 50 ms
apart: first far into a corner, which puts it at a known place, then to
where it's wanted. The corner scrolls the map, though, so where that
matters the pointer is moved from where it starts instead: after a
skirmish launched from the Game Setup screen, it stood at (320, 349),
found by matching the cursor's sprite in the frames. A `move` in a
mission is taken as motion too, by its distance from the centre.

A skirmish: Multiplayer, a name typed (`PLAYER`), Instant Action, then
the Game Setup screen's launch:

```text
4000 move 552 18
5000 click 552 18    # Multiplayer
8000 move 320 110
8500 click 320 110   # the name's field
9000 key 80
9300 key 76
9600 key 65
9900 key 89
10200 key 69
10500 key 82
11500 move 320 310
12000 click 320 310  # Instant Action
15000 move 320 460
16000 click 320 460  # LAUNCH
```

To start "Premonitions", the mission first on the Federation's panel
(its map `bzn\fed1.bzn`), skip its two opening scenes (Escape, virtual
key 27), and close its objectives:

```text
4000 move 300 300
5000 click 90 18     # Single Player
9000 move 130 150
10000 click 130 150  # the Federation
13000 move 268 395
14000 click 268 395  # OK
60000 key 27         # the Romulans' scene
70000 key 27         # the Federation's
90000 motion -2000 -2000  # the mission's pointer into the top-left corner
92000 motion 636 584      # to the objectives' OK, (318, 292)
95000 click
```

```bash
reluva-tool storm3d host <game folder> -ms 110000 -frames frames -input mission.txt -cd <disc folder> -toc Armada.mds
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

- Clicks by the clock can come before the screen they're for; a run that
  goes further will want input that waits for what's shown.
- Where a mission's pointer starts, and why: (320, 349) after the
  skirmish's launch, seen once; a campaign mission's isn't known, so its
  pointer is pushed into a corner first (which scrolls the map there).
- Fonts: without Windows' own (`-fonts`), text is in look-alikes with
  the same widths.
- Its sound: DirectSound is there, but silent.
