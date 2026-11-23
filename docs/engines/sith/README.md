# The Sith engine

Engine id `sith`, in `src/engines/sith/`. It's meant for the family of
LucasArts games on the Sith engine, which share Jedi Knight's files (GOB
archives, text levels and models, COG scripts), starting with Jedi Knight:
Dark Forces II; its expansion, Mysteries of the Sith, would be the next.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Jedi Knight: Dark Forces II](games/jk/README.md) | `jk` | LucasArts, 1997 | Windows | US retail, two CDs, unpatched | Recognised and imported from its CDs; nothing of the game runs yet |

## Where it stands

Jedi Knight's install is recognised, or made from its CDs, and its CDs
are found; its archives and their pictures, materials, colormaps, fonts,
string tables, sounds, sound classes, episodes, models, animations and puppets are read, and the cut
scenes decoded, all shown or played by the data viewer
(`reluva-viewer`). The engine itself shows an empty screen. See the game's [README](games/jk/README.md) and
[PLAN.md](games/jk/PLAN.md).

## Using it

Add Jedi Knight's install in the launcher (*Add Game...*), or import it
from its CDs (*Import Game...*). The data viewer is `reluva-viewer`. The game's
[README](games/jk/README.md#using-it) says where it looks for the CDs.

## This folder

| Page | What's in it |
|---|---|
| This page | The engine, its games |
| [games/jk/](games/jk/README.md) | Jedi Knight: its formats, plan, tests and how it was worked out |

What every game would share stays with Jedi Knight's pages until a second
game is found to share it (docs/DOCUMENTATION_STANDARD.md).

## Code

| Path | What |
|---|---|
| `sith.cpp` | The `MetaEngine` (detection, making an `Engine`) and the `Engine`: an empty screen for now |
| `games.cpp` | The games and their releases |
| `folders.cpp` | Telling the CDs apart, finding them for an install, and their cut scenes |
| `import.cpp` | Making an install from the CDs (*Import Game...*) |
| `game_files.cpp` | The install's archives, as one set |
| `formats/` | The games' files: `.gob`, `.cmp`, `.bm`, `.mat`, `.sft`, `.uni`, `.snd`, `episode.jk`, `.3do`, `.key`, `.pup` |
| `render/` | Drawing models (and, to come, levels) in software through a colormap, for the data viewer |
| `pages/` | The data viewer, in `reluva-viewer` only: pictures, materials, colormaps, models, fonts, string tables, sounds, sound classes, episodes, cut scenes |
| `tools/` | `reluva-tool sith` commands (`reluva-tool help sith` lists them) |

## Tests

- `reluva-tests sith` runs its unit tests and its engine test
  (`tests/engines/sith/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: the game's
  [TESTING.md](games/jk/TESTING.md).

## Terms

- **Sith:** LucasArts' engine for Jedi Knight, as the source file names
  in `JK.EXE`'s error messages call it (`sithWorld.c`, `sithThing.c`).
- **GOB:** the games' archives (`.gob`), holding their data files.
