# LEGO Loco: area arrangements (`.lay`)

> **Status:** decoded · **Code:** `src/engines/legoloco/formats/area_layout.cpp` · **Checked on:** the CD's resource file, version 1.0.18.2: `layouts\index.lay` and the five arrangements it names (November 2026) · **Confidence:** seen, not traced unless marked

## Overview

The [resource file](resource.md)'s `layouts` folder holds 17 area
boards (`area1.sav`... in the [board format](sav.md)) and six `.lay`
files that arrange them: `index.lay`, naming the arrangements, and one
file per arrangement, named after it (`3 x 2.lay`). The game's strings
include `%sLayouts\index.lay` and `%sLayouts\%s.lay`. What the
arrangements are for in play, building a board from areas, is
*(guessed)*.

## Layout

Text, a value per line, lines ending CR LF.

### `index.lay`

The arrangements' names, a line each: `3 X 2`, `3 X 1`, `2 X 1`,
`2 X 2`, `3 X 3`. Each is a file's name without `.lay`, in another
case.

### An arrangement

| Line | Value |
|---|---|
| 1 | how many areas |
| 2 | columns of areas |
| 3 | rows of areas |
| then | an area board's name per line, as many as line 1 says |

The areas' count is the columns times the rows in every file.
`2 x 2.lay`:

```text
4
2
2
area1.sav
area3.sav
area7.sav
area9.sav
```

## In Reluva

- `src/engines/legoloco/formats/area_layout.cpp`: `parseLayoutIndex`
  and `parseAreaLayout`.
- `reluva-tool legoloco lay <game folder>` prints each arrangement
  `index.lay` names, and marks any area board the resource file
  doesn't have.

## Sources and checks

- **Where the facts come from:** the six files on the CD, and the file
  names in `loco.exe`'s strings. The code that reads them isn't traced.
- **How it was checked:** `reluva-tool legoloco lay` reads all five
  arrangements, each naming as many boards as it counts, all in the
  resource file. The unit tests (`legoloco.unit.easter_eggs`) read an
  arrangement and an index in the files' shape.
