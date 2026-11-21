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
- **Reading the game's files:** `DATA.dat`, the archive holding nearly
  all of them ([formats/data-dat.md](formats/data-dat.md)); the objects
  in text that hold its rules, maps and missions
  ([formats/text-object.md](formats/text-object.md)); the scripts of its
  words ([formats/rds.md](formats/rds.md)); its pictures
  ([formats/frm16.md](formats/frm16.md)). The sequences, models and maps'
  other files, saved as objects in binary, are still to be read.
- **The data viewer** (`reluva-viewer`) shows the pictures.
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
- **The game** opens `DATA.dat` and reads its rules, then shows an empty
  screen for now; Esc goes back to the launcher.
- **The data viewer:** `reluva-viewer` with the game. Its page of
  pictures: Left/Right step through them, PageUp/PageDown by 10,
  Home/End to the first and last, F to the next folder's; B changes what's
  behind the picture (dark, a checkerboard, magenta), Z the zoom (as large
  as fits, or 1-4 times), I hides the status line. Esc leaves.

## This folder

| Page | What's in it |
|---|---|
| This page | The game, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | A page per file format: [data-dat.md](formats/data-dat.md), [text-object.md](formats/text-object.md), [rds.md](formats/rds.md), [frm16.md](formats/frm16.md), [pal.md](formats/pal.md) |
| `systems/` | A page per part of how the game behaves, as it's worked out |
| [research/](research/README.md) | Working notes: [file-survey.md](research/file-survey.md), what the install holds; [binary-objects.md](research/binary-objects.md), the objects saved in binary |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `stawayteam.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `formats/` | Reading the game's files: `text_object.cpp` (objects in text), `rds_script.cpp` (`.RDS` scripts), `frame.cpp` (pictures) |
| `pages/` | The data viewer, in `reluva-viewer` only: `picture_viewer.cpp` |
| `tools/` | `reluva-tool stawayteam` commands (`reluva-tool help stawayteam` lists them): `archive.cpp` for `DATA.dat`, `text.cpp` for its text, `images.cpp` for its pictures, `files.cpp` for the game's folder |

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
  (`CStarTrekRules`, `CLayerSaveData`...)
  ([formats/text-object.md](formats/text-object.md)).
- **Script:** an `.RDS` file of the game's words, its entries found by
  label ([formats/rds.md](formats/rds.md)).
- **Frame:** a picture in the game's 16-bit format, a `.frm16` file or
  one inside a sequence or model ([formats/frm16.md](formats/frm16.md)).
- **Blended pixels:** a frame's pixels drawn part see-through, with an
  alpha each: edges, shadows, panels ([formats/frm16.md](formats/frm16.md#blended-pixels)).
