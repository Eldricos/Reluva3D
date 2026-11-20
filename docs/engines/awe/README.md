# The AWE engine (Agatha Christie games)

Engine id `awe`, in `src/engines/awe/`.

## Games

AWE Productions made three Agatha Christie adventures for The Adventure
Company on what looks like one engine, grown from game to game:

| Game | Year | Status |
|------|------|--------|
| Murder on the Orient Express (`motoe`) | 2006 | first target: [games/motoe/](games/motoe/README.md) |
| And Then There Were None (`attwn`) | 2005 | second: [games/attwn/](games/attwn/README.md), its files read; the older version (scenes and saves are `.ac1`) |
| Evil Under the Sun (`euts`) | 2007 | later; expected to be closest to Orient Express |

As with id Tech 3, one Reluva engine (`src/engines/awe/`, `awe` in
`engines.cfg`) serves the family. What the shared code needs to know of
each game is in a table (`games.h`); each game's own code is in a folder of
its own (`motoe/`, `attwn/`), as docs/ADDING_AN_ENGINE.md lays engines out.
Orient Express was built first; And Then There Were None is being added.

## Where it stands

- Murder on the Orient Express plays from a new game to the end credits:
  [games/motoe/](games/motoe/README.md#where-it-stands).
- And Then There Were None is recognised, its files and scenes read and
  its scenes drawn, but not played yet:
  [games/attwn/](games/attwn/README.md#where-it-stands).

## Using it

Each game's page says how to add it and play it:
[Murder on the Orient Express](games/motoe/README.md#using-it),
[And Then There Were None](games/attwn/README.md#using-it).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its family of games |
| [formats/](formats/) | What every game on it shares: [Granny 2 (`.gr2`)](formats/gr2.md), the model and animation files of RAD Game Tools' Granny 3D, whose runtime (`granny2.dll`) the games call |
| [games/](games/) | A folder per game, for what was worked out from it |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything but Granny was read from Murder on the Orient Express.

## Code

| Path | What |
|---|---|
| `awe.cpp` | The `MetaEngine` and the `Engine` |
| `games.h`, `games.cpp` | What the shared code needs to know of each game, and the detection table |
| `formats/` | The files every game on it reads: scenes (`.ac2`), Granny models, Bink movies, saves, tables |
| `game/` | The shared game: scenes, actors, the walk grid, scripts, dialogue, the story, saves |
| `render/`, `ui/` | Drawing the scenes, and the game's screens |
| `motoe/`, `attwn/` | Each game's own code: its puzzles, and for And Then There Were None its acts, journal and inventory |
| `pages/` | The data viewer's pages (in `reluva-viewer`) |
| `tools/` | `reluva-tool awe` commands (`reluva-tool help awe` lists them) |

## Tests

- `reluva-tests awe` runs its unit tests and its engine test
  (`tests/engines/awe/`).
- A game-files test, with your own copy of each game: see
  [motoe's TESTING.md](games/motoe/TESTING.md#the-game-files-test-with-your-own-copy-of-the-game)
  and
  [attwn's](games/attwn/TESTING.md#the-game-files-test-with-your-own-copy-of-the-game).
