# Need for Speed II SE: the program on Reluva's host

> **Status:** done · **Code:** `src/engines/nfs/host/game_host.cpp`, `directx.cpp`, `src/engines/nfs/ui/play_page.cpp`, `src/engines/nfs/tools/host.cpp` · **Checked against:** the original running on Reluva's host · **Confidence:** from the code unless marked

## Overview

Reluva plays NFS II SE by running its retail programs in the x86
emulator, as it does NFS III's: `nfs2sen.exe`, which draws in software,
and `nfs2sea.exe`, which draws through 3dfx's Glide. This page is what
the programs do with Windows and DirectX, and what they need of the
host: how they start, show their pictures, take their keys and keep
time.

## How it works

Addresses are in `nfs2sen.exe`. What a run showed is from
[research/hosted-spike.md](../research/hosted-spike.md) unless another
note is named.

### Start-up

- It reads `install.win`.
- It loads the install's `dsetup.dll` (DirectX's set-up) and asks it the
  DirectX version (0x4722c0). It goes on only from DirectX 5, the
  version's low word at least 5 (0x472308): without the DLL, or with
  less, it shows a message in the chosen language and quits.
- The DLL reads the version from the registry
  (`Software\Microsoft\DirectX`, `Version`: "4.05.01.1998" read as 4.05
  and 1.1998), or without the key works it out from Windows' version:
  4.05 on Windows 98 and on NT after 4.
- It checks the free memory and disk, and makes its window on a thread
  of its own, which runs the message loop, while the main thread runs
  the game.
- It finds the CD as the CD-ROM drive holding its own program
  (0x471cc0). Its only `GetVolumeInformationA` (0x447bf5) asks for the
  file system's flags, not the label.

### DirectX and sound

DirectDraw at 640x480 in 16 bits, a primary surface with one back
buffer. From DirectInput, with no controller, it took only the mouse;
`joyGetNumDevs` is asked on the way to a race. Its sound is
`eacsnd.dll` (iSND) on DirectSound, as NFS III's.

### Pictures

The software program draws into the front surface of its flipping
pair (Lock, Unlock), never flipping it:

- a movie's picture: two Unlocks to a picture;
- a race's frame: copied in 8 strips of rows, top to bottom, then a
  part of the HUD: 9 Unlocks to a frame.

The network lobby is drawn into the front surface by one thread,
polling the link, which never pauses
([research/network-play.md](../research/network-play.md)).

### Keys

- The window procedure takes `WM_KEYDOWN` itself; `WM_KEYUP` goes to
  `DefWindowProcA`.
- A `WH_KEYBOARD` hook (0x475380) catches only system keys: Alt+Tab,
  Alt+Esc, Ctrl+Esc, the Windows and menu keys, Print Screen.
- Esc held is scan code 1 in the keys' table, set and cleared at
  0x4757f0 and 0x475783.
- The menus read their key buffer (0x475f20) twice (0x405900): the first
  read's key steers the menu, and a key the second finds is only checked
  against the cheat codes (0x404df4). Between the two reads there is
  only a critical section, so a key that comes between them is lost to
  the menu. The main thread's loop calls DirectInput (0x422d71) before
  the first read.

### Force feedback

With a wheel offered, force feedback plays once the wheel is the game's
controller: *options*, *controls*, *setup*, each control re-mapped by
moving the wheel (steering took axis 0, the accelerator axis 1). In a
race the game then keeps a spring on the wheel whose centre follows the
road (214 changes in a minute), and a full constant push at two impacts;
no damper or vibration. Left on the keyboard, the game downloads its
three effects but never starts them ([PLAN.md](../PLAN.md), Phase 6).

### Time

The program's tick is at 0x51b024: zeroed as its timer starts
(0x477e1b), one more each time the timer thread wakes (0x4779cf), asked
for 128 times a second (0x47212c)
([research/network-play.md](../research/network-play.md)).

**The race timer check.** Before a race the program sets a counter going
on its timer (0x4dcf78, zeroed by 0x4402f0), one more each two of the
timer's ticks (0x440340), loads the race, and quits if the counter is still 0
(0x45cee0), with exit code 1 and no message; its last call is the exit
at 0x45d0a0. On a machine of the time the load's file reads alone take
a second or more, so the counter has long moved
([research/road-driver.md](../research/road-driver.md)).

### The 3dfx program

`nfs2sea.exe` draws only through Glide, made for the Voodoo Graphics.
It passes its vertices snapped (x + 786,432) and never takes the snap
off, which the first boards read by their low bits; on a Voodoo2 nothing
showed ([PLAN.md](../PLAN.md), Phase 7). It needs nothing else of the
host. Its Glide calls are on
[docs/common/graphics/glide.md](../../../../../common/graphics/glide.md).

## In Reluva

`GameHost` (`src/engines/nfs/host/game_host.cpp`) runs the program it's
given (`Options::program`; `Options::programFile` for the CD's 3dfx
program). The launcher's Play page (`src/engines/nfs/ui/play_page.cpp`)
runs `nfs2sen.exe`, or with `renderer=glide` `nfs2sea.exe`
(`nfs2se::playProgram`); `reluva-tool nfs host` runs either without a
window (`src/engines/nfs/tools/host.cpp`).

- **Windows' behaviour** the program needs: only DLLs exporting
  `THRASH_` functions are taken for THRASH (`isThrashRenderer`), so
  `dsetup.dll` and `eacsnd.dll` run as themselves; a keyboard hook is
  called with each key message as it's taken from the queue;
  `joyGetNumDevs` gives 0, no joystick driver.
- **The registry** has DirectX 5.2's key, `Software\Microsoft\DirectX`
  with `Version` "4.05.01.1998", as Windows 98, which the host reports,
  came with. It's put in for NFS II SE only.
- **The drives** are labelled `NFS2SE` (`Options::driveLabel`), the CD's
  real label not known *(guessed)*. The game's writes (`config.dat`,
  `TmpTrk/`, `replay.rpy`, `HUDCON.DAT`) go to Reluva's save folder for
  the game; the install is left untouched.
- **Presenting** (`GameHost::presentPendingFront`,
  `src/engines/nfs/host/directx.cpp`): drawing into a primary surface
  is shown at its Unlock, back buffer or not, as on Windows. A picture
  written into the front surface is shown once the writing stops for 4
  ms of the game's clock, or 25 ms after it began, for drawing that
  doesn't stop: once a frame in a race, not at each of its 9 Unlocks.
  It's also checked before each native function and at each change of
  threads, so the lobby shows.
- **Input** reaches the game as Windows' would, by the clock, its window
  thread running at once to take it: before a native function of the
  main thread's, and at a change of threads once 20 ms late. Not before
  other threads' functions, nor before critical sections and TLS: the
  sound's threads call functions while the main thread may be between
  its menus' two reads of the key buffer.
- **The 3dfx program** runs on Reluva's Glide stand-in as a Voodoo
  Graphics unless `glideboard=` (nfs host `-glideboard`) names another.
- **Force feedback:** the game's DirectInput effects are played on the
  wheel offered to it (`src/engines/nfs/host/force_effects.h`).

**The clock.** On the real clock the game sees real time. `reluva-tool
nfs host -deterministic N` gives it a clock of N instructions to the
millisecond instead (`Options::instructionsPerMs`), a millisecond more
each time every thread waits, so runs are the same each time: the key
times land on the same moments, and the date is fixed (1 November 1997,
every file dated so). 300,000 is about what the interpreter runs a
millisecond on the machine measured, where a race drew about 39 frames
a second, 7.7 million instructions a frame. On that clock file reads
would take no time, and a race's load about 15 ms, two of the timer's
ticks: now and then the race timer check found its counter still 0 and
the program quit. So the tool has a file's bytes read (and mapped) take
a millisecond for each 2,000, a hard disk of the time
(`Options::diskBytesPerMs`): the load then takes about 1.5 s, the
counter 14 to 16 when checked. Two copies on a link need the real
clock: on instruction clocks theirs drift apart, and the game's own
network timeouts fire ([network.md](network.md)).

These are listed in [KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md).

## Sources and checks

- **Where the facts come from:** `nfs2sen.exe` and `nfs2sea.exe` run on
  the host by `reluva-tool nfs host`, their calls logged and their
  frames saved, and their code disassembled where they stopped or went
  wrong; `dsetup.dll`'s reading of the version, as it behaved with and
  without the key; a memory dump at the end of a run that quit. The
  working notes they came from are
  [research/hosted-spike.md](../research/hosted-spike.md),
  [research/road-driver.md](../research/road-driver.md) and
  [research/network-play.md](../research/network-play.md).
- **How it was checked:** the movies, front end and races played on the
  host and from the Play page; 64 keys sent on the real clock to two
  copies, none lost; a race's presents, one whole picture every 23-29
  ms; a wheel's forces logged (`-forcelog`); a knockout to its fifth
  round, every round's load past the race timer check; the game-files
  test's `host`, `host_glide` and `drive` steps, the same record each
  time ([TESTING.md](../TESTING.md)).
