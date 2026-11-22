# The LEGO Loco engine

LEGO Loco (Intelligent Games for LEGO Media, 1998), a toy train set on
the Windows desktop: the player lays track, roads and buildings on a
board, and trains, cars and minifigures go about it. An engine for this
one game. Engine id `legoloco`, in `src/engines/legoloco/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| LEGO Loco | `legoloco` | LEGO Media, 1998 | Windows | The CD, version 1.0.18.2 (`Exe/Loco.exe`, `art-res/resource.RFH`), installed or as on the CD | Recognised; shows an empty screen |

The CD holds one release for nine languages: the installer asks which,
and writes it into the install's `Exe/LEGO.INI`. The files are the same
for each, so the one entry recognises them all; English is the language
being worked on first.

## Where it stands

- **Recognising the game:** the CD and an install of it.
- **Reading its files:** the resource file, and its compressed entries
  ([formats/resource.md](formats/resource.md)); the object definitions
  ([formats/dat.md](formats/dat.md)).
- **Not yet:** everything else. The plan is to run the game's own
  `loco.exe` in Reluva's x86 emulator, with stand-ins for the parts of
  Windows and DirectX it calls, after readers and a data viewer for its
  files; see [PLAN.md](PLAN.md).

## Using it

Add the game's folder in the launcher: the install (the folder holding
`Exe` and `art-res`), or the CD itself, which has the same layout. It
then shows an empty screen; Escape goes back.

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan: the ways weighed, the phases, the decisions |
| `formats/` | A page per file format: [the resource file](formats/resource.md), [object definitions](formats/dat.md) |
| `systems/` | A page per part of how the game behaves, as it's worked out |
| [research/](research/README.md) | Working notes: [the first survey](research/first-survey.md) of the CD and `loco.exe` |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `legoloco.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `formats/` | Reading the game's files: `resource_file.cpp`, the resource file; `object_definition.cpp`, the `.dat` object definitions |
| `tools/` | `reluva-tool legoloco` commands (`reluva-tool help legoloco` lists them) |

## Tests

- `reluva-tests legoloco` runs its unit tests and its engine test
  (`tests/engines/legoloco/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **`art-res`:** the game's data folder, on the CD and in an install.
- **Resource file:** `resource.RFH` (its index) and `resource.RFD` (its
  data), the archive holding most of the game's pictures, sounds and
  object definitions ([formats/resource.md](formats/resource.md)).
- **Object definition:** a `.dat` file, the text describing a piece,
  person or button and its picture's frames
  ([formats/dat.md](formats/dat.md)).
- **Frame set:** a named run of a picture's frames, an animation
  ([formats/dat.md](formats/dat.md#the-frame-set-table)).
