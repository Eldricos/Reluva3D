# Star Trek: Birth of the Federation: pop-up menus (`.mnu`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/menu_file.cpp` · **Checked on:** the three `.mnu` of the English retail `stbof.res` (October 2026) · **Confidence:** confirmed unless marked

## Overview

A menu widget in a window layout ([wdf.md](wdf.md)) names a `.mnu`, the
items of its pop-up menu. There are three, each used by the five races'
screens:

- `system.mnu` (in `?solars.wdf`, the system screen): one item, "home".
- `fleets.mnu` (in `?tactscn.wdf`, tactical combat, twice): the task force
  kinds, 1 Escorts, 2 Artillary (sic), 3 Capital, 4 Non-Combat, 5 Bases.
- `combat.mnu` (in `?tactscn.wdf`): the combat orders, for one ship (1-12)
  and for a group (101-112): Charge, Assault, Harry, Circle, Strafe, Flyby,
  Evade, Ram, Retreat, Open hailing frequencies (8 and 11 aren't used);
  then 200 Attack, 201 Open hailing frequencies, 900 Select Ship, 901
  Select Group.

The menus are in the language's `stbof.res` (the German ones differ).

## Layout

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | 1 |
| 4 | `u32` | how many items |
| 8 | | the items, 88 bytes each (below) |

An item:

| Offset | Type | Field |
|---|---|---|
| 0 | `i32` | its number: what the game acts on, presumably *(inferred)* |
| 4 | `char[13]` | a picture ("none" in all of them) |
| 17 | `char[31]` | the label |
| 48 | 40 bytes | the same in every item: 359, 0, three values that look like addresses, 0, 0, 359, 0, 1 |

## Behaviour

### The menu widget

The menu widgets in the `.wdf` files have four values after the common
part, then two fonts. The four values are 0, 0 and the widget's own x and
y again, not a text rectangle as UE has it. The first font is the default
(0) and the second has its own colour, per race in the system screen and
red (`ff 00 00`) in combat: the highlight, presumably *(inferred)*.

## In Reluva

`MenuFile` (`src/engines/botf/formats/menu_file.cpp`) reads the items.
reluva-viewer's Birth of the Federation Layouts page shows each menu
widget open, the items in the menu's font. The unit tests
(`botf.unit.layouts`, "menus") cover it.

## Sources and checks

- **Where the facts come from:** worked out from the three files (the
  Ultimate Editor doesn't read them), October 2026.
- **How it was checked:** all three read whole
  (`tools/botf-re/botf_res_sample.py --ext wdf mnu --all-whole`), and their
  items shown in the viewer.

## Open questions

- How the game frames the menu with the menu widget's four pictures.
- What the 40 bytes at the end of each item are.
