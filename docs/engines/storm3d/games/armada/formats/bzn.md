# Star Trek: Armada: maps (`.bzn`, `.mdf`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/bzn.cpp` · **Checked on:** the English retail 1.0 install's 49 maps · **Confidence:** confirmed unless marked

## Overview

`bzn\` holds the maps: 49 `.bzn`, 20 of the campaign's missions, 28
skirmish maps and `fulltech.bzn`, and for each skirmish map a `.mdf`
(its start positions) and a `.bmp` (its picture on the setup screen,
128 × 128).

A `.bzn` is the game's saved-game format, holding where a mission or a
skirmish starts. It's a few lines of text, then typed fields with no
names: the map's settings, its objects, what follows them, and the
paths its script moves things along. So a field's meaning depends on
where it is, and an object's own fields on its kind. What's here is
what every map shares; the fields that differ by kind of object aren't
worked out.

## Layout

Little-endian throughout.

### The text header

```text
version [1] =
2051
saveGameDesc = 5072656d6f6e6974696f6e730000...
binarySave [1] =
true
```

Lines end in CR-LF. The version is 2051 in all but `fulltech.bzn`
(2050, a map of one object). `saveGameDesc` is a title in hex, its
characters' codes two digits each, padded with `00`: "Premonitions" for
`fed1.bzn`. The fields start after `true`'s line.

### Fields

| Offset | Size | |
|---|---|---|
| 0 | 4 | The type, in the low byte. The other three bytes vary from field to field and from map to map: what was in the program's memory, as the game wrote the word whole *(inferred)* |
| 4 | 4 | The data's size |
| 8 | size | The data |

The fields follow each other to the file's last byte, in all 49 maps.

| Type | Data |
|---|---|
| 0 | 4 bytes (or 64, a mission's title again) |
| 1 | A byte: a flag |
| 2 | Text, padded with zeros to the field's size (16, 32, 40, 64, 100 or 128) |
| 4 | A 32-bit integer |
| 5 | A float |
| 7 | An object's definition ([odf.md](odf.md)), without `.odf`: 8 bytes |
| 8 | 4 bytes that look like an address in the program's memory, meaningless in the file *(inferred)* |
| 9 | Points: floats, 3 a point (x, y, z) |
| 11 | A transform: 12 floats, the right, up and front axes, then the position |

### The map's settings

The first 17 fields, the same types in every map:

| Field | Type | |
|---|---|---|
| 0 | text (16) | The map's name as it was saved: `fad1.bzn` in `fed1.bzn`, `untitled.bzn` in several |
| 1 | flag | 1 in every map |
| 2 | text (100) | The same name again (in `finale1.bzn`, without `.bzn`) |
| 3–8 | float × 6 | The map's area: x from and to, y, z. 0 to 6400 across and deep (`2duel.bzn`: 12800 × 3200), −28.5 to 105.5 high. Most objects lie within it; scenery can lie far outside (`borg1.bzn`'s reach x 10,641) *(inferred)* |
| 9 | flag | 0 in every map |
| 10–12 | float × 3 | 0 in most maps; 0.267 grey in `fed1.bzn`, a bluish 0.37, 0.39, 0.49 in `borg1.bzn` perhaps the light's colour *(guessed)* |
| 13 | text (100) | The background's model, one of 15: `mbgstars.sod`, `mbgborg.sod`... |
| 14 | text (128) | The tech tree, from `assets.zfs`: `fed1.tt` |
| 15 | text (128) | The player's race: `federation` |
| 16 | integer | The number of objects |

### Objects

Each object starts with the same seven fields:

| Field | Type | |
|---|---|---|
| 0 | 7 | Its definition: `fente` (the Enterprise), `fconst`, `mnebula5`... |
| 1 | integer | A number for it, its top bit set (`0x8000…` in most, up to `0x8004…`) |
| 2 | points (1) | Its position |
| 3 | integer | Its team: 0 for none (16,016 of the 17,586), then 1 to 8 |
| 4 | text (40) | Its name: "Enterprise", or the definition's and a number ("mnebula537") |
| 5 | 8 | |
| 6 | transform | Its orientation and position: the same position again |

Its own fields follow, as many as its kind has: 27 for a nebula, 30
for the Enterprise in `fed1.bzn`. They include its kind's name ("Sovereign
Class"), more definitions (its weapons', as type 7 too, so type 7
alone doesn't mark an object), and values that look like its state (a
second position, 1e30 in one float). These aren't worked out.

The objects are found by those seven fields, which no other run of
fields matches: in every map they come to the number the settings give.

### After the objects

What follows the last object isn't worked out: integers and floats in
groups (400, 3500, 1000000, 4000, 10000 repeating, perhaps by team
*(guessed)*), and in a mission's map a 40-character `EmptyMission`, then
its scripts in 64-character fields, `Federation1S.dsl` and
`Federation1R.drl` (those the missions' DLLs are named for,
[install.md](install.md#missions)), and the title again.

### Paths

The file ends with its paths, after an integer giving their number: 31
in `fed1.bzn`, up to 156 in `finale6.bzn`; 2 to 8 in a skirmish map; none
in `fulltech.bzn`. Each is seven fields:

| Field | Type | |
|---|---|---|
| 0 | 0 | 4 bytes, varying |
| 1 | integer | Its name's length |
| 2 | text | Its name, that long, without a zero: `ENTSTART`, `DOM1PATROL`, `CENTER` |
| 3 | integer | Its points' number |
| 4 | points | Its points |
| 5 | 0 | 0 (1,186 paths), 2 (33) or 3 (19). The patrols' are 2: perhaps a path that loops *(guessed)* |
| 6 | float | 0.01 in 1,221 of the 1,238; 0 to 0.15 in the others |

A mission's script moves its ships along them, by name. A patrol's last
point is its first.

### Start positions (`.mdf`)

A skirmish map's `.mdf` is text:

```text
StartLocations = 4
Start1 = 10 109
Start2 = 10 14
```

Each start is a cell of the map's 128 × 128 picture, which covers its
6400 × 6400 area, 50 units a cell, with z running up the picture: a
skirmish map's paths `Camera_1`, `Camera_2`... are where each player's
camera starts, and they fall within a cell or two of the starts (`2blue.bzn`'s
`Camera_2` at x 456, z 5,889: cell 9, 10 from the top, against `Start2
= 8 9`). A mission's map has none: its objects are placed.

## In Reluva

`BznMap` (`src/engines/storm3d/formats/bzn.h`) reads a `.bzn`: the
header, every field as it is, the settings, the objects (their opening
fields, and where their own fields are), the scripts and the paths. It
fails if a field's framing, the object count or the paths' count isn't
as the file says.

- **The data viewer's Maps page** (`MapPage`,
  `src/engines/storm3d/pages/map_page.h`) draws a map: each object's
  model where its transform puts it, with its sprites (the nebulae are
  sprites only), and the map's background model around the eye, behind
  everything (how the game places the background isn't known). An
  object's model is named for its definition, unless the definition says
  `dont_load_SOD = 1`, and scaled by its `scaleSOD` *(inferred)*: the
  traced mission loads `fente.SOD` for `fente`. N goes from ship to ship,
  P from path to path; T looks from straight above, as the map's picture
  does, and G as the game does on starting, approximately (see below).

  An asteroid field (`mafield`, `classLabel "asteroidfield"`) has no
  model: its definition gives 10 to 15 asteroids within a radius of 100.
  The mission traced, with 155 fields, loads `master.sod` to
  `master7.sod`, small clusters of asteroid sprites, but not `master8`,
  so the asteroids are those eight *(inferred)*. Which the game picks,
  and where, isn't known: the page scatters as many as the definition
  says within its radius, chosen from the object's number so a map looks
  the same each time *(guessed)*.
- `reluva-tool storm3d bzn <game folder> <name.bzn>`: the settings, each
  object's definition, team, position and name, the scripts, the paths.
- `reluva-tool storm3d bzn-check <game folder>`: every map read, with
  the objects, definitions and paths counted.

## Sources and checks

- **Where the facts come from:** the 49 maps, read byte by byte; the
  objects' and paths' meaning from their names and values (the
  Enterprise's position in `fed1.bzn` is where the mission starts it).
  Which files the game reads for a mission was traced from the original
  running in Reluva's emulator ([research/startup-trace.md](../research/startup-trace.md)).
- **How it was checked:** every map reads to its last byte; the objects
  found come to the count in every map (17,586 of 139 definitions), and
  the paths to theirs (1,238). The unit tests (`storm3d.unit.bzn`) build
  maps in the layout: objects with a weapon's definition among their own
  fields, paths of one and of two points, a map with none, and broken
  ones. No published description of the format was used.

## Open questions

- Each kind of object's own fields, and what follows the objects.
- The header's two flags, the light's three floats, the second word of
  each path and its last float.
- Whether the area bounds the camera or the ships.
- The game's camera. "Premonitions"' opening scene ends on its
  `ZOOMOUT` path's last point, (5,775, 450, 678), 450 being
  `RTS_CFG.h`'s `OVERVIEW_INIT_HEIGHT`, and its last frame faces +z
  (`OVERVIEW_INIT_ROTATION` 0): the planet Barisa, 1,600 ahead, to the
  left of the ships and above them, as the Maps page's G shows it. How
  far behind the camera sits and how wide it sees isn't known: G puts it
  √(4 × 200 × 450) = 600 behind, if it rides a parabola of
  `OVERVIEW_PARABOLA_FOCUS` *(guessed)*, and the original's frame shows
  Barisa larger and higher, so nearer and more level. Storm3D transforms
  its vertices itself, so the camera isn't in what it gives Direct3D
  ([research/startup-trace.md](../research/startup-trace.md#how-it-draws)).
- How the game places an asteroid field's asteroids, and the background.
