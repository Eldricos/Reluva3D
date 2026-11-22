# Need for Speed II SE: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **An install with only `nfs2sea.exe`**, the program for 3dfx cards,
  is recognised as the game (`fallbackTable`, `src/engines/nfs/games.cpp`).
  The CD's set-up copies the programs on a condition, which by its
  script's strings is the display card: it looks through the PCI display
  devices and copies 3dfx's `glide2x.dll` for a Voodoo Rush or another
  Voodoo, and the install studied, made without one, has every icon but
  only `nfs2sen.exe` ([research/first-survey.md](research/first-survey.md),
  The set-up). Whether a 3dfx install also has `nfs2sen.exe` isn't known:
  an install made on a machine with a Voodoo card would settle it.
- **Pictures** ([fsh.md](../../formats/fsh.md), NFS II SE): an 8-bit
  picture without a palette uses its bank's palette entry; 4-bit
  pictures are the low half of each byte first; `0x29` is a 16-bit 5:6:5
  palette. Each from how the pictures look; the game's code would settle
  them.
- **The pause screens' palette** is the race's, `hud.fsh`'s: the 3dfx
  copies' colours agree with it ([fsh.md](../../formats/fsh.md), NFS II
  SE's pause screens). The software game, paused, would settle it.
- **The front end's text records** ([text.md](../../formats/text.md),
  NFS II SE's front end): their first byte is the font's size, and two
  of their words the string's place on the screen. The front end drawn
  by the game would settle it; the other three bytes aren't worked out.

## Guessed

None yet.

## Settled

None yet.
