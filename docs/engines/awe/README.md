# The AWE engine (Agatha Christie games)

Engine id `awe`, in `src/engines/awe/`.

## Games

AWE Productions made three Agatha Christie adventures for The Adventure
Company on what looks like one engine, grown from game to game:

| Game | Year | Status |
|------|------|--------|
| Murder on the Orient Express (`motoe`) | 2006 | first target: [games/motoe/](games/motoe/README.md) |
| And Then There Were None (`attwn`) | 2005 | later; the older version (saves are `.ac1`) |
| Evil Under the Sun (`euts`) | 2007 | later; expected to be closest to Orient Express |

As with id Tech 3, one Reluva engine (`src/engines/awe/`, `awe` in `engines.cfg`)
should serve the family, with per-game differences in a table, rather than
one engine per game. Orient Express is built first. The other two get
added once their file lists have been compared against it.

## Where it stands

Only Murder on the Orient Express is recognised so far: see
[games/motoe/](games/motoe/README.md#where-it-stands).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine and its family of games |
| [formats/](formats/) | What every game on it shares: [Granny 2 (`.gr2`)](formats/gr2.md), RAD's model and animation format |
| [games/](games/) | A folder per game, for what was worked out from it |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything but Granny was read from Murder on the Orient Express.
