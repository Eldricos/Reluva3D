# Star Trek: Armada: tech trees (`*.tt`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/tech_tree.cpp` · **Checked on:** the English retail 1.0 install's 27 tech trees, in `assets.zfs` · **Confidence:** layout confirmed; meaning inferred from the original's build menus

## Overview

A tech tree says what a player must have before building each unit. A
map names its own in its header ([bzn.md](bzn.md)): the 20 maps in the
install's `bzn` folder all name `tech1.tt`; the campaign's missions name
their own (`fed1.tt`, `kling3.tt`...), and others are for particular
games (`fulltech.tt`, where nothing needs anything; `nosuper.tt`,
`scionly.tt`).

## Layout

Text, a line a unit:

```text
fyard.odf    1 fbase.odf
ffreight.odf 2 fbase.odf fmining.odf
fyard2.odf   2 fyard.odf fresear.odf
```

- The unit's definition, then how many it needs, then those, spaces
  between. All 4,333 lines of the 27 files have this form; a count of 0
  needs nothing.
- Blank lines part the races and the special weapons, which need their
  research pod (`gamine.odf 1 fedpod4.odf`).

## Behaviour

A unit is built only once its player has every unit its line names
*(inferred)*. Against the original's menus with a skirmish's starting
units (a starbase and two construction ships, [building.md](../systems/building.md)):
the starbase's Dilithium Freighter was dark, and `tech1.tt` has it need a
mining station; the construction ship's first six items were lit and its
last four dark, and `tech1.tt` has the six need at most a starbase, the
last four a shipyard and a research facility (Advanced Shipyard,
Science Station), a shipyard (Torpedo Turret) or the advanced shipyard
and science station (Temporal Research). A research pod for a ship of
the Advanced Starship Yard was dark with no shipyard: `fedpod1.odf`
needs that yard.

## In Reluva

`TechTree` reads one; a skirmish reads its map's, and its build menus
light an item only when the player has all it needs
(`MissionView::buildMenuOf`).

## Sources and checks

- **Where the facts come from:** the 27 files themselves; the original's
  build menus in a skirmish's first minutes.
- **How it was checked:** the unit tests (`storm3d.unit.play`) give it
  `tech1.tt`'s Federation lines and check the construction ship's menu
  as the original had it.

## Open questions

- Whether a unit under construction counts as had.
- Whether the Game Setup's tech level chooses another tree.
