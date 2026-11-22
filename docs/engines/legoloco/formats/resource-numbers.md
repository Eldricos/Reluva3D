# LEGO Loco: resource numbers (`loco.exe`'s string table)

> **Status:** decoded · **Code:** `src/engines/legoloco/game_files.cpp` · **Checked on:** `loco.exe` 1.0.18.2 and every board on the CD (November 2026) · **Confidence:** seen, not traced unless marked

## Overview

The game's files refer to resources by number: a board's pieces and
minifigures ([sav.md](sav.md)), a definition's `RMBSeq` and
`PossibleMinifigs` ([dat.md](dat.md)), `EE.INI`'s events
([ee-ini.md](ee-ini.md)). A number is the id of a string in
`loco.exe`'s string table, and the string is the resource's name: its
path in the [resource file](resource.md) without the extension.

| Number | String |
|---|---|
| 1024 | `backdrop\backdrop` |
| 2062 | `building\house8` |
| 3164 | `track\post` |
| 4118 | `scenery\tree1` |
| 7230 | `people\police2\police2` |
| 12288 | `paths\path` |
| 14451 | `misc\cloud8` |

## Layout

Windows' string table resources (type 6): blocks of sixteen strings,
block n holding ids (n - 1) × 16 to n × 16 - 1, each a 16-bit length and
UTF-16 text. All of `loco.exe`'s are in one language, neutral. Of its
1,867 strings, 1,039 are paths with `\` between folders; the rest are
the interface's words and the minifigures' names (2 to 47 are names:
`Bruce`, `Dee`...).

A resource's files are found by adding an extension to the name: `.bmp`
for its picture, `.dat` for its definition, `.but` for its toolbox
button, `.wav` for a sound *(inferred)*.

## In Reluva

- `src/engines/legoloco/game_files.cpp`: `readResourceNames` reads the
  string table (`readPeStrings`, `src/common/pe.cpp`) and keeps the
  strings that are paths; `GameFiles::findResource` finds a number's
  entry in the resource file for an extension.
- The data viewer's boards page draws each piece by its number
  ([data-viewer.md](../systems/data-viewer.md)).

## Sources and checks

- **Where the facts come from:** the string table read from `loco.exe`,
  and the numbers in the CD's boards and definitions looked up in it.
  The code that looks a number up isn't traced.
- **How it was checked:** every one of the 12,294 pieces and 137
  minifigures on the CD's 28 boards (`~curr` being a copy of one) names a
  string that is a path, and that path with `.bmp` is in the resource
  file, so the viewer's boards page draws every piece. The unit tests (`legoloco.unit.game_files`)
  read a string table built in the test, keeping only its paths.
