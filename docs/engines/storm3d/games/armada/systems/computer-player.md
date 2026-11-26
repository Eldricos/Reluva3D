# Star Trek: Armada: the computer player

> **Status:** partial · **Code:** `src/engines/storm3d/play/computer_player.cpp`, `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, without the shroud, its frames every two seconds and draws every ten, watching its computer player's base · **Confidence:** published (the build lists, the README), where it builds inferred from one game

## Overview

A skirmish's computer player builds by its race's build list,
`AI\AIPs\<race>_build_list.aip` (`federation_build_list.aip` and the
Klingons', Romulans' and Borg's), in order; and, by the game's own
description of it (`AI\AIPs\Scheduler_README.txt`, shipped with the
game), sends its ships to goals on a strategic map of 400-unit cells
(`AI\strategic_AI_config.txt`) weighed by its personality's `.aip`
priorities. This page covers its building and its ships' goals.

## How it works

### The build list

The list is C, read as the game's other settings
([config.md](../formats/config.md)): `the_build_list`, an array of
`Better_Build_List_Element` (`aipdef.h`): a unit's definition, how many
of it, and for a station which base it's by (`m_base_num`, "only used
for stations"):

```text
"fbase",    1,0,    // first base
"fconst",   1,0,    // we only need one at first
"fmining",  1,1,    // two mining facilities
"ffreight", 2,0,    // and two freighters for each
"fturret",  1,1,    // defend the mining stations
"fmining",  1,2,
```

Its counts add up by type: the second `fmining` asks for two in all.

### Watching the original

With the shroud off (Game Setup's shroud list held open and let go on
"Shroud Off, Fog Off"), the computer player started at `Camera_4`, its
units red on the minimap; the camera was taken there by a click on it.

- **First,** before 44 seconds, one of its construction ships was
  building a Mining Station: the list's first entry it didn't meet (it
  starts with its starbase and two construction ships). The station
  stood at about (5673, 709), 357 from the nearest moon (5927, 960) and
  369 from its starbase, its builder waiting 99 from it, as the
  player's do.
- **Then,** freighters went to the moon, and by 105 seconds a Pulse
  Cannon (`fturret 1,1`, "defend the mining stations") stood at (5803,
  839): the middle of the mining station and its moon, to the unit.

The positions were placed back in the world from its models in the
draws, relative to its starbase (where its spiral of starting units
puts it), so a camera a little off didn't move them.

## In Reluva

- **The list** (`ComputerPlayer`): read through the configuration reader;
  the next entry is the first whose type the player has fewer of, built
  or on order, than it and those before it ask for together.
- **Each computer player** has its own counters, starting as the
  player's do, its crew growing by its starbases, its freighters
  unloading into them. Once a second it looks at its next entry: a ship
  it orders from a builder of its with nothing on order, if it can pay;
  a station it gives to an idle construction ship, which flies there and
  pays as it starts, as the player's.
- **Where a station goes:** the nearest free site (by the footprints'
  rule, [building.md](building.md)) going round from the side of its
  home: a mining station from its base-th moon out, 150 first
  *(guessed)*; a station for a base (`m_base_num` past 0) at the middle
  of that mining station and its moon, as the original's cannon; others
  from its starbase out.

### Its ships' goals

By the scheduler's README, its own words, a computer player's map is cut
into cells 400 across, and each cell has a goal of each kind, valid or
not, with a raw priority, the sum of:

- the distance to our nearest station and to the enemy's, in cells,
  times `distance_from_home_priority_modifier` and
  `distance_from_enemy_priority_modifier`, and `perimeter_priority` on
  the map's edge;
- **attack** (cells with enemies but no enemy stations): the threat
  there times `attack_troops_priority`;
- **defend** (cells with our stations): the threat, and our stations
  times `defend_buildings_priority`;
- **attack base** (cells with enemy stations): the threat, and their
  stations times `attack_enemy_base_priority`;
- **explore** (cells not yet seen): `exploration_priority`;
- escort, a goal per freighter, wanting shields of 1000 times its build
  time, which the README calls in need of "a more logical measure".

Each ship's match with each goal is the goal's priority and its
distance to it in cells times `distance_from_goal_match_modifier`; the
best matches first, a ship goes to a goal until that goal has more
shields committed than the enemy has there (one ship, to explore); the
ships at goals still short are taken back and tried at others, and
those committed told to go to their goal's cell.

In Reluva each computer player plans so every ten seconds *(guessed)*, as its personality's `recompute_strategy_period`, 200,
gives no unit,
with its race's personality (`federation.aip`, `klingon.aip`,
`romulan.aip`, `borg.aip`) *(guessed)*. A cell's threat is its enemy
ships counted, and the shields to beat theirs *(guessed)*: the README doesn't
say how it's measured; a cell is seen once one of the
player's units has come within its `rangeScan` of its middle. Its armed
ships that aren't builders or freighters are sent; escorting isn't
made. And any armed unit, the player's too, with nothing to attack,
takes the nearest enemy in its weapons' reach *(guessed)*.

## Sources and checks

- **Where the facts come from:** the build lists, `aipdef.h`, the
  scheduler's README and `strategic_AI_config.txt`; the original's
  frames and draws over three and a half minutes of its computer
  player.
- **How it was checked:** the unit tests (`storm3d.unit.play`) read a
  build list as the game's and pick its next entries from the starting
  units on, and give the scheduler a small map: its goals where the
  README puts them, and two ships committed together to beat one
  enemy's shields that one alone couldn't; `engine-shot` of a skirmish without the shroud: the computer
  player's construction ship off to build, and more of its units by two
  minutes.

## Open questions

- How the original chooses a mining station's site: 357 from the moon
  isn't the nearest free one.
- Which personality (`.aip`) a skirmish's computer player takes, the
  period of its plans, how it measures threat, and how its ships fight
  once there: none of its warships were seen in the minutes watched.
- Where Random Placement puts the computer player (`Camera_4` in the
  game seen; Reluva puts slot n on `Camera_<n+1>`).
