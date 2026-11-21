# The Star Trek: Away Team engine

Star Trek: Away Team (Reflexive Entertainment / Activision, 2001), a
real-time tactics game in 2D. An engine for this one
game, rebuilt from its data files and from reading its executable.
Engine id `stawayteam`, in `src/engines/stawayteam/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| Star Trek: Away Team | `stawayteam` | Activision, 2001 | Windows | US English retail (`Star Trek.exe`, `DATA.dat`); any other by its files' names, as an unknown release | Recognised; shows an empty screen |

## Where it stands

- **Recognising the game:** done for the US English retail release, and
  *Import Game...* from its CD.
- **Everything else** is still to come. The plan is in [PLAN.md](PLAN.md),
  and a first survey of the game's files is in
  [research/file-survey.md](research/file-survey.md).

## Using it

- **Add the game's folder** (the install) in the launcher. A full install
  has everything the game needs: the program, `DATA.dat`, and the movies
  in `DATA/Movies/`.
- **Or import it from the CD** (*Import Game...*): Reluva copies what it
  uses from the CD's `Setup` folder, where the install is loose: the
  program, `DATA.dat`, `texture.dat` and the movies. An installed copy
  can be imported the same way.
- **The game** shows an empty screen for now; Esc goes back to the
  launcher.

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | A page per file format, as it's worked out |
| `systems/` | A page per part of how the game behaves, as it's worked out |
| [research/](research/README.md) | Working notes: [file-survey.md](research/file-survey.md), what the install holds |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `stawayteam.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `tools/` | `reluva-tool stawayteam` commands (`reluva-tool help stawayteam` lists them) |

## Tests

- `reluva-tests stawayteam` runs its unit tests and its engine test
  (`tests/engines/stawayteam/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **Away team:** the officers the player picks for a mission from the
  ship's roster.
- **`DATA.dat`:** the archive holding nearly all the game's data: a ZIP
  file, with one change ([research/file-survey.md](research/file-survey.md)).
- **Object text:** the game's text format for its rules, maps and
  missions: nested blocks, each named by the C++ class it is read into
  (`CStarTrekRules`, `CLayerSaveData`...).
