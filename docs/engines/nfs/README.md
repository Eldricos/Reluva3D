# The Need for Speed engine

Engine id `nfs`, in `src/engines/nfs/`. It's meant for the family of
Electronic Arts' Need for Speed games that share NFS III's files (VIV
archives, FSH pictures, FCE models, FRD tracks, EA's audio), starting with
Need for Speed III: Hot Pursuit; High Stakes would be the next. It runs
the retail game's own executable in Reluva's x86 emulator, with
stand-ins for Windows, DirectX and the game's renderer and sound
interfaces; `reluva-viewer` has a data viewer of the games' files.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Need for Speed III: Hot Pursuit](games/nfs3/README.md) | `nfs3` | Electronic Arts, 1998 | Windows | US "Classics" CD (English) | Plays: the retail game hosted, every race type, single player and network; a data viewer of its files |

## Where it stands

NFS III plays from its menus through every race type, single player and
over a network, its own executable run in the emulator; the data viewer
(`reluva-viewer`) shows every file kind it has. See the game's
[README](games/nfs3/README.md) and [PLAN.md](games/nfs3/PLAN.md).

## Using it

Add the game's install in the launcher (*Add Game...*), with a copy of
its CD's files beside it as `NFS3-CD` for the music and movies: it starts
straight into the game. The data viewer is `reluva-viewer` (`reluva-viewer
<its name in the launcher>` or `reluva-viewer <the install>`); Esc leaves
it. The game's
[README](games/nfs3/README.md#using-it) has its keys, the viewer's pages
and the settings.

## This folder

| Page | What's in it |
|---|---|
| This page | The engine, its games |
| [games/nfs3/](games/nfs3/README.md) | Need for Speed III: its formats, systems, plan, tests and how it was worked out |

What every game would share stays with NFS III's pages until a second
game is found to share it (docs/DOCUMENTATION_STANDARD.md).

## Code

| Path | What |
|---|---|
| `nfs.cpp` | The `MetaEngine` (detection, making an `Engine`) and the `Engine`: the Play page alone |
| `games.cpp`, `folders.cpp`, `game_files.cpp` | The games and their releases, finding the install's and the CD's files, a file in an archive |
| `formats/` | The games' files: `.viv`, RefPack, `.fsh`, text, `.ffn`, `.fce`, `.frd`, `.col`, `.hrz`, `.bnk`, `.mus`, `.mad` |
| `render/` | Particles, and drawing THRASH's frames on the GPU |
| `ui/` | The window's pages and running them (`runPages`), and the Play page, which shows the game and passes it the input |
| `pages/` | The data viewer, in `reluva-viewer` only: the folders found, the 3D tracks and cars, pictures, text, sounds, movies, with the Play page among them |
| `host/` | The retail game hosted: `GameHost` (Windows, DirectX, sound, input, the network, serial links), THRASH natively and on the GPU |
| `tools/` | `reluva-tool nfs` commands (`reluva-tool help nfs` lists them) |

## Tests

- `reluva-tests nfs` runs its unit tests and its engine test
  (`tests/engines/nfs/`).
- The game-files test: `RELUVA_NFS3=<the install> ctest -L gamedata`; see
  the game's [TESTING.md](games/nfs3/TESTING.md) and
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).

## Terms

- **EA's formats:** RefPack (compression), SHPI (`.fsh` picture banks),
  BIGF (`.viv` archives), PT headers (`.bnk`), SCHl streams (`.mus`), MAD
  movies; NFS III's pages describe each
  ([games/nfs3/formats/](games/nfs3/README.md#this-folder)).
