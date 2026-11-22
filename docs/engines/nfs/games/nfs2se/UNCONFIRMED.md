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

## Guessed

None yet.

## Settled

None yet.
