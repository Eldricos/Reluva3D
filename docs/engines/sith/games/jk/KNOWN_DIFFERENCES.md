# Jedi Knight: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

- **Sound:** none. The game finds no sound card, no CD audio and no
  joystick, and plays silently ([systems/hosting.md](systems/hosting.md)).
- **Multiplayer:** DirectPlay offers no connections, so there's no one to
  play with.
- **Direct3D:** Direct3D 1's devices aren't there yet, so choosing
  hardware drawing in *Setup* would stop the game. The game's own
  software drawing, its default, is what's shown.
- **Text written through GDI** is in Reluva's built-in bitmap font, not
  Arial. The menus and the game draw their text in the game's own fonts,
  which are as they were.

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
