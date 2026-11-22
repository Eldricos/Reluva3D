# SimCoaster: settings files (`.sam`)

> **Status:** decoded · **Code:** `src/engines/tpw/formats/sam.cpp` · **Checked on:** the US release's 369 settings files · **Confidence:** confirmed unless marked

## Overview

Much of how the game plays is in plain-text settings files: 369 of them,
loose in `Data/` (`Config.sam`, the detail levels' `low.sam`, `med.sam`,
`high.sam`, `sound.sam`, `Challenges.sam`, the missions'
`missions/miss00.sam` to `miss17.sam`) and in the archives, one for each
ride, shop, sideshow, feature and obstacle, beside each world's general
ones (`levels/<world>/rides/Rides.sam`, `levels/<world>/Standard.sam`).
Their comments still call the game Theme Park World, or "Theme Park 2".
Between them they hold 15,825 settings: ride costs, guests' behaviour,
weather, research, the shapes things take on the ground.

## Layout

Text in the Windows code page, lines ending `\r\n` (`\n` in 5 files).
Spaces and tabs around a line, and between its parts, don't count. A line
is one of:

### A comment, or nothing

A line starting `#`, or an empty one.

### A setting

A name, then its values:

```text
Info.Id                 1304
Info.Name               "Fruit Box"
Upgrades[0].CostOfUpgrade 1200
PeepTypes[0].PreferredExcitement.StartingCash.BoredomThreshold.Generation 90 300 40 0
```

- **The name** is a group and fields, joined by dots: words of letters,
  digits and `_`, each with an optional index in brackets (`Upgrades[0]`,
  0 to 44 here). A name has as many values as it has fields after the
  group: one for `Info.Id`, four for the `PeepTypes[0]...` line above.
- **A value** is a number (`1304`, `-2`, `0.5`) or text between double
  quotes, which may hold `'` but never `"`.
- **Anything after the values** is a comment, with or without a `#` or
  `//` before it: `Research.Group 0 0 = Available initially` is one
  value, 0, then a comment that happens to start with a number.

59 names come twice in one file, most with the same value both times; a
file is read top to bottom, so the last is taken to count *(guessed)*.
Two field names are spelt in two ways (`FileName` and `Filename`,
`WEATHER` and `Weather`), so names are matched ignoring case *(inferred)*.

### A grid

A name with no values, then rows between two lines of `---`:

```text
Info.Shape
---
***
***
*2*
---
```

432 of the settings are grids: the ground a thing stands on (`Info.Shape`,
`Obstacle.AfterShape`) and its hoardings (`Info.Hoarding`), a character a
cell. What each character means is the game's, and is to be worked out
with the things that use them.

## In Reluva

`SamFile` (`src/engines/tpw/formats/sam.cpp`) reads a file into its
settings, each with its line, values and grid, and looks a name up
ignoring case, the last of a name counting. A line that isn't a comment,
a setting with all its values, or a grid with both its fences is an
error, saying which line.

`reluva-tool tpw sam-check <game folder>` reads every settings file of a
game, with a name lists that one's settings; the game-files test runs it
(`tpw.gamedata.settings`).

## Sources and checks

- **Where the facts come from:** the US release's settings files, every
  line sorted by its shape (November 2026): every value turned out a
  number or quoted text, and every line with more values than one has as
  many fields after its group; the lines with numbers after their value
  all have comments there ("0 = Available initially").
- **How it was checked:** all 369 files read with `reluva-tool tpw
  sam-check`, none failing. The unit tests (`tpw.unit.formats`) read a
  file of each kind of line, and turn away broken ones.

## Open questions

- Whether a later setting of one name wins, as Reluva takes it, or the
  first.
- How the general files (`Rides.sam`, `Standard.sam`) and each thing's own
  combine: the ride's own presumably over the general one *(guessed)*.
