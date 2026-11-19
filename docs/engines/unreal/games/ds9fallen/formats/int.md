# The Fallen: documents, objectives and map strings (`int/*.int`)

> **Status:** decoded · **Code:** `src/engines/unreal/vm/vm_documents.cpp`, `vm/vm.cpp` · **Checked on:** the retail release (October 2026) · **Confidence:** confirmed unless marked

## Overview

Beside the dialogue tables ([dialogue-tables.md](dialogue-tables.md)), the install's
`int` folder holds `Document.int`, the readable documents, and one `.int`
per map, named after it (`M01_KiraL1A.int`). Some maps have none (the cut
scenes'); a few names have no map (`M01_KiraL1.int`, `M10_L2.int`).

## Layout

They're Windows text files, lines ending CR LF, laid out as `.ini` files:
`[Section]` lines and `Key=Value` lines under them. Some start with a row
of asterisks. A key can come many times in a section, and the order
counts: the lines are drawn in it.

`Document.int` has a section for each document, `[Document_1]` to
`[Document_11]`:

| Key | Meaning |
|---|---|
| `Title` | its title in the list ("GRIGARI") |
| `MissionLevel` | the first mission it's shown in (3, 5 or 10) |
| `Color` | the colour of the next line: Purple, PurpleBright, Orange, OrangeBright, Gold, GoldBright, Blue, BlueBright (`CTVE_Document.SetColor`) |
| `Text` | a line of text, empty for a blank line |
| `IMAGE` | a texture to show (`DS9_Objectives.M11_Grigari`), then options (`Justify=Left`, the only one used) |

A map's `.int` has:
- **`[SaveGameString]`**: its `Title` ("Bajor - Monastery").
- **`[Objective_1]` onwards**: each objective's `Title`, `bCritical` and
  `bActive` (True or False, any case), its lines (`Color`, `Text`,
  `IMAGE`), and a `CompleteMessage`.
- **A section for each of its actors that shows text**, named after the
  actor's tag (`[consx1]`). These hold `MESSAGE`, the tricorder's
  `TRICORDERNAME`, `TRICORDERBIG1`/`2` and `TRICORDERSUB1` to `3`,
  `SCANSTRING` and a few others: the actors' localised strings.
  The actor names its own section and file: The Fallen's Actor has
  `TextInfo` (the section) and `FileName` (the file, with or without
  `.int`), under the FileInfo category. M01_WorfL1's
  CTVE_TricorderInfo1 has `TextInfo="WarpCore"`, `FileName="M01_WorfL1"`:
  its tricorder reading is `[WarpCore]` in `M01_WorfL1.int`. The section
  is usually, but not always, the actor's tag. Each key is one of the
  actor's class's `localized` properties, set from the text as the map
  is loaded (after its own properties, so the text wins over what the
  map was saved with); no script reads TextInfo or FileName for this
  (CTVE_Dialog takes TextInfo as its dialogue's name), so it's the
  engine's own doing.

  That is why some files are named for no map: the maps split in two
  later (M01_KiraL1A and M01_KiraL1B; M06_SiskoL1A to C) still name the
  file of the map they came from (`M01_KiraL1.int`), whose own `.int`
  holds only the title and objectives. Actors with no TextInfo take

## Behaviour

  nothing from these files.

The class-level strings (a class's `localized` defaults) come from
`System/<Package>.int` and `int/Classes.int`, under `[ClassName]`
sections; those are read with the class defaults, not from here.

## In Reluva

The documents and the maps' objectives and titles are read by
`vm/vm_documents.cpp` ([systems/documents.md](../systems/documents.md)),
the actors' strings by `Vm::applyLocalizedActor` (`vm/vm.cpp`).

## Sources and checks

- **Where the facts come from:** the install's `int` folder, and The
  Fallen's scripts (which of them read what).
- **How it was checked:** on The Fallen (October 2026).
