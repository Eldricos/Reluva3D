# Need for Speed II SE: the hosted spike (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026: Phase 5 of [the plan](../PLAN.md),
the retail `nfs2sen.exe` run on NFS III's host, and its speed in a race.
The copy studied is the one in [first-survey.md](first-survey.md).

## How it was run

```text
reluva-tool nfs host <install> -deterministic 300000 -frames <dir> -every 10
    -keys 6000:enter,14000:move0/60,16000:move0/60,18000:move0/60,20000:click,40000:+up
```

The CD's files beside the install, found as the Play page finds them.
`-deterministic 300000` is a clock of 300,000 instructions to the
millisecond, about what the interpreter runs a second on the machine
used, so key times land on the same moment every run. With the real
clock the start-up's threads take turns differently each run, and a key
meant for the title movie can land before or after it.

## What it does

- **Start-up:** reads `install.win`; loads the install's `dsetup.dll`
  (DirectX's set-up) and asks it the DirectX version, which it finds in
  the registry (`Software\Microsoft\DirectX`, not there: it goes on);
  checks the free memory and disk; makes its window on a thread of its
  own, which runs the message loop, while the main thread runs the game.
- **DirectDraw:** 640x480 in 16 bits, a primary surface with one back
  buffer. **DirectInput:** the mouse only. **Sound:** `eacsnd.dll`
  (iSND) on DirectSound, as NFS III's.
- **Keys:** the window procedure takes `WM_KEYDOWN` itself; `WM_KEYUP`
  goes to `DefWindowProcA`. A `WH_KEYBOARD` hook (`0x475380`) catches
  only system keys: Alt+Tab, Alt+Esc, Ctrl+Esc, the Windows and menu
  keys, Print Screen.
- **Movies:** the EA logo, then `D:\FeData\pc\movies\titleav.dct`, a
  reader thread feeding it. Each picture is drawn into the front surface
  of the flipping pair (Lock, Unlock), not flipped: two presents to a
  picture. Enter skips the title movie.
- **Front end:** the main menu with its cursor, after which, left alone,
  it plays the attract-mode movies (`demoav0.dct`...). The mouse moves
  the cursor and clicks.
- **Race:** RACE, clicked, loads Proving Grounds from the CD and races
  (`joyGetNumDevs` is asked on the way). Each frame's picture is copied
  into the front surface in 8 strips of rows, top to bottom, then a part
  of the HUD: 9 presents to a frame.

What the host was missing, each found by the game stopping or going
wrong:

| Found | Fixed |
|---|---|
| The host loaded only `nfs3.exe` | `Options::program`; the tool picks `nfs2sen.exe` |
| Every DLL the game loaded but `eacsnd.dll` was taken for THRASH, its functions wrapped as THRASH's: `DirectXSetupGetVersion` crashed (stdcall taken for cdecl) | THRASH is a DLL exporting `THRASH_` functions |
| A frame only at a flip, or at an Unlock of a primary without a back buffer: the movies never showed | Drawing into a primary shows at its Unlock, back buffer or not, as on Windows |
| `SetWindowsHookExA` did nothing | A keyboard hook is called with each key message as it's taken from the queue |
| `joyGetNumDevs` missing | 0: no joystick driver |

## The race's speed

Proving Grounds, the car driven with Up held, 8 cars. Over 24 billion
instructions, the clock reached 80,015 ms: every thread waited for 15 ms
of it, so the game used all the time it had, drawing as fast as it
could. In the race:

- a frame every 25-26 ms of the game's clock: about **39 frames a
  second**, at 300,000 instructions a millisecond;
- so about **7.7 million instructions a frame**, everything included
  (the simulation, the software renderer, the sound DLL's mixing);
- the race's own clock (the HUD's) went 1.05 s in 1.05 s of the game's
  clock: the race runs at its real speed;
- the run took 69.5 s for 80 s of the game's clock: the emulator,
  345 million instructions a second on that machine, faster than real
  time.

The software renderer is in the game itself (no THRASH DLL here), so
nothing of it runs natively: the whole game is interpreted, and still
comfortably at full speed. NFS III's race needed 16 million a frame, 14
of them its software renderer's (its PLAN.md, Phase 4).

## Left for Phase 6

- **Up and Down in the menus**: the first press moves the selection, the
  next ones don't, though each `WM_KEYDOWN` reaches the window
  procedure. Enter works, as does the mouse. Not worked out.
- **Presenting**: each Unlock of the front surface converts the whole
  screen, 9 times a frame in a race. Once a frame would do: at the last
  strip, or on the host's own clock.
- **The registry** has no `Software\Microsoft\DirectX`: what the game
  does with the version `dsetup.dll` reports isn't known.
- **The drives** are labelled `NFS3`, NFS III's CD's label; the save
  folder the tool uses by default is named for NFS III.
- **Sound** wasn't listened to, nor the 3dfx program (`nfs2sea.exe`)
  tried.
