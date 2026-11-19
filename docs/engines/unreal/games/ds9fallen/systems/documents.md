# The Fallen: documents and objectives

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_documents.cpp` · **Checked against:** the scripts and the game running in Reluva · **Confidence:** from the code unless marked

## Overview

The documents, the level's objectives and title, read from `int/`
([int.md](../formats/int.md)) and shown on the info pad.

## How it works

`vm_documents.cpp` reads The Fallen's other text files in `int`
([int.md](../formats/int.md)) for `CTVE_Document`, which the HUD
spawns. At the start of a level (its `Timer`), the document actor does
three things:
- **Lists the documents** the player has, by asking `FindDocument` for
  `Document_0` to `Document_29`. A section of `Document.int` with that
  name is there: `FoundDocument`. That reads its settings and lists the
  document if its `MissionLevel` is at most the player's mission
  (`GetMissionLevel`). Mission 1 has none, mission 5 ten, mission 11 all
  eleven.
- **Reads the objectives:** each `Objective_N` of the map's own `.int`
  (`GetMapTitle()` + `.int`). Its title and whether it's critical and
  active go into LevelInfo's `Objective_Title`, `Objective_bCritical`
  and `Objective_State` (0 active, −1 not yet). M01_KiraL1A starts with
  7, three of them active.
- **Reads the level's title** from `SaveGameString` into LevelInfo's
  `Title`: M01_KiraL1A is "Bajor - Monastery".

The natives:
- `GetMapTitle()`: the map's name (`M01_KiraL1A`). Its callers make a
  file name of it, or show it in capitals.
- `FindDocument(Section)`: sends `FoundDocument(Section)` if
  `Document.int` has that section.
- `LocalizeDocument(Name, File)`: the section's settings become the
  actor's properties of the same names (`Title`, `MissionLevel`,
  `bOnlyThisMission`, `bCritical`, `bActive`), each converted to its
  type. As localised properties are, by name.
- `LoadDocument(Name, File)`: the section's lines, in order, each to the
  actor's event that draws it: `Color` (`SetColor`), `Text`
  (`LoadText`, a line), `IMAGE` (`DefineImage`), `Title` (`SetTitle`).
  An image's `Justify=` becomes the HUD's justification: it takes 2 as
  right and 3 as centred, anything else as left. Those events draw
  through the HUD onto its canvas (the Grigari report: 12 lines and an
  image).

The Maps page shows the level's title and its active objectives while
playing.

## In Reluva

`vm/vm_documents.cpp` answers `CTVE_Document`'s natives.

## Sources and checks

- **Where the facts come from:** `CTVE_Document`'s script and the
  `int` files.
- **How it was checked:** in play (above).
