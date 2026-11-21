# The LEGO Island engine

LEGO Island (Mindscape, 1997), an open-world adventure on a LEGO island
for young players: an engine for this one game. Its code is almost all in
`LEGO1.DLL`, the "LegoOmni Library", which is to be run in Reluva's x86
interpreter with Windows and DirectX stood in for ([PLAN.md](PLAN.md)).
Engine id `legoisland`, in `src/engines/legoisland/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| LEGO Island | `legoisland` | Mindscape / LEGO Media, 1997 | Windows | English 1.0 (`LEGO1.DLL`, `WORLD.WDB`); any other by its files' names, as an unknown release | Recognised and imported; nothing runs yet |

## Where it stands

It recognises the English 1.0 release, and other releases by their
files' names, and imports the game from its CD into one folder
([systems/game-files.md](systems/game-files.md)). It reads the SI
archives, and gives back their pictures, sounds and animations as the
files they were made from ([formats/si.md](formats/si.md)), which the
data viewer shows and plays, and the world database's models, parts and
textures ([formats/wdb.md](formats/wdb.md)), which it draws in 3D, and the
worlds' lists of animations ([formats/dta.md](formats/dta.md)). Run,
it shows an empty screen. The plan is in [PLAN.md](PLAN.md).

## Using it

Nothing to play yet. *Import Game...* in the launcher, with the CD (or a
copy of it), copies everything the game needs, 520 MB, into a folder of
its own; started from an install folder, *Add CD or installed game...*
adds the CD. *Add Game...* recognises an install folder as it is, but
without the CD's scripts the game won't be able to run from it.

**The data viewer:** `reluva-viewer` with the game; Tab goes from page to
page.

- **SI files:** their objects, as a tree, and the selected one's
  details; a still is drawn, and a sound, FLC or Smacker animation
  played. Up/Down pick an object (PageUp/PageDown by 20, Home/End),
  Left/Right the SI file, M shows media only or every object, Space
  plays or stops, A plays each as it's picked.
- **`WORLD.WDB`'s textures:** each drawn as large as fits, with its
  palette. Up/Down pick one (PageUp/PageDown by 20, Home/End).
- **`WORLD.WDB`'s models and parts,** in 3D (it needs a GPU with Vulkan
  or Metal): Up/Down pick one (PageUp/PageDown by 20, Home/End), L a
  less detailed LOD, drag to turn, the wheel or +/- to come nearer, R
  to reset the view, I to hide the details.

## This folder

| Page | What's in it |
|---|---|
| This page | The games, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | A page per file format: [si.md](formats/si.md) (the SI archives), [wdb.md](formats/wdb.md) (the world database), [dta.md](formats/dta.md) (the worlds' animation lists) |
| [systems/](systems/) | How the game behaves: [game-files.md](systems/game-files.md) (its files, and telling releases apart) |
| `research/` | Working notes: address maps, raw findings, notes on recordings |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `legoisland.cpp` | The `MetaEngine` (name, making an `Engine`, importing) and the `Engine` |
| `detection.cpp` | The releases it recognises, and finding and copying the game's parts for an import |
| `formats/dta.cpp` | The worlds' `*INF.DTA` files: their animations and the models each uses |
| `formats/si.cpp` | The SI archives: their objects, their chunks, and their media as standard files |
| `formats/wdb.cpp` | The world database: its worlds, parts, models and textures |
| `pages/` | The data viewer (`reluva-viewer` only): `si_viewer.cpp`, the SI files' page; `wdb_viewer.cpp`, the world database's textures and models |
| `tools/` | `reluva-tool legoisland` commands (`reluva-tool help legoisland` lists them) |

## Tests

- `reluva-tests legoisland` runs its unit tests and its engine test
  (`tests/engines/legoisland/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **Omni:** Mindscape's engine under the game; `LEGO1.DLL` names its
  classes `Mx...` (the engine) and `Lego...` (the game).
- **SI (Interleaf):** the archives on the CD holding the game's scripts
  and the media they play, laid out to be streamed from the CD.
- **Presenter:** the class an SI object names to play it (a sound, an
  animation, a model).
- **Object, chunk, piece:** an SI file's `MxOb` records, the `MxCh`
  chunks of their data, and the pieces (a header, a frame, a second of
  sound) the chunks join into ([formats/si.md](formats/si.md)).
- **World:** a part of the game with its own models (`ACT1`, the
  island in Act 1; `HOSP`, the hospital), as `WORLD.WDB` lists them.
- **Model, part, ROI, LOD:** a thing in a world; a piece models share
  (a steering wheel, a hat); an ROI ("real-time object instance"), one
  thing drawn; and its levels of detail ([formats/wdb.md](formats/wdb.md)).
