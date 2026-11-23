# The LEGO Loco engine

LEGO Loco (Intelligent Games for LEGO Media, 1998), a toy train set on
the Windows desktop: the player lays track, roads and buildings on a
board, and trains, cars and minifigures go about it. An engine for this
one game. Engine id `legoloco`, in `src/engines/legoloco/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| LEGO Loco | `legoloco` | LEGO Media, 1998 | Windows | The CD, version 1.0.18.2 (`Exe/Loco.exe`, `art-res/resource.RFH`), installed or as on the CD | Plays: its movies, front screen, layouts and trains, the Toybox, postcards, saving and loading; network play to come |

The CD holds one release for nine languages: the installer asks which,
and writes it into the install's `Exe/LEGO.INI`. The files are the same
for each, so the one entry recognises them all; English is the language
being worked on first.

## Where it stands

- **Recognising the game:** the CD and an install of it.
- **Reading its files:** the resource file, and its compressed entries
  ([formats/resource.md](formats/resource.md)); the object definitions
  ([formats/dat.md](formats/dat.md)); the boards: layouts, screen saver
  boards and saves ([formats/sav.md](formats/sav.md)); the date-based
  surprises ([formats/ee-ini.md](formats/ee-ini.md)); the area
  arrangements ([formats/lay.md](formats/lay.md)); postcards
  ([formats/crd.md](formats/crd.md)).
- **The data viewer** (`reluva-viewer`): the boards drawn with their
  pieces, every picture cut into its frames with its animations played,
  the object definitions and the sounds
  ([systems/data-viewer.md](systems/data-viewer.md)).
- **Playing it:** the game's own `loco.exe` runs in Reluva's x86
  emulator, with stand-ins for the parts of Windows and DirectX it calls:
  its movies, music and sound, its front screen, its layouts with their
  trains, the Toybox, laying track, starting and building trains at a
  depot, the post office and postcards, saving and loading layouts,
  its windowed mode, and leaving, played with the mouse and keys
  ([systems/running.md](systems/running.md)).
- **Not yet:** Windows' own pointer, and network play (postcards
  between players among it), aren't done. See [PLAN.md](PLAN.md).

## Using it

Add the game's folder in the launcher: the install (the folder holding
`Exe` and `art-res`), or the CD itself, which has the same layout. It
then runs the game in an 800 by 600 window; what it writes (`LEGO.INI`,
its layouts) goes to the game's save folder. Closing the window asks
first, as a layout not saved would be lost. `reluva-viewer` opens its
data viewer ([systems/data-viewer.md](systems/data-viewer.md)).

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan: the ways weighed, the phases, the decisions |
| `formats/` | A page per file format: [the resource file](formats/resource.md), [object definitions](formats/dat.md), [boards](formats/sav.md), [`EE.INI`](formats/ee-ini.md), [area arrangements](formats/lay.md), [postcards](formats/crd.md), [resource numbers](formats/resource-numbers.md) |
| `systems/` | A page per part of how the game behaves: [how pictures are drawn](systems/drawing.md), [the data viewer](systems/data-viewer.md), [running the program](systems/running.md) |
| [research/](research/README.md) | Working notes: [the first survey](research/first-survey.md) of the CD and `loco.exe`, [postcards](research/postcards.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `legoloco.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `formats/` | Reading the game's files: `resource_file.cpp`, the resource file; `object_definition.cpp`, the `.dat` object definitions; `board.cpp`, the boards; `easter_eggs.cpp`, `EE.INI`; `area_layout.cpp`, the `.lay` files |
| `game_files.cpp` | A copy's files: its resource file and the resources' names |
| `host/` | Running `loco.exe` in the x86 emulator: `game_process.cpp` |
| `pages/` | The data viewer, in `reluva-viewer` only |
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
- **Resource number:** a resource's id in `loco.exe`'s string table,
  whose string is its name ([formats/resource-numbers.md](formats/resource-numbers.md)).
- **Square:** a board's 16-pixel cell ([systems/drawing.md](systems/drawing.md)).
- **Board:** the play area, with its squares, pieces, minifigures and
  trains; a `.sav` file holds one ([formats/sav.md](formats/sav.md)).
- **Frame set:** a named run of a picture's frames, an animation
  ([formats/dat.md](formats/dat.md#the-frame-set-table)).
