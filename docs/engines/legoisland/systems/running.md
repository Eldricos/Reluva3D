# LEGO Island: running the game's own code

> **Status:** partial · **Code:** `src/engines/legoisland/host/game_process.cpp`, `src/engines/legoisland/tools/run.cpp` · **Checked against:** the original program running in Reluva's x86 interpreter · **Confidence:** confirmed unless marked

## Overview

How the game is to be run: its own program, `ISLE.EXE`, and its code,
`LEGO1.DLL`, in Reluva's x86 interpreter, with Windows and DirectX stood
in for ([PLAN.md](../PLAN.md#decisions)). This page says what the code
calls, how it's loaded, and how far it gets so far.

## How it works

### What the game's code calls

From the two files' import tables (English 1.0). Neither imports a C
runtime DLL: each has its own built in. `ISLE.EXE` imports from
`LEGO1.DLL` (57 functions, by their C++ names), DirectSound, WinMM,
ADVAPI32 (the registry), GDI32, USER32 and KERNEL32. `LEGO1.DLL` imports
from DirectDraw, Direct3D Retained Mode (`d3drm.dll`), DirectSound,
DirectInput, WinMM (the timer, MIDI streams, the joystick), GDI32, USER32
and KERNEL32.

What Reluva's emulator doesn't stand in for yet, 43 functions
(DirectSound is stood in for: [directsound.md](../../../common/x86/directsound.md);
so are the timers, window settings, semaphores and named mutexes it
calls: [windows.md](../../../common/x86/windows.md)):

| DLL | Functions | What they are |
|---|---|---|
| `d3drm.dll` | 2 | `Direct3DRMCreate` (Retained Mode, all of it reached through the object it makes) and `D3DRMCreateColorRGBA` |
| KERNEL32 | 14 | file mapping by name, `CreateProcessA` and `OpenProcess`, locales, `MoveFileA`, `VirtualQuery`, `VirtualLock`, the 16-bit file functions (`_hread`, `_hwrite`, `_llseek`) |
| GDI32 | 13 | text (fonts, `ExtTextOutA`, measuring), palettes, `StretchDIBits` |
| WinMM | 14 | MIDI streams and devices, the joystick |

Whether each is called, and when, isn't known until the program gets
that far.

### Loading and starting

- The process believes it's installed in `C:\Program Files\LEGO Island\`,
  and the registry holds what the installer writes under
  `HKEY_LOCAL_MACHINE\SOFTWARE\Mindscape\LEGO Island`: `diskpath`, the
  install folder by its short name (`C:\PROGRA~1\LEGOIS~1`), and
  `cdpath`, the CD's drive (`D:`)
  ([game-files.md](game-files.md#where-the-game-looks)).
- The install folder, by either name, and drive D: are all the game
  folder, which an import fills with both the installer's files and the
  CD's `LEGO\SCRIPTS`. What the program writes there goes to a save
  folder, if one is given; else writing isn't allowed.
- `LEGO1.DLL` is loaded at its own address (0x10000000), then `ISLE.EXE`
  (0x400000). The program's threads run each on a host thread of its own
  (`Win32Process::useHostThreads`), as its streamer's threads wait on
  each other.

### How far it gets

- `LEGO1.DLL`'s start-up (its C runtime and static constructors) runs to
  its end: 15,500 instructions.
- `ISLE.EXE`'s C runtime starts, then the program looks for a window of
  its own already open (`FindWindowA`), then calls `DirectSoundCreate`.
- **It needs DirectSound to start at all.** With `DirectSoundCreate`
  answering as on a machine with no sound card (`DSERR_NODRIVER`), the
  program shows "Lego Island Error: "Lego Island" is not detecting a
  DirectSound compatible sound card. Please quit all other applications
  and try again." and exits. With the stand-in, it makes the DirectSound
  object, releases it, and goes on.
- It then reads its settings from the registry (about 15 values), makes
  its window, and sets up DirectSound: the primary buffer, its format, and
  the 3D listener.
- It makes DirectDraw and lists its display modes, then asks it for
  `IDirect3D2` (`{6aae1ec1-662a-11d0-889d-00aa00bbb76a}`), DirectX 5's
  Direct3D ([directx.md](../../../common/x86/directx.md)), and lists its
  devices.
- It then makes DirectDraw again, takes the display (exclusive and
  fullscreen) and sets its mode, makes its primary surface and a clipper
  for its window, makes a Z buffer and attaches it, and makes its
  Direct3D device (`IDirect3D2::CreateDevice`). It clears its surfaces,
  which shows the first frame: black.
- Then it makes Retained Mode ([d3drm.md](../../../common/x86/d3drm.md)),
  as `IDirect3DRM2`, and its scene: frames, a device on its Direct3D
  device, a viewport, three lights (`CreateLightRGB`), each added to a
  frame of its own, the frames placed (`AddTransform`) and joined
  (`AddChild`), the viewport's planes and field, the device's dither,
  texture quality and render mode.
- It goes on to its main loop: each pass reads the messages
  (`PeekMessageA`), the time, and DirectInput's keyboard, and shows its
  frame by blitting its back buffer to its window (`Blt`), black so far.
  Meanwhile its streaming threads open and read `ISLE.SI`, `JUKEBOX.SI`,
  `SNDANIM.SI` and `WORLD.WDB` through the 16-bit file functions
  (`OpenFile`, `_hread`, `_llseek`), waiting on each other with
  semaphores. It looks for its saved games, `History.gsi` and
  `Players.gsi`, in `C:\` (a fresh game has none). It doesn't open
  `INTRO.SI`.
- Its C runtime opens `WORLD.WDB` (`CreateFileA`), asks what kind of
  handle it has (`GetFileType`), and, told "unknown", closes it at once,
  as Visual C++'s runtime does with a handle that isn't a disk file, a
  device or a pipe: Reluva's stand-in said so for every file. Unread,
  the world database left the game's map of models empty, and the first
  plant it made, "plant0" from the model "flwrred", wasn't found: a null
  pointer written through in `LEGO1.DLL` at 0x100264d9, after 105,847,482
  instructions. Told a file is on a disk, as Windows does, the program
  reads it.
- It goes on to load its models: 55 textures on surfaces
  (`CreateTextureFromSurface`), 400 meshes of 1,280 groups, their
  vertices, colours, materials and textures, and clones of some of them,
  placed in frames.
- Then it reads `ACT1INF.DTA`, `INFOMAIN.SI`, `IMAININF.DTA` and
  `INTRO.SI`, and **plays its intro**: the LEGO Media logo, the LEGO
  logo, Mindscape's, then the flight over the island, its Smacker movies
  drawn through DirectDraw, about as fast as they should go (in 108
  seconds of this machine's time, 2,500 million instructions, it got
  through the logos and most of the 82-second intro movie, the game
  timing its movies by the clock). Each frame it also clears, renders and
  updates its 3D viewport.
- Run to 4,000 million instructions, it doesn't stop: 16,560 frames.
- Played in the engine, a click skips the intro and it opens the
  Information Center, its room drawn in 2D and the Infomaniac, its
  host, in 3D; run to 6,000 million instructions, 69,716 frames, it
  doesn't stop. Its clicks are 2D controls (`MxControlPresenter`s in
  `INFOMAIN.SI`, each a bitmap whose drawn pixels are where it's
  clicked, such as the door's `Door.bmp`, put at 553, 206) or, where no
  control is, a pick in the 3D scene (`IDirect3DRMViewport::Pick`).
- What the Information Center's clicks do: the portraits play each
  character's film; the book opens the register, where a name is
  spelled from letters; the left arrow leads to the elevator's lobby,
  and the right arrow to the room of high scores, its 3D score cube
  turning. In the elevator, whose window shows the island in 3D, the
  third floor's button rides up to the observation deck: `ISLE.SI`'s 2D
  rooms `ElevOpen`, `Observe`, `SeaView` and `ElevDown`. The lobby's
  left arrow leads to the room of doors (`INFODOOR.SI`), its revolving
  door the way out to the island; clicked, it plays an offscreen line of
  the Infomaniac's (`iic037in`, object 500) and doesn't open: he says a
  character has to be chosen first, by dragging its picture to where
  it's to go on the map. Dragged, a portrait lights up, its character's
  3D figure follows the pointer, and the place under it on the map
  lights up. Before that a player has to be registered (`iic007in`
  says so): a name spelled in the book and its tick, at the top of the
  right-hand page, clicked; the program then writes `Players.gsi` to
  `savepath`, the save folder.
- **Out on the island:** registered, Pepper dragged to the pizzeria
  goes there; she waves, and the island is drawn in 3D around her, its
  other people walking by. The arrow keys walk her (read through
  DirectInput's keyboard): up the road to the pizzeria, where the
  Brickolinis are, and on. Run to 33,000 frames, it doesn't stop. The right-hand
  door is the way out of the game: the Infomaniac asks
  (`iic043in_RunAnim`, the sound `IIC044IN.wav`) and puts a green brick
  and a red one before him, in 3D; picking the green one ends the game,
  with its credits.
- The settings it reads from `HKEY_LOCAL_MACHINE\SOFTWARE\Mindscape\LEGO Island`,
  which `CONFIG.EXE` writes: `3D Device ID`, `3DSound`, `Back Buffers in
  Video RAM`, `Display Bit Depth`, `Draw Cursor`, `Flip Surfaces`, `Full
  Screen`, `Island Quality`, `Island Texture`, `JoystickIndex`, `Music`,
  `UseJoystick`, `Wide View Angle`, `cdpath`, `diskpath`, `savepath`.
  `Island Quality` and `Island Texture` are read as text and made
  numbers; missing, each is "1" (`ISLE.EXE`, its default text at
  0x410228, read at 0x402ac4 and 0x402b16). Set to 0 or 2, the island's
  rocks are drawn coarser or the same; its roads don't change. The
  yes-or-no ones are text, "YES" for yes *(inferred: the one literal
  beside their names in both programs)*. Without them it uses its own
  defaults.

## In Reluva

**Playing it:** the engine (`src/engines/legoisland/legoisland.cpp`)
starts a `Player` (`src/engines/legoisland/host/player.cpp`), which runs
the program on a thread of its own, as its main loop never hands back
control. The engine's thread shows each frame the program shows (its
primary surface), and passes the player's input on: the pointer where it
is in the game's picture, which fills the window's client area, its
buttons and wheel, and keys, as window messages and DirectInput's state.
The program's sound goes to the engine's mixer through the DirectSound
stand-in. Closing the window asks first. The registry's `savepath` is
the install folder, whose writes go to the game's save folder, so its
saved games are kept with it *(inferred: what `CONFIG.EXE` writes there
isn't known)*.

`GameProcess` (`src/engines/legoisland/host/game_process.cpp`) loads and
starts the program. `reluva-tool legoisland run <game folder>` runs it and
reports how far it gets: the functions nothing stands in for, by DLL; on
a failure, the last stand-ins called and the last instructions (as
module+offset); the frames shown, and how fast it ran. `-log` prints
every stand-in called (and, for `QueryInterface`, the interface asked
for), `-files` each file the program asks for, and
`-save <folder>` lets it write, `-shots <folder>` saves every
`-every`th frame shown as a TGA file, `-at <address>` prints the
registers, the stack's first words and any text they point to (or that a
pointer they point to points to) each time the program gets to an
address, `-watch <address>,<bytes>` each store into memory there and
where it was made, `-click <frame>@<x>,<y>` a left click at a point
of the game's picture at a frame, `-drag <frame>@<x>,<y>:<x>,<y>,<frames>`
the left button pressed at one point and let go at the other so many
frames later, the pointer moved between, and `-key <frame>@<key>,<frames>` a key
(`up`, `down`, `left`, `right`, `space`, `return`, `escape` or a
character) held from a frame for so many. With `-log`, the file calls' handles, positions and
sizes are printed too. `reluva-tool pe-disasm` shows `LEGO1.DLL`'s
instructions at an address ([disasm.md](../../../common/x86/disasm.md)).

## Sources and checks

- **Where the facts come from:** the executables' import tables; the
  installer's log, for the registry and the install folder; the
  program running in the interpreter, through `reluva-tool legoisland
  run`, for how far it gets. The sound card message was seen with
  `DirectSoundCreate` made to fail, in a build kept out of the
  repository.
- **How it was checked:** run on the English 1.0 release, imported from
  its CD (November 2026).

## Open questions

- What `CONFIG.EXE` writes for each setting, and its defaults: Reluva
  sets only `savepath` (and the installer's `cdpath`, `diskpath`).
- How bright the island is in the original: its roads are plain grey
  in the world database (`isle`'s "ROAD GREY FLAT", #636363, untextured)
  and its grass dark green (#114216), but drawn lit by a white
  directional light, a white point light and an ambient one at 0.3,
  Direct3D's way, they come out light grey and bright green.
- Which of the missing functions the program calls, once it gets past
  DirectSound.
- What else the registry holds once `CONFIG.EXE` has run, and which of
  it the program needs.
