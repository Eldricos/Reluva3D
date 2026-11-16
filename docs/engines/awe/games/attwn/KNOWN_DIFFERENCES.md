# And Then There Were None: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

The game runs on the engine's shared code, so most of what Reluva does
differently from Orient Express it does here too; those differences are
listed again below, each with what this game's code was checked for.
Orient Express's page ([../motoe/KNOWN_DIFFERENCES.md](../motoe/KNOWN_DIFFERENCES.md))
gives their detail.

## By choice

### Drawing

- **Shadows on a device without a stencil buffer** are filled on the
  CPU, their edges a pixel off a card's at most; with a stencil buffer
  they're drawn as the game's are ([systems/shadows.md](systems/shadows.md)).
- **The specular light is a second pass** over the textured model, where
  Direct3D adds it in the same draw; the sum is the same. This game
  lights with specular as Orient Express's does
  ([systems/lighting.md](systems/lighting.md)).
- **Anti-aliasing changes at once.** The game resets the device when it's
  changed (OK at `0x4245f2`); Reluva makes the frame's targets again
  ([systems/options.md](systems/options.md)).
- **Characters are skinned and lit on the CPU**, as Direct3D's
  fixed-function pipeline would light them: the same colours, by the same
  per-vertex formula.
- **Text is drawn by Reluva's own TrueType renderer**, not Windows', from
  the game's fonts sized as Windows sizes them: letters can differ a
  little at their edges. Lines wrap where the game's do (past 750
  pixels, `0x434960`; [systems/dialogue.md](systems/dialogue.md)).

### Characters and animation

- **A walk's root motion** is the distance the root moves in one loop
  (`Actor::motion`), taken out of each animation as it's blended, where
  the game takes it out of the pose through Granny's track groups: the
  character goes the same way at the same pace
  ([../../formats/gr2.md](../../formats/gr2.md)).
- **An `anim` whose file is missing ends at once**, where the game would
  wait for ever and hang the script; no `anim` in the install names a
  missing file.
- **An `anim` finishes whether it played under the asked name or a
  renamed one.**

### Saves and scenes

- **Scenes aren't kept in memory.** The game loads a place's scenes
  together (`0x4035d0`, [systems/acts.md](systems/acts.md)); Reluva
  reads a scene again when it's shown, keeping what the game would have
  kept (characters' positions, what scripts left them doing, the
  player's position) by scene until a new place is loaded.
  Saves are in the game's own format either way
  ([systems/saves.md](systems/saves.md)).

## Bugs fixed

None: where the original misbehaves, Reluva does the same.

## Not yet as the original

### Only one release checked

Everything was worked out and checked on the English retail release on
two CDs, the only one we have. Other releases haven't been tried: they
wait for someone who owns one. Reluva finds such a release by the
fallback (the same files and `granny2.dll`, whatever their contents),
the game-files test makes it a record of its own
(`attwn-unknown-release`, [TESTING.md](TESTING.md)), and what differs
goes here.

## Additions outside the game

None of these changes what the game does.

- **Data viewer.** `reluva-viewer` has pages for looking at the install:
  Scenes (the scenes' files, an act's version over its base; a game
  started with N), Models, Movies, Pictures, Text and Sounds
  ([README.md](README.md#using-it)).
- **The frame rate in the game's title.** The window is titled with the
  game's name, as `ac.exe` names it (`0x401980`), then the frames shown
  in the last second.
- **Screenshots.** F12 saves the screen as a TGA, under `screenshots/` in
  the game's save folder.
- **Frame time.** `RELUVA_PERF=1` writes where a frame's time goes, every
  three seconds.
- **Importing from the CDs.** The launcher copies the game out of its CDs'
  cabinets as its setup would install it, without running setup
  ([README.md](README.md#using-it)).
- **Tools.** `reluva-tool`'s `awe` commands read, check and dump the
  install's files, compare Reluva's code with the original's, and play
  the game headless ([TESTING.md](TESTING.md)).
