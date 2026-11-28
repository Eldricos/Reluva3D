# Mysteries of the Sith: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Jedi Knight's CD 2 is linked once, at import.** The game asks for it
  on its first start; Reluva puts the copy of its marker, linked with
  *Add other disc...*, in the drive then, and the game's own CD the rest
  of the time ([README.md](README.md#proof-of-owning-jedi-knight)).
- **No second request for its own CD.** On the first start the original
  asks for its own CD right after Jedi Knight's, its search starting
  after the drive it found that in; Reluva has each search start from
  the first drive, so the game goes straight on.

## Bugs fixed

None yet. The original's own bugs are kept, as `JKM.EXE` runs unchanged
but where Reluva reaches into it; one known:

- **`makeitwork` typed without a number stops the game.** The cheat
  reads its number without checking one was typed, as `takemeto` does,
  so the program reads from address 0 and stops, as it would on
  Windows. With a number it works. Left as it is for now; a check of a
  few bytes, as for the program's other changes, would fix it
  ([README.md](README.md#its-cheats)).

## Not yet as the original

## Additions outside the game

- The engine's settings, as for Jedi Knight
  ([../jk/KNOWN_DIFFERENCES.md](../jk/KNOWN_DIFFERENCES.md)): the 3D on the
  graphics card, larger and smoothed; *Frame rate*, the world moved
  at each frame up to 100 a second; and *Widescreen*, the scope's sight
  kept as it is at 4:3
  ([README.md](README.md#where-reluva-reaches-into-the-program)).
