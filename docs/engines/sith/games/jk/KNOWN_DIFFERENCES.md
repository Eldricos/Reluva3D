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
  where the game's buffer has 5 or 6, and its depth finer than the
  game's 16 bits, so surfaces nearly at the same depth may hide each
  other otherwise. The display's gamma ramp, which the game doesn't set,
  isn't applied to the scene there. Specular colour, fog and the alpha
  test's exact reference aren't drawn on the card, as the game doesn't
  use them ([systems/hosting.md](systems/hosting.md), step 21). At a
  larger *Picture size* the 3D is finer than any card of the time drew
  it at 640x480, while the menus, gauges and text keep their pixels,
  enlarged.

## Additions outside the game

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
