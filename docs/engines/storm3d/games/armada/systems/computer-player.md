# Star Trek: Armada: the computer player

> **Status:** partial · **Code:** `src/engines/storm3d/play/computer_player.cpp`, `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, without the shroud, its frames every two seconds and draws every ten, watching its computer player's base · **Confidence:** published (the build lists, the README), where it builds inferred from one game

## Overview

A skirmish's computer player builds by its race's build list,
`AI\AIPs\<race>_build_list.aip` (`federation_build_list.aip` and the
Klingons', Romulans' and Borg's), in order; and, by the game's own
description of it (`AI\AIPs\Scheduler_README.txt`, shipped with the
game), sends its ships to goals on a strategic map of 400-unit cells
(`AI\strategic_AI_config.txt`) weighed by its personality's `.aip`
priorities. This page covers its building; its goals aren't made yet.

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

## Sources and checks

- **Where the facts come from:** the build lists, `aipdef.h`, the
  scheduler's README and `strategic_AI_config.txt`; the original's
  frames and draws over three and a half minutes of its computer
  player.
- **How it was checked:** the unit tests (`storm3d.unit.play`) read a
  build list as the game's and pick its next entries from the starting
  units on; `engine-shot` of a skirmish without the shroud: the computer
  player's construction ship off to build, and more of its units by two
  minutes.

## Open questions

- How the original chooses a mining station's site: 357 from the moon
  isn't the nearest free one.
- Which personality (`.aip`) a skirmish's computer player takes, and
  the scheduler's goals: exploring, attacking, defending, escorting.
- Where Random Placement puts the computer player (`Camera_4` in the
  game seen; Reluva puts slot n on `Camera_<n+1>`).
- The minimap without the shroud: the original shows a picture of the
  map; Reluva its background.
