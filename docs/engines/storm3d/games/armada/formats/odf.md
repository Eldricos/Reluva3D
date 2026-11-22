# Star Trek: Armada: object definitions (`.odf`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/odf.cpp` · **Checked on:** the English retail 1.0 install's 488 object definitions · **Confidence:** layout confirmed; how the game looks values up inferred

## Overview

Each ship, station, weapon, projectile, effect and map object is
defined by an `.odf` file: its name and costs, its model, its weapons and
their sounds, its build lists. They're text, and all but one are in
`assets.zfs` ([zfs.md](zfs.md)), 488 of them. The one in the install's
folder, `events.odf`, isn't this format: it's the announcements' sounds
and messages, in blocks of their own ([Open questions](#open-questions)).

## Layout

Text in Windows-1252, with `\r\n` line ends. A line is one of:

| Form | Example | Seen |
|---|---|---|
| A comment, from `//` to the line's end | `//Rate at which shield recharges` | in most files |
| `#include "<file>"` | `#include "destroy.odf"` | 284 files, each at most one, always before the file's own keys |
| A section, in brackets | `[GameObjectClass]` | 51 times in 48 files: `GameObjectClass` (31), `Builder` (12) and eight others once each |
| `key = value` | `buildTime = 24.0` | 7,767 in the 488 files |
| A continuation: quoted strings, after a key with a list | `"USS Odyssey" "USS Ulysses"` | the lists of ship names |

### Values

- **Numbers,** sometimes with C's `f` (`0.75f`), and in 28 places with a
  `;` after them (`tooCloseForWarp = 10;`).
- **Quoted strings,** one or several (`hardpoints = "hp20" "hp21"`).
- **Empty,** when the list follows on the next lines
  (`possibleCraftNames =`).

### Includes

A file that includes another is that file with its own keys added and
changed: `fdestroy.odf` (the Defiant) includes `destroy.odf`, which
includes `craft.odf`. Includes nest at most 4 deep, and every included
file is in `assets.zfs`.

### Classes

`classLabel` names the kind of object, and so which of the game's classes
reads the rest *(inferred)*: `cannon` (16 files), `planet_obj` (15),
`shipyard` (13), and 77 others.

## Behaviour

How the game looks a key up is *(inferred)*: the last definition of it
counts, so that a file's own keys override the ones it includes. Two
files define keys twice: `zkahless.odf` its `soundThrust`, and
`cbltphot.odf` its `spriteDuration` and its radius, as `radius` and then
`Radius`; Reluva takes the later one. Keys are
matched ignoring case, and whether a section limits a lookup isn't known.
A number is read as C's `atof()` reads one, so `10;` is 10 *(inferred)*.

## In Reluva

`Odf` (`src/engines/storm3d/formats/odf.h`) reads a file and its
includes, through a loader that gives a file's text by name, and keeps
each entry with its file and section. `find()` gives the last entry of a
key; `string()`, `strings()` and `number()` read its value as above.
Lines of none of the forms are kept as problems, not refused.

- `reluva-tool storm3d odf <game folder> <name.odf>`: a definition with
  its includes, an entry a line.
- `reluva-tool storm3d odf-check <game folder>`: reads every definition,
  in the install's folder and `assets.zfs`, and reports what it can't.

## Sources and checks

- **Where the facts come from:** the 488 files, decoded from
  `assets.zfs`; the forms above were counted over all of them.
- **How it was checked:** `odf-check` reads all 488 with every include
  found and one line left unread: `shieldi.odf`'s fourth,
  `/energy cost per second`, a comment written with one slash. The unit
  tests (`storm3d.unit.odf`) cover includes, lists, sections, the value
  forms and failures. Published readers of ODFs were not needed.

## Open questions

- Whether sections matter to a lookup, and what a duplicate key does in
  the game.
- `events.odf`: named blocks (`FerengiArrives{ ... }`) of `Sound`,
  `Text`, `Table` and a few other lines, with `#` comments. The object
  definitions name them (`eventAttack = "SpecialAttack"`). To be read
  with the game's sounds.
