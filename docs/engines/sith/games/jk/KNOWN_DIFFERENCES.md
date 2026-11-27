# Jedi Knight: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Changing CDs:** the original asks for CD 1 or 2 ("Insert Jedi Knight
  CD #n and press Enter") when a step of the episode is on the other one.
  Reluva puts that CD in the drive as the game checks, so it never asks
  ([systems/hosting.md](systems/hosting.md)).

## Bugs fixed

None yet.

## Not yet as the original

- **Music:** from each CD's audio tracks beside its files (a cue sheet
  and its image, or ripped tracks); without them, none, as with the CD
  missing ([README.md](README.md#using-it)).
- **Joysticks:** the players' game controllers, laid out as Reluva
  chooses ([joystick.md](../../../../common/x86/joystick.md)), a racing
  wheel as either. Two, as the game itself reads no more. *Calibrate
  joystick* opens nothing: Windows' control panel isn't there, the
  game's `WinExec` of "control joy.cpl" fails as for a program that isn't
  installed ([windows.md](../../../../common/x86/windows.md)), and the
  game ignores the failure (`0x41edb0`) *(from the code)*. Reluva's
  joysticks need no calibration: their ranges are always 0 to 65535,
  centred.
- **Windows' dialog boxes:** none is shown. The game makes one only
  with both `-devMode` and `-windowGUI` on its command line, its "Jedi
  Console" (template 112, `0x41f11c`); the emulator makes it and runs its
  procedure, but draws nothing
  ([windows.md](../../../../common/x86/windows.md)).
- **Multiplayer:** over TCP/IP only, and between copies of Reluva: its
  DirectPlay plays sessions over a protocol of its own, so it can't meet
  a game on Windows' DirectPlay. There's no IPX, modem or serial
  connection, no lobby to start it from, and the host's address, where
  Windows' TCP/IP asked for it in a box, is the `joinaddress` setting.
  The host's leaving ends the session, as the game asked.
- **Direct3D:** *Setup*'s hardware drawing is done by the emulator's
  software rasterizer, not a graphics card: its filtering and precision
  are Reluva's, not those of the cards of 1997. With *3D drawing* on the
  graphics card, it's the GPU's instead, its colours 8 bits a channel
  where the game's buffer has 5 or 6, and its depths worked out its own
  way, so where triangles meet at the same depth the other may keep a
  pixel. The display's gamma ramp, which the game doesn't set, would be
  drawn as the nearest power curve; table fog (by depth), which it
  doesn't use either, isn't drawn ([systems/hosting.md](systems/hosting.md),
  step 21). At a
  larger *Picture size* the 3D is finer than any card of the time drew
  it at 640x480, while the menus, gauges and text keep their pixels,
  enlarged. *Anti-aliasing* smooths the polygons' edges, which the cards
  of 1997 left stepped; the edges of a texture's transparent holes stay
  stepped.

## Additions outside the game

- **Widescreen:** the *Widescreen* setting has the game play on a 16:9,
  16:10 or 21:9 screen, seeing more at the sides, by offering it a mode
  it never had and changing its focal length as it runs
  ([systems/hosting.md](systems/hosting.md), step 24). It plays through
  Direct3D whatever *Setup* says; the menus and cut scenes stay 4:3.
  Only the first level of each game has been played this way.
- **Frame rate:** by default the game shows at most 60 frames a second,
  its world moving on about every other one, as on a 60 Hz display.
  The *Frame rate* setting can have it move at every frame instead, at
  60 to 100 frames a second, by changing one number in the game's code
  as it runs: how long it waits between moving its world. Its speed is
  unchanged ([systems/hosting.md](systems/hosting.md), step 22), though
  only Nar Shaddaa's walking and jumping were measured, and the
  multiplayer games haven't been played at it. With it on, Reluva
  crashed as the ending's cut scene started (after Jerec) in 3 of 12
  skims through the game, in the emulator's native code (its JIT): the
  first three, within the same hour; the nine since, and 24 runs from a
  level 21 save, didn't. Not found, and not seen with the game's own
  frame rate ([TESTING.md](TESTING.md#last-checked)).

- **Copies of the CDs:** the original reads its cut scenes from the CD in
  the drive; Reluva finds copies of both CDs' files in or beside the
  install, or where the `cd1path` and `cd2path` settings say
  ([README.md](README.md#using-it)).
- **The data viewer** (`reluva-viewer`, never the game): the archives'
  pictures, materials, colormaps, fonts and strings
  ([README.md](README.md#using-it)).
- **Importing:** Reluva's *Import Game...* copies what the game's
  installer does, without running it, and the CDs' cut scenes too, so the
  game needs no CD.
