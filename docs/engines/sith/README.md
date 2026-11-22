# The Sith engine

Engine id `sith`, in `src/engines/sith/`. It's meant for the family of
LucasArts games on the Sith engine, which share Jedi Knight's files (GOB
archives, text levels and models, COG scripts), starting with Jedi Knight:
Dark Forces II; its expansion, Mysteries of the Sith, would be the next.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Jedi Knight: Dark Forces II](games/jk/README.md) | `jk` | LucasArts, 1997 | Windows | US retail, two CDs, unpatched | Recognised, its CDs found; nothing of the game runs yet |

## Where it stands

Jedi Knight's install is recognised and its CDs are found; the engine
shows an empty screen. See the game's [README](games/jk/README.md) and
[PLAN.md](games/jk/PLAN.md).

## Using it

Add Jedi Knight's install in the launcher (*Add Game...*). The game's
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
| `folders.cpp` | Telling the CDs apart, and finding them for an install |
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
