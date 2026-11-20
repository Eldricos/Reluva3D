# The AGE engine (Angel Game Engine)

Angel Studios' Angel Game Engine, meant for a family of games: Midtown Madness
2 first. Engine id `age`, in `src/engines/age/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Midtown Madness 2](games/mm2/README.md) | `mm2` | Microsoft, 2000 | Windows | English (by `mm2core.ar`); others by file names, as "unknown release" | Playable: races, Crash Course, cruising, the menus, multiplayer between copies of Reluva |
| Midtown Madness | | Microsoft, 1999 | Windows | | Not yet recognised: after Midtown Madness 2 runs |

Midtown Madness (1999) runs on the same engine, and later Midnight Club
II on PC. They share the archive format, models, textures, the city
description (PSDL) and the car simulation's tuning files. As with
`idtech3`, what they share is to be written once, with what differs
keyed off a `GameInfo` entry (`src/engines/age/games.cpp`).

## Where it stands

Only Midtown Madness 2 is recognised so far: see
[games/mm2/](games/mm2/README.md#where-it-stands).

## Using it

Each game's page says how to add it and play it:
[Midtown Madness 2](games/mm2/README.md#using-it).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its family of games |
| [games/](games/) | A folder per game: [Midtown Madness 2](games/mm2/README.md) |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything was read from Midtown Madness 2.

## Code

| Path | What |
|---|---|
| `age.cpp` | The `MetaEngine` and the `Engine`: the opening movie, then the game's menus |
| `games.cpp` | The games and the detection table |
| `filesystem.cpp` | The game's archives mounted, loose files over them |
| `formats/` | The game's files |
| `game/` | The game's rules: driving, races, city life, multiplayer |
| `render/` | The city, the cars and the pedestrians drawn |
| `audio/` | Engine sounds, ambient sounds, music |
| `ui/` | The menus, the HUD, the screens |
| `pages/` | The data viewer's pages (in `reluva-viewer`) |
| `tools/` | `reluva-tool age` commands (`reluva-tool help age` lists them) |

## Tests

- `reluva-tests age` runs its unit tests and its engine test
  (`tests/engines/age/`).
- A game-files test, with your own copy of the game: see
  [games/mm2/TESTING.md](games/mm2/TESTING.md#the-game-files-test).

## Terms

- **AGE:** the Angel Game Engine, Angel Studios' engine.
- **PSDL:** the city's streets and buildings as blocks of attributes
  ([psdl.md](games/mm2/formats/psdl.md)).
- **`.ar`:** the archives the game's data is kept in
  ([ar.md](games/mm2/formats/ar.md)).
- **`type: a`:** the text form of the tuning files
  ([tune.md](games/mm2/formats/tune.md)).
