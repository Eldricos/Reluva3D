# The Storm3D engine

Engine id `storm3d`, in `src/engines/storm3d/`, for the games Activision
built on its Storm3D renderer and the Interstate '76 and Battlezone code
under it. Meant for a family of games: Star Trek: Armada first, Star
Trek: Armada II later, as both share their files' kinds (`.sod` models,
`.odf` object definitions, `.bzn` maps).

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Star Trek: Armada](games/armada/README.md) | `armada` | Activision, 2000 | Windows | English retail 1.0 | Recognised; a data viewer of its files |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything was read from Star Trek: Armada.

## Where it stands

Star Trek: Armada is recognised, and the data viewer shows its pictures,
models, sprites, sounds, movies and text; the game itself doesn't run
yet: see
[games/armada/](games/armada/README.md#where-it-stands).

## Using it

The game's page says how to add it: [Star Trek: Armada](games/armada/README.md#using-it).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine, its games, where its code goes, and its tests |
| [games/armada/](games/armada/README.md) | Star Trek: Armada: its releases, plan, formats, known differences, unconfirmed items, testing and how it was worked out |

## Code

| Path | What |
|---|---|
| `storm3d.cpp` | The `MetaEngine` (name, detection tables, making an `Engine`) and the `Engine`: the opening movie and the main menu so far (`shell/`) |
| `game_files.cpp` | A game's files searched as one: the install's folders, then its archives |
| `pages/` | The data viewer, for `reluva-viewer`: a page per kind of file |
| `scene/` | Models drawn in 3D (`SodMeshes`) and their trees of nodes (`ModelTree`): the game's and the data viewer's |
| `shell/` | The front end: the opening movie, the main menu, the single-player screen, the loading screen |
| `formats/` | Reading the games' files: ZFS archives, object definitions, settings in C, `Input.map`, `.ldl` files, sprite tables, models |
| `tools/` | `reluva-tool storm3d` commands (`reluva-tool help storm3d` lists them), the original hosted in the emulator among them |

What every game shares (the archives, models, object definitions, maps,
the renderer) goes in the engine's folders as ADDING_AN_ENGINE.md lays
them out; what is one game's own goes in a folder named after it
(`armada/`).

## Tests

- `reluva-tests storm3d` runs its unit tests and its engine test
  (`tests/engines/storm3d/`).
- A game-files test, once it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: each game's TESTING.md
  ([Star Trek: Armada](games/armada/TESTING.md)).

## Terms

- **Storm3D:** Activision's 3D renderer, named so by `Armada.exe` itself,
  with Direct3D 7 and software drivers.
- **ZFS:** Activision's archive format (`ZFSF` at its start), from
  Interstate '76.
- **ODF:** an object definition file: a unit's, a weapon's or the game's
  settings, as text.
- **SOD:** a Storm3D model (`Storm3D_SW` at its start).
- **BZN:** a map or a saved game, from Battlezone.
