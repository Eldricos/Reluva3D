# SimCoaster: park awards

> **Status:** partial · **Code:** `src/engines/tpw/park/awards.cpp`, `src/engines/tpw/ui/info_windows.cpp` · **Checked against:** the settings, the string tables · **Confidence:** guessed unless marked

## Overview

A park wins awards for the highest and fastest coaster, the cleanest
park, the most decoration, the best security, the best staff training,
the fastest go-kart track and the longest water ride (`UITEXT` 453 to
460). The Park menu's Park Awards (850) lists them ("Park Awards - {20}",
452), each the park's or "None" (472). The last mission, Operation Boss!,
asks for six of them ("Win {0} park awards", `STAT_TITLES` 5; "Park
awards currently held: {0}", 6; [missions.md](missions.md)). The
recording never opens the window.

## How it works

### The settings

The general settings (`levels/Standard.sam`) give each award its mark,
with the makers' comments:

| Setting | Value | What it is |
|---|---|---|
| `ParkAward.CoasterHeight`, `CoasterSpeed` | 110, 100 | a coaster's height and speed |
| `ParkAward.Hygiene` | 90 | "Percentage of unlittered cells in the park" |
| `ParkAward.HygieneMinParkSize` | 1000 | "Min num cells covered by objects to qualify for the hygiene award" |
| `ParkAward.Security` | 25 | "percentage of park covered by guard patrol areas + percentage covered by security cameras" |
| `ParkAward.Decoration` | 15 | "percentage of cells covered by objects, which are features" |
| `ParkAward.Training` | 50 | "percentage of staff that have been trained in a special ability" |
| `ParkAward.TrainingMinStaff` | 10 | the staff needed to qualify, as its name says |
| `ParkAward.GokartSpeed`, `WaterLength` | 5, 50 | a go-kart track's speed, a water ride's length |
| `ParkAward.MinCellsOwned`, `MinCellsCovered` | 3000, 3000 | not known; Reluva doesn't use them |

### Held

An award is held while the park measures up to its mark, counted afresh
whenever it's asked for *(guessed)*; nothing in the string tables
announces one won or lost. Reluva measures them so:

- **Cleanest Park:** the path and queue cells with no litter on them, a
  percentage of all of them, once `HygieneMinParkSize` cells are covered
  by objects. "Cells in the park" are taken as those people walk.
- **Most Decoration:** the cells covered by features (the `features`
  folder's objects, not obstacles), a percentage of the cells covered
  by objects.
- **Best Security:** the park's cells inside guards' patrol areas, a
  percentage of the park's cells, plus those within six cells of a
  security camera (`UsageInfo.ProvidesSecurity`), the reach Reluva gives
  a camera against pranks ([visitors.md](visitors.md)).
- **Best Staff Training:** the staff trained in an ability, a percentage
  of all the staff, with at least `TrainingMinStaff` of them.
- **Fastest Go-kart Track:** a whole go-kart track's longest straight, in
  blocks of two cells, round its end, as `GokartSpeed`'s comment says
  ("just the longest straight section of track") *(guessed)*
  ([tracks.md](tracks.md)).
- **Longest Water Ride:** a whole water track's length, in cells
  *(guessed)*.
- **The coasters':** Reluva builds no coasters yet, so they're never
  held, and Operation Boss! asks for six of eight: it can be done only
  with the rest.

The Park Awards window shows a held award's measure as a percent, and
"None" for the others, laid out by guess.

## In Reluva

`Park::awards()` and `awardsHeld()` (`src/engines/tpw/park/awards.cpp`);
a mission's statistic 6 is `awardsHeld()`. `AwardsWindow` draws the
window. `reluva-tool tpw window-shot <game folder> <world folder>
<out.png> awards` draws it and prints each award's measure. The unit
tests (`tpw.unit.park`) win the decoration, security, hygiene and
training awards on a world built in the test, and lose decoration as the
park grows.

## Sources and checks

- **Where the facts come from:** the general settings and their
  comments; `UITEXT.str`, `STAT_TITLES.str`; the missions' settings.
- **How it was checked:** `window-shot awards` over Land of Invention's
  prebuilt park: its paths all clean, but too few cells covered to
  qualify; ten percent of them features. None of it has been set against
  the game.

## Open questions

- How each award is measured: which cells count as the park's, whether
  the measure is a record held against other parks, and how often it's
  judged.
- What `MinCellsOwned` and `MinCellsCovered` limit.
- How the coasters' height and speed, the go-kart tracks' speed and the
  water rides' length are measured.
- Whether winning or losing an award is announced.
