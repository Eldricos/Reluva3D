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
| 10–12 | float × 3 | The ambient light's colour ([Lighting](#lighting)): 0 in 37 of the 49 maps, 0.267 grey in `fed1.bzn`, a bluish 0.37, 0.39, 0.49 in `borg1.bzn` |
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

A directional light (`dlight`) has, after its transform, three floats
of colour ((0.357, 0.408, 0.49) and (0, 0.294, 0.592) in `fed1.bzn`'s
two) and a second position a little way along its front axis, the way
it shines *(inferred)*. They, and the settings' colour as the ambient
light, are how the game lights the map ([Lighting](#lighting)).

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

### Lighting

Storm3D lights its vertices itself and hands them to Direct3D coloured
([research/startup-trace.md](../research/startup-trace.md#how-it-draws)),
so a frame's draws (the host's `-draws`) show its lighting, and the code
that colours them was found by watching a vertex's colour being written
(the host's `-watch`) back to where it's worked out *(from the code,
0x59a290 and 0x599e10, each light's part 0x5837a0 and 0x5835b0)*. For
each vertex of a model, with the model's materials
([sod.md](sod.md#lighting-materials)):

> diffuse = the map's ambient (its settings' colour) + the material's
> ambient + Σ over the lights of max(0, n · towards it) × the light's
> colour × the material's diffuse
>
> specular = Σ over the lights of s / (p − p × s + s) × the light's
> colour × the material's specular, where s = max(0, n · h), h the way
> to the light and the camera's backward axis added and made one long,
> and p the material's power

each held between 0 and 1; the specular only for the faces whose
material's byte is 2, the diffuse for 1 or 2, a byte of 0 leaving the
faces their material's diffuse colour, unlit
([sod.md](sod.md#lighting-materials)). The lights are the map's directional lights,
their colour as the map has it; each is turned into the model's own
space by the transpose of the model's turn (0x553950), so a model's
scale, if it had one, would brighten it. n is the vertex's normal: the
faces' about it added up, each made one long first, so a big face counts
as much as a small one *(inferred)*: so made, the starbase's vertices in
the skirmish's frame 4,100 come out as the game's, the median within
0.001 of 1 (three in four within 0.02).

The specular is added over the texture: the game draws those faces with
a specular colour (vertex format 1c4) and Direct3D's specular turned on,
the others without (144). The camera's axis is the third row of the
camera's turn, taken into the model's space with the lights; it points
back towards the camera *(inferred)*: in the skirmish's frame 4,100, the
starbase's 513 vertices of byte-2 faces fit the formula so, the median
within 0.009 of the game's (0.003 for those of one material only), and
fit it worse with the camera's forward or any other axis, or with the
camera's pitch moved 4 degrees either way. The formula's peaks come out
higher than the game's, by 0.02 on average and by up to 0.28 at the
brightest vertices, which a highlight's steepness makes the normals'
smallest differences matter to; why isn't known. The power, 50 in the
starbase's materials, is the material's own: it's 50 in the game's
memory too.

The planet Barisa's 198 vertices in frame 6,100 of "Premonitions" fit
it to 0.002 on average: its material's ambient 0.2 and the map's 0.267
make its darkest vertices 119 of 255, and a light's part comes out 1.002
times the formula's. (A factor of 1.75 fitted them before, from the
vertices placed back in the world wrongly: a draw's vertex is at a depth
of 1.208 / rhw from the camera, not 1 / rhw; so placed, the planet's are
on a sphere, to 2 units.) Every one of the 49 maps has directional
lights.

The map's background model isn't lit: in the same frame its vertices are
its material's diffuse colour (255, 253, 255). Its star sphere is drawn
first, with depth tested but not written, blended one to zero, and its
nebula's cube faces after it added (one, one).

### The grid

In play the game draws a grid over the map, as `ART_CFG.h`'s
`DETAIL_<n>_main_show_grid = 1` asks at every detail level: in the
skirmish's frame 4,100, 15 lines of two points each, placed back in the
world, lie on the plane y = 0 (to 0.8), a line at each x and each z a
multiple of 100, in 0x262100 (38, 33, 0), added on (one, one), depth
tested but not written, drawn after the background and the planets and
before the ships. How far out the game draws them isn't known (its
lines are cut at the screen's edges; `GRID_FAR_CLIP_PLANE` is 10,000).
In the mission's opening scenes there's none, as `ART_CFG.h`'s
`DETAIL_<n>_cinematic_show_grid = 0` has it for the cinematic view.

### The shroud and the fog

What a player hasn't seen is covered *(seen in the skirmish's draws and
memory)*. The game keeps two 128 × 128 textures of the map's area, a
texel a 50-unit cell, row 0 at the far (highest) z: the shroud's,
`gshroud.tga`'s picture with the cells the player has seen in black, and
the fog's, `gfog.tga`'s with the cells it sees now in black. Each is
drawn as one square over the area on the plane y = 0, as the grid is,
its texture's u = x / 6400 and v = 1 - z / 6400, black left out: the
fog first, 70% opaque (its vertices' alpha 0xb2, blended by alpha),
then the grid, then the shroud, opaque, depth tested but not written,
so it covers the grid and what's below the plane (the planets) where
it lies. The map's ships and nebulae in it aren't drawn. Past the
area's edge there's neither.

At the skirmish's start the two are the same: a disc about each of the
player's units, of its `rangeScan` (the starbase's 700, from
`station.odf`; a ship's 500, from `craft.odf`). Counted in whole cells
from the unit's own cell, a cell is seen when it's within 716 to 721
units of it, a little more than the range: the 596 cells the starbase
and the two construction ships cleared on `4first.bzn`, cut by the
area's edge, fit that and nothing simpler. The game's own record of
what's seen wasn't found in its memory. The fog shows only once a
unit has moved from where it saw: until then it lies under the shroud.
In play the two follow the player's units ([play.md](../systems/play.md#sight-and-the-fog)).

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
= 8 9`). Not always: `4first.bzn`'s `Camera_1`, at x 577, z 445, is in
cell 11, 119 from the top, against `Start1 = 10 109`. A mission's map
has none: its objects are placed.

### Starting units

A skirmish's players start with their race's units, not the map's: a
race's definition (`fed.odf`, `kling.odf`, `rom.odf`, `borg.odf`) names
three sets, `minimalUnits1`..., `standardUnits1`... and `superUnits1`...,
nine at most each; the Federation's standard set is two construction
ships (`fconst`) and a starbase (`fbase`), and so is each other race's
with its own. The game makes them *(from the code, 0x440e47 to
0x441085)*, for each player's slot:

- about the first point of the path `Camera_<n>`, n the slot (the middle
  of the map's area if there's none), at that point's height;
- each unit in its set's order, its name taken up to its dot;
- on a spiral: with r from 1, 1/12 more for each place tried, at
  (x + sin(2πr) × 100r, z + cos(2πr) × 100r), the first place the unit
  fits (a test of the unit's own, not traced); past r = 40, at the
  middle;
- facing +z, unturned.

The first unit is then 100 ahead of the point, the next a twelfth of a
turn on and a little further out. The game's memory in its skirmish on
`4first.bzn` agrees: the starbase, made third, at (677.64, 38.5,
502.83), 116.67 out at 60 degrees from `Camera_1`, and the other
player's second and third units the same about `Camera_4`, to 0.01. The
construction ships had drifted by then, a few units up and down.

Their colours: a mesh whose material is named `team_...` takes the
player's colour, from `teamcolor.odf`, which has a colour each for the
teams and the races; the starbase's bands, red in its model, are the
Federation's 0.9 grey in the game's frame, as no vertex of it is red
*(inferred)*.

## In Reluva

`BznMap` (`src/engines/storm3d/formats/bzn.h`) reads a `.bzn`: the
header, every field as it is, the settings, the objects (their opening
fields, and where their own fields are), the scripts and the paths. It
fails if a field's framing, the object count or the paths' count isn't
as the file says.

- **The data viewer's Maps page** (`MapPage`,
  `src/engines/storm3d/pages/map_page.h`) draws a map: each object's
  model where its transform puts it, with its sprites (the nebulae are
  sprites only) and the sprite node named for its definition (a planet's
  glow, [spr.md](spr.md#emitters-and-sprite-nodes)), and the map's background model around the eye, behind
  everything, added up and unlit, as above (how the game places and
  turns the background isn't known). An
  object's model is named for its definition, unless the definition says
  `dont_load_SOD = 1`, and scaled by its `scaleSOD` *(inferred)*: the
  traced mission loads `fente.SOD` for `fente`. One whose definition
  says `hidden = 1`, which the program reads (0x466f3d), isn't drawn:
  the directional lights, whose model is an arrow the original's cut
  scenes don't show *(inferred)*. N goes from ship to ship,
  P from path to path; T looks from straight above, as the map's picture
  does, and G as the game does on starting
  ([systems/camera.md](../systems/camera.md)).

  A map is lit by its own lights, as above (`MapPage::lightingOf`), its
  models' diffuse light and highlights as the game's, each group of
  faces lit or not as its material's byte says. Set
  beside the game's skirmish on `4first.bzn` and its mission
  "Premonitions", the planets, moon, starbase and backgrounds match: the
  planet's mean colour (93, 54, 35) against the game's (92, 53, 33).

  An asteroid field (`mafield`, `classLabel "asteroidfield"`) has no
  model: its definition gives 10 to 15 asteroids within a radius of 100.
  The mission traced, with 155 fields, loads `master.sod` to
  `master7.sod`, small clusters of asteroid sprites, but not `master8`,
  so the asteroids are those eight *(inferred)*. Which the game picks,
  and where, isn't known: the page scatters as many as the definition
  says within its radius, chosen from the object's number so a map looks
  the same each time *(guessed)*.

  The grid is drawn over the map's area, as above: one square across
  the area, its texture a line on two edges each 100 units, so it fades
  at a distance as lines wouldn't; H hides it.

  With the starting units comes the shroud, over what the first
  player's units don't see, as above, with 718 units' reach for 700's
  range (`MapPage::seenOf`); the map's objects above the plane in it
  aren't drawn. F turns it off. The fog isn't drawn: at the start it's
  hidden under the shroud.

  R adds each player's starting units, a race's standard set, as the
  game makes them (`MapPage::startUnitsOf`), but without its test that a
  unit fits: Federation, Klingon, Romulan, Borg, then none. Their team
  parts are in that race's colour, a map's own ships' in its race's.
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
  The starting units' placing was found by watching the starbase's
  position in the skirmish's memory being written (the host's `-watch`)
  and reading the code that wrote it, up its callers.
- **How it was checked:** every map reads to its last byte; the objects
  found come to the count in every map (17,586 of 139 definitions), and
  the paths to theirs (1,238). The unit tests (`storm3d.unit.bzn`) build
  maps in the layout: objects with a weapon's definition among their own
  fields, paths of one and of two points, a map with none, and broken
  ones; the starting units against where the game's memory had them,
  and the shroud's cells against its texture's. No published description of the format was used.

## Open questions

- How far out the grid goes.
- How far a unit sees exactly (between 716 and 721 for a range of 700),
  and where the game keeps what's been seen.
- How the game tests that a starting unit fits where the spiral puts it,
  and how a player's colour is chosen.
- Each kind of object's own fields, and what follows the objects.
- The header's two flags, the light's three floats, the second word of
  each path and its last float.
- Whether the area bounds the camera or the ships.
- How the game places an asteroid field's asteroids, and the background.
