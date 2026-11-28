# Mysteries of the Sith: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** the US retail CD, as an image (a cue sheet, its
  data track and 13 audio tracks), and an install from it (`JKM.EXE`
  1,759,744 bytes, whole-file MD5 `e8d36089bbd161f2d6881c7da1112662`).
- **`JKM.EXE`:** its imports and strings; its code where a run of it
  stopped, read with `reluva-tool sith disasm`; and the program run in
  Reluva's x86 emulator.
- **The readme** (`README.TXT`).
- **Not used:** decompilation-based rebuilds, as for Jedi Knight
  ([../jk/PLAN.md](../jk/PLAN.md#decisions)).

## Methods

- The CD image's data track read as ISO 9660 and its files compared with
  the install's by whole-file MD5; the archives' directories listed.
- `JKM.EXE` run with Jedi Knight's host changed by hand for it: where it
  stopped (a box asking for a CD), the code that reads `jk_.cd` found by
  the references to that string, and read.

## The tools

`reluva-tool help sith` lists the engine's commands
(`src/engines/sith/tools/`); `host` and `disasm` were used, `host` changed
by hand to run `JKM.EXE` until the game is known to Reluva.

## Working files not included

The files taken out of the CD's image, and the trial's change to the
host, kept outside the repository.
