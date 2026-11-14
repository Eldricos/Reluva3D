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
- And Then There Were None is recognised and its files read, but not
  played yet: [games/attwn/](games/attwn/README.md#where-it-stands).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its family of games |
| [formats/](formats/) | What every game on it shares: [Granny 2 (`.gr2`)](formats/gr2.md), the model and animation files of RAD Game Tools' Granny 3D, whose runtime (`granny2.dll`) the games call |
| [games/](games/) | A folder per game, for what was worked out from it |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything but Granny was read from Murder on the Orient Express.
