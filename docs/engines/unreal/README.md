# The Unreal engine

Epic's first Unreal engine (Unreal Engine 1), meant for a family of games;
the first here is Star Trek: Deep Space Nine - The Fallen. Engine id
`unreal`, in `src/engines/unreal/`.

## Games

Releases are recognised by the files in `detectionTable()`
(`src/engines/unreal/games.cpp`), made with `reluva --hash <files...>`.

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Star Trek: Deep Space Nine - The Fallen](games/ds9fallen/README.md) | `ds9fallen` | The Collective, 2000 | Windows | One, by `System/DS9.exe` and `System/Engine.u`; others by their file names, as an unknown release | Playable: every level runs with its scripts, the player, HUD, menus, saves and sound; being checked against a recording of the original |
| Any other Unreal Engine 1 game | `ue1` | | | A folder holding `System/Core.u` and `System/Engine.u` that isn't a known game, offered as "Unreal Engine 1 game (unidentified)" | Its data can be looked at in the data viewer |

## One engine for the family

Like `idtech3`, this engine is meant for every game on Epic's first Unreal
engine. What they share (packages, properties, textures, sounds, BSP
levels, and the UnrealScript virtual machine) is written once; what a
game changes is keyed off its `GameInfo` in `games.cpp`, with
game-specific code kept apart from the rest when there is some.

Everything in these docs so far was worked out from The Fallen's own
files and DLLs, so its pages are in [games/ds9fallen/](games/ds9fallen/README.md).
A page moves up to this folder once a second game is found to share it.

## Where it stands

The Fallen is playable: see
[games/ds9fallen/](games/ds9fallen/README.md#where-it-stands).

Other UE1 games: the layouts other licensees changed are to be checked
once there's a second game to check against. Their music modules play
(`.umx`: IT, S3M, XM, MOD, through Reluva's
[tracker player](../../common/audio/tracker.md)), checked only on modules
the tests build: not yet on a real song, nor its `Song` and transitions
in a level.

## Using it

See [games/ds9fallen/](games/ds9fallen/README.md#using-it). Any other
UE1 game is shown in the data viewer only: `reluva-viewer <game folder>`.

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its games, where it stands |
| [games/](games/) | A folder per game, for what was worked out from it: [The Fallen](games/ds9fallen/README.md), with its formats, systems, tests and how it was worked out |

## Code

In `src/engines/unreal/`:

| Path | What |
|---|---|
| `unreal.cpp` | The `MetaEngine` (name, detection, making an `Engine`) and the `Engine` |
| `games.cpp` | The detection table and what each game changes |
| `formats/` | The file formats: packages and their search path, properties, textures, sounds, BSP models, meshes, fonts, actors and class defaults, the dialogue tables, saved games (read and written) |
| `vm/` | The UnrealScript VM: the compiled script read and written out as text, the interpreter, and the natives (Core's, the level's, physics, climbing, paths, dialogue, canvas, console, sound, effects, splines, animation, attachment, travel, saves, documents) |
| `game/` | A level being played (`LevelPlay`), its zones, and collision against its BSP |
| `render/` | Drawing a level (`LevelView`, `render/level_view.h`) through its view (`ViewContext`, `render/view.h`), its lighting, meshes and procedural textures |
| `audio/` | A level's sound, through the shared mixer |
| `ui/` | The movie player (`MoviePlayer`, `ui/movie.h`) |
| `pages/` | The data viewer's pages |
| `tools/` | `reluva-tool unreal` commands (`reluva-tool help unreal` lists them), and the software renderer `unreal play` draws with |

## Tests

- `reluva-tests unreal` runs its unit tests (`unreal.unit.packages`,
  `unreal.unit.textures`, `unreal.unit.sounds`, `unreal.unit.levels`,
  `unreal.unit.meshes`, `unreal.unit.script`) and its engine test
  (`unreal.engine`), in `tests/engines/unreal/`.
- No game-files test yet: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand:
  [games/ds9fallen/TESTING.md](games/ds9fallen/TESTING.md).

## Terms

- **Package:** a file of the engine's objects (`.u`, `.unr`, `.utx`,
  `.uax`, `.umx`; The Fallen's maps are `.dsm`)
  ([package.md](games/ds9fallen/formats/package.md)).
- **Export, import:** an object a package holds, and one it refers to in
  another package.
- **UnrealScript:** the engine's scripting language, compiled into the
  packages and run by the VM
  ([script-vm.md](games/ds9fallen/systems/script-vm.md)).
- **Native:** a script function done in C++ by the engine.
- **BSP, model:** a level's geometry, a binary space partition of its
  brushes ([model.md](games/ds9fallen/formats/model.md)).
- **Zone:** a part of a level closed off by zone portals; what's drawn is
  the zones seen through them.
- **Lightmap:** a surface's light, worked out from its lights and their
  shadow bits ([lighting.md](games/ds9fallen/systems/lighting.md)).
- **Mover:** an actor drawn from a brush of its own and moved by its
  script: a door, a lift ([actors.md](games/ds9fallen/systems/actors.md)).
- **Mesh, LodMesh:** an animated model ([mesh.md](games/ds9fallen/formats/mesh.md)).
- **UWindow:** the engine's window system, for options windows.
