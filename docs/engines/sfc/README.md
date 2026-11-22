# The Starfleet Command engine

Engine id `sfc`, in `src/engines/sfc/`. It's meant for Taldren's
Starfleet Command games, which share one engine and its files (ship
tables, `.mod` models, the `sprites.q3` interface archive, mission DLLs),
starting with Starfleet Command Volume II: Empires at War. Starfleet
Command (1999) and Orion Pirates (2001) could follow.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| [Star Trek: Starfleet Command Volume II: Empires at War](games/sfc2/README.md) | `sfc2` | Taldren / Interplay, 2000 | Windows | English retail 1.0; others, the 2.0.3.6 patch among them, as an unknown release | Recognised; nothing runs yet |

## Where it stands

It recognises Starfleet Command II's installs, and shows an empty
screen. The plan is in [games/sfc2/PLAN.md](games/sfc2/PLAN.md).

## Using it

Each game's README says what it needs: [Starfleet Command
II](games/sfc2/README.md#using-it).

## This folder

| Page | What's in it |
|---|---|
| This page | The engine, its games |
| [games/sfc2/](games/sfc2/README.md) | Starfleet Command II: its plan, formats, systems, tests and how it was worked out |

What every game would share stays with Starfleet Command II's pages until
a second game is found to share it (docs/DOCUMENTATION_STANDARD.md).

## Code

| Path | What |
|---|---|
| `sfc.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `tools/` | `reluva-tool sfc` commands (`reluva-tool help sfc` lists them) |

## Tests

- `reluva-tests sfc` runs its unit tests and its engine test
  (`tests/engines/sfc/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: each game's `TESTING.md`
  ([Starfleet Command II's](games/sfc2/TESTING.md)).
