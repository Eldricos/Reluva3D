# Midtown Madness 2: plan

## Goal

Midtown Madness 2 played from its own files, as the original plays it:
its menus, its races, its lessons and its cities, and multiplayer between
copies of Reluva. Midtown Madness 1 comes after Midtown Madness 2 runs.

## Phases

Each phase ended with something visible in Reluva, tests for what can be
tested without game data, and a format page under `formats/`
recording how each format was worked out (CONTRIBUTING.md).

### 1. Engine skeleton, detection and the archives (done)

- `src/engines/age/` with `MetaEngine`, `games.cpp` (detection table
  in [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#detection)), listed in
  `engines.cfg`.
- `ar_archive.cpp`: the DAVE archive as a `common/archive.h` `Archive`,
  so the four `.ar` files join a `SearchSet` with loose files over them
  (the original reads loose files first, which mods rely on). Deflate
  from `common/zip.cpp`.
- `reluva-tool age ar-list` and `ar-extract`.
- Unit tests with a small archive built in the test, as
  `tests/support/zip_writer.h` does for zips.
- Viewer: a Files page listing the archives, and text files shown.

### 2. Textures and models (data viewer) (done)

- `.tex` decoding for each format seen, checked against all 3,665.
- `.pkg` models: parts, levels of detail (`_H`, `_M`, `_L`, `_VL`),
  materials; `.mtx` placement matrices.
- Textures and Models pages, as New Worlds' and Elite Force's: cars
  with their colours, buildings and props.
- Done since: the texture flag that clamps, animated water, the models
  placed inside models (xrefs, with the landmarks' warning lights as
  flares at night), what the `offset` section is (where a model's middle
  was in the city). (The damage textures, `_dmg`, are used since phase 4.)

### 3. The city (done)

- PSDL: the street network as blocks of typed attributes (roads,
  sidewalks, crosswalks, facades, roofs), turned into meshes.
- `.inst` placed models, `.sky`, `.lt00`-`.lt15` lighting.
- World page: fly through London and San Francisco on the
  `gfx::Renderer3D` the id Tech 3 engine already uses.
- Done since: tunnels, walls and railings (the PSDL's tunnel attribute);
  the block flags, the header's count (the first road block) and the
  roads after the bounds; `.cpvs` visibility (the World page's **V**);
  the texture flag that clamps; `.water` (the water under the bridges);
  animated water; the city lit by the `.lt` files' three lights as
  Direct3D would, and the `_fog.csv` fog; low-detail road textures far
  off. `.lmap` holds nothing to go on, and the ambient maps' pictures
  aren't shipped ([tune.md](formats/tune.md)).
- Worked out from the files since (compared over every road and block
  of both cities): the road map's sidewalk kinds and road flags (roads
  with none, kept free of traffic and pedestrians), the PSDL roads'
  flags, ends and vertices, block flag 0x40 (off the ground's level), the
  road blocks' second byte (the street furniture rule), the facade
  bounds' direction, the dividers' texture, kind and height (raised and
  solid now) ([psdl.md](formats/psdl.md), [bai.md](formats/bai.md)).
  Models and cars are lit as the city is, by all three lights, with their
  shaders' ambient colours as Direct3D uses them ([tune.md](formats/tune.md)).
- Worked out since by comparing the files with each other: the `.bai`'s
  road flag 0x8 (a level road), the PSDL roads' 0x100 (cable-car tracks),
  0x200 (a train line) and 0x400/0x800 with the second word's low bits
  (the junction controls at the two ends), the tunnels' flag 0x4 (a 2 m
  fascia under raised decks, it seems), texture flag 0x4 (the ground's
  surfaces) and the whole of `.ter` (a polygon grid and edge data for
  colliding) ([bai.md](formats/bai.md),
  [psdl.md](formats/psdl.md), [tex.md](formats/tex.md),
  [bnd.md](formats/bnd.md)).
- Since then: the `.cpvs` variants are cuts of `.pvshist`'s scores (the
  game's at 8); block flag 0x80 is, on roads, having walls or railings,
  and otherwise ground with a model's collision or an entrance, not
  exactly; texture flag 0x8000 is on what's seen closest, which Reluva
  reads as "keep full detail" for its Texture Quality.
- Still not known: a junction tunnel's word 3 and exactly what its extra
  masks mark, the tunnels' flag 0x4000, the exact rules for block flags
  0x20 and 0x80, texture flag 0x10000, and whether the game uses the
  low-detail roads as Reluva does. For watching the game run (no
  footage taken under the freeway or along the railings shows them).

### 4. Driving (done)

- Collision against the city (PSDL geometry, facade bounds, `.bnd` and
  `.bbnd`), with what each surface is made of (`city/materials.mtl` and
  `.csv`): its grip, its sound, water's drag, deep water sinking a car.
- The car simulation from `.vehCarSim`: suspension, tyres, engine,
  automatic and manual gearbox, aero drag and down force; Reluva's model
  of the numbers. Every car tops out at its Trans High; the `.info`
  figures are the menus' bars. A car turned over is set upright
  (`.vehStuck`).
- Cameras from `tune/camera`, with the dashboard (`<car>_dash.pkg`,
  `.asnode`) in the car; damage from `.vehCarDamage` (smoke, damaged
  textures, wrecked).
- The HUD: the game's speed, gear, revs, damage meter and clock
  pictures, laid out where the game's own screens show them, and its map
  (`hudmap_<city>.pkg`, `.mmHudMap`); see
  [footage.md](research/footage.md).
- Sounds: the engine, horn, clutch, tyres rolling and skidding by
  surface, knocks and the suspension.
- Drive page: any car round either city.
- Still not known: Aero's angular damping (`.vehGyro` and `.vehStuck`
  are read now, as Reluva's readings), the tyres'
  finer terms (what can be said from the 48 cars' values is in
  [car-simulation.md](systems/car-simulation.md)), which particle texture the smoke uses, the rest of `.ter` after its
  grid's header ([bnd.md](formats/bnd.md); the bounds' material
  effects and sounds are all none). For watching the game run.

### 5. The game

- Blitz, Circuit and Checkpoint races from `race/`, opponents following
  their `.opp` paths (routed along the roads) in their aimaps' cars,
  results.
- City life: traffic and pedestrians on the road maps (`.bai`, `.aimap`,
  `anim/`), traffic lights, lane changes, the cars' lamps, police
  patrols, chases and sirens (the game has no police radio recordings);
  the ferries, boats and drawbridges, parked cars and Underground trains
  from the `.pathset` files; the props of `props.pathset`, which, with
  the traffic lights, can be knocked down (`.dgBangerData`) and come
  apart into their pieces; each race's own props (its barricades); the
  road markings (`decals.pathset`); the street furniture (`proprules.csv`,
  by the road blocks' rule numbers); the ambient sounds (`aud/ambient`).
- Crash Course: each city's 13 lessons (`crash<n>data.csv`), named and
  judged by `mmlang.dll`'s strings, cop chases included.
- The player's record (Reluva's own file), and unlocking: races in
  groups (the `.cinfo`), lessons in turn, cars and paint jobs as the
  game's locked pictures say ([races.md](systems/races.md)).
- The menus redrawn from `tune/widget.csv`, the `jpg/` backgrounds and
  the buttons' pictures, with `mmlang.dll`'s strings: the main menu,
  races, Crash Course and garage ([menus.md](systems/menus.md)).
- Opening logos: `logos.avi` (Indeo 5) is played before the menus by
  Reluva's own Indeo 5 decoder, shared with other engines
  ([indeo5.md](../../../../common/video/indeo5.md)); where it can't be played,
  the game's splash picture stands in, as the original's "No Movie"
  shortcut does ([menus.md](systems/menus.md)).
- Music: the DirectMusic segments and styles, composed as they play by
  Reluva's DirectMusic player on its DLS synthesizer (`common/audio`;
  [directmusic.md](../../../../common/audio/directmusic.md), [dls.md](../../../../common/audio/dls.md)),
  following the race or drive ([races.md](systems/races.md)).
- Done since: the other menus and modes (Cops & Robbers, replays, options).

### 6. Multiplayer (done)

The original uses DirectPlay. Playing against the original isn't
practical; Reluva-to-Reluva play over `System::openUdp` is
([multiplayer.md](systems/multiplayer.md)):

- The game's multiplayer menus: sessions found on the local network or
  joined by address, the host's options and settings, the lobby with its
  chat, teams, cars and readiness.
- A protocol of Reluva's own: the host keeps the session and the clock
  and passes each car's state to the others, who draw it smoothed.
- Cruise, Blitz, Checkpoint and Circuit races, and Cops & Robbers
  (free-for-all, cops against robbers, robber teams) played between the
  city's `multicopwaypoints.csv` places with the game's gold, its words
  and its announcer.
- Computer players the host adds, driven on its machine: they race,
  cruise and play Cops & Robbers (take the gold home, chase and ram
  whoever has it), so a session can be filled out or played alone,
  without a network.

## Decisions

- **Reimplemented, not emulated.** The game's code is encrypted by
  SafeDisc, so running it in the x86 interpreter isn't an option
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-program)).
- **Multiplayer is Reluva to Reluva**, over a protocol of its own: the
  original's DirectPlay isn't spoken
  ([network-protocol.md](systems/network-protocol.md)).
- **Reluva's own files** for the options, the drivers' records and the
  replays, not the registry or the game's `.plr` files.
- **Midtown Madness 1 after Midtown Madness 2.**
