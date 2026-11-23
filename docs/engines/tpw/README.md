# The Theme Park World engine

Bullfrog's engine for its 3D theme park games, meant for the family that
shares its files: SimCoaster first (Theme Park Inc in Europe), then Theme
Park World (Sim Theme Park in the US). Engine id `tpw`, in
`src/engines/tpw/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [SimCoaster](games/simcoaster/README.md) | `simcoaster` | Electronic Arts, 2001 | Windows | US (by `Data/ui.wad` and the American string tables); others by file names, as "unknown release" | Recognised, and imported from its CD; plays its opening, then its loading screen |
| Theme Park World | | Electronic Arts, 1999 | Windows | | Not yet recognised: after SimCoaster runs |

SimCoaster is Theme Park World's engine and file formats with new parks,
rides and a coaster designer: its own settings files still call it
"Theme Park World" ([games/simcoaster/README.md](games/simcoaster/README.md)).
What the two games share is to be written once, with what differs keyed
off a `GameInfo` entry (`src/engines/tpw/games.cpp`).

## Where it stands

Only SimCoaster is recognised so far: see
[games/simcoaster/](games/simcoaster/README.md#where-it-stands).

## Using it

Each game's page says how to add it and play it:
[SimCoaster](games/simcoaster/README.md#using-it).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its family of games |
| [games/](games/) | A folder per game: [SimCoaster](games/simcoaster/README.md) |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything was read from SimCoaster.

## Code

| Path | What |
|---|---|
| `tpw.cpp` | The `MetaEngine` and the `Engine`: the opening, then the loading screen for now |
| `games.cpp` | The games, the detection tables, and importing from the CD |
| `game_files.cpp` | The `Data` folder mounted: the archives, loose files over them |
| `textures.cpp` | A model's textures found among the files, and pictures decoded |
| `script/` | The script machine, and a stand-in for the park to run scripts against |
| `ui/` | The screens: the opening, the loading screen; a model and a world's terrain drawn in 3D |
| `pages/` | The data viewer's pages (in `reluva-viewer`) |
| `formats/` | The games' files: `.wad`, `.sam`, `.str`, `.bf4`, `.sdt`, `.MD2` and their animations, `.hmp`, `.lnd`, `base.map`, `scape.omp`, `.RSE`; their pictures are read by the shared code (`src/image/`) |
| `tools/` | `reluva-tool tpw` commands (`reluva-tool help tpw` lists them) |

## Tests

- `reluva-tests tpw` runs its unit tests and its engine test
  (`tests/engines/tpw/`).
- What they cover, and what to check by hand:
  [games/simcoaster/TESTING.md](games/simcoaster/TESTING.md).

## Terms

- **Bullfrog:** the studio that made the engine and both games, by then
  part of Electronic Arts.
- **SafeDisc:** the copy protection on the games' executables, which
  keeps them from being read or run
  ([executable.md](games/simcoaster/formats/executable.md)).
