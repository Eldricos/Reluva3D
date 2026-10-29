# The Angel Game Engine

Engine id `age`, in `src/engines/age/`. First game: Midtown Madness 2
(`mm2`). This page is the plan for the engine and records where it
stands, like [idtech3.md](idtech3.md).

## Where it stands

| Part | State |
|---|---|
| Detection | Midtown Madness 2: the English Windows release by `mm2core.ar`'s hash (install folder or the CD's `GAME` folder); other releases by file names, shown as "unknown release" |
| Archives | done: `.ar` ([ar.md](../formats/age/ar.md)); all 13,288 retail files read. Every `.ar` in the game folder is mounted, loose files over them (`filesystem.cpp`); `reluva-tool ar-list` and `ar-extract` |
| Textures | done: `.tex` in its four formats ([tex.md](../formats/age/tex.md)); all 3,665 retail textures decode; the flags' meanings still to work out; `reluva-tool tex-info` |
| Models | done: `.pkg` meshes, shaders with their paint jobs, placed models, and `.mtx` part placements ([pkg.md](../formats/age/pkg.md)); 1,034 of 1,040 retail models load (the 6 that don't are unused); `reluva-tool pkg-info` |
| Cities | done: `.psdl` streets and buildings, generated from every attribute, with tunnels, walls and railings, and the roads after the bounds read ([psdl.md](../formats/age/psdl.md)); `.cpvs` visibility ([pvs.md](../formats/age/pvs.md)); the water under the bridges, the road markings of `decals.pathset`, animated water textures, the texture flag that clamps ([tex.md](../formats/age/tex.md)); the city lit by its three lights, and its fog; `.inst` placed models ([inst.md](../formats/age/inst.md)); the sky dome and the 16 lightings ([tune.md](../formats/age/tune.md)); both retail cities read whole, every texture and model they name found; `reluva-tool psdl-info` |
| Driving | a first cruise mode, Reluva's model of the game's tuning ([age-driving.md](age-driving.md)): every car the menus offer, in either city; its simulation (suspension, tyres, engine, automatic gearbox, drag) from its `.vehCarSim`; collisions with the city's generated geometry, facade bounds and placed models' `.bnd` bounds ([bnd.md](../formats/age/bnd.md)); the near, far and in-car cameras from its camera files; its engine sound from its sound table; a HUD (speed, gear, revs). Not compared with the game yet |
| Races | a first version ([age-racing.md](age-racing.md), [race.md](../formats/age/race.md)): every Blitz, Circuit and Checkpoint race of both cities from the game's race files, at Amateur or Professional, against opponents in the cars and at the skills the race's aimap gives them, driving their recorded paths along a road graph Reluva builds from the city (88% of them finish; every checkpoint race's all do); checkpoints, laps, a Blitz's time limit, positions, results; the game's checkpoint stands and HUD arrow; the race's traffic, pedestrians and police; the game's music |
| City life | a first version ([age-traffic.md](age-traffic.md)): the road maps ([bai.md](../formats/age/bai.md)) and AI settings ([aimap.md](../formats/age/aimap.md)) of both cities read whole; traffic in the game's ambient models along the lanes, through the junctions, knocked aside by the player; pedestrians, the game's skinned models and animations ([ped.md](../formats/age/ped.md)), walking the sidewalks and diving out of the way; police that chase a speeding player along the roads and bust a stopped one. traffic lights at the road ends the road maps mark, which the traffic stops for; lane changes; the cars' lamps (brake lights, indicators, headlights with flares and pools of light at night, sirens). police patrolling the lanes when not chasing, with the game's siren tables and arrest sound, and the player's own siren in the police car. Ferries, boats and ducks on their paths, drawbridges that rise and fall, parked cars, and London's Underground trains ([pathset.md](../formats/age/pathset.md)); props, signs and traffic lights knocked down and broken into pieces; each race's own barricades; the ambient sounds of `aud/ambient`; the street furniture of `proprules.csv` along the sidewalks (the game has no police radio to play) |
| Multiplayer | a first version ([age-multiplayer.md](age-multiplayer.md)): the game's multiplayer menus; sessions over UDP between copies of Reluva, found on the local network or by address; Cruise, Blitz, Checkpoint, Circuit and Cops & Robbers, with chat, teams and the game's gold, words and announcer; computer players to fill a session, or to play alone. Its protocol is Reluva's own (the original's is DirectPlay) and its Cops & Robbers rules Reluva's reading of the files |
| Viewer | Race page (3D); Drive page (3D); World page (3D): London and San Francisco flown through, from the road nearest the middle, at each time and weather; Models page (3D, on the GPU renderer): every model with its parts placed, by level of detail and paint job, the lit parts (lamps, glows) on request; Textures page; Files page: every file with its size and archive, by folder; text files shown as text, the rest as hex dumps |

Decided: Midtown Madness 1 comes after Midtown Madness 2 runs.

## Why an engine family

Midtown Madness 2 runs on Angel Studios' Angel Game Engine (AGE), as
does Midtown Madness (1999), and later Midnight Club II on PC. They share
the archive format, models, textures, the city description (PSDL) and the
car simulation's tuning files. As with `idtech3`, what they share is
written once and what differs is keyed off a `GameInfo` entry, with
game-specific code in subfolders (`src/engines/age/mm2/`).

## What the retail copy holds

Surveyed from the English Windows release (Midtown2.exe 1.00.3200,
`reluva --hash` signatures below). Everything was read with a few lines
of throwaway Python; nothing below came from anyone else's code.

| File | Size | Contents |
|---|---|---|
| `mm2core.ar` | 21 MB | 5,680 files: `geometry/` (1,040 `.pkg` models, `.mtx`), `tune/` (cars, cities, cameras, HUD maps), `race/` (every race's checkpoints, opponents' recorded paths, AI maps), `bound/` (collision), `city/` (London and San Francisco: PSDL, instances, AI roads, visibility, lighting), `anim/` (pedestrians) |
| `mm2tex.ar` | 114 MB | 3,665 `.tex` textures, 307 `.tga`, 388 `.jpg` menu backgrounds |
| `mm2aud.ar` | 120 MB | 1,091 `.wav` sound effects and speech, 655 `.csv` sound tables, DirectMusic music (62 `.sgt` segments, 17 `.sty` styles, 14 `.dls` instrument banks) |
| `mm2audex.ar` | 144 MB | 1,412 more `.wav` (commentary, police radio) |
| `mmlang.dll` | | the game's text as Windows string resources (547 strings) |
| `logos.avi` | 4 MB | the opening logos: Indeo 5 video, PCM sound |
| `Midtown2.exe` + `MIDTOWN2.ICD` | | the program, under SafeDisc (see below) |

### The archive (`.ar`)

Worked out from `mm2core.ar` and confirmed by unpacking all four
archives with every file's size matching:

- Header: `DAVE`, then three little-endian u32: file count, directory
  size, names size. The directory starts at 2048.
- Directory: 16 bytes per file: offset of its name in the names block,
  offset of its data from the start of the archive, size, stored size.
- Names: zero-terminated, after the directory (2048 + directory size).
  Paths use `/`; folders appear as entries ending in `/` with size 0.
- Data: raw deflate (no zlib header) when stored size differs from size,
  stored as is otherwise. Files start on 2048-byte boundaries.

### Much of the game is text

This is the most useful finding: a lot of the game's behaviour is in
plain-text tuning files, so it can be read instead of reverse engineered.

- `tune/vehicle/*.vehcarsim`: each car's simulation: mass, inertia box,
  centre of gravity, aerodynamics, engine (horsepower, optimal and
  maximum RPM), gearbox (ratios, shift points), drivetrain, brakes,
  wheels and suspension. Also `.vehcardamage`, `.vehgyro`, `.vehstuck`,
  `.vehtrailer`, and `.aiVehicleData` for traffic.
- `tune/*.info`: the car list as the menus show it (name, colours,
  horsepower, top speed, unlocking). `tune/*.cinfo`: each city's races
  and their names.
- `bound/*.bnd`: collision shapes as text (vertices, materials, polygons);
  `.bbnd` are their binary forms.
- `race/<city>/*.csv`: every race's checkpoints and settings;
  `*.opp`: opponents' paths as CSV (x, y, z, brake, throttle...).
- `tune/*.mmHudMap`, camera files and more use the same `type: a` text
  form.

Binary formats still to work out (models, textures, cities, placed
models, the road maps and the pedestrians' animations are done, see the
table above): `.cpvs` (`PVS0`, visibility), `.lmap` (`LMP0`).

### The program is protected by SafeDisc

`Midtown2.exe` is SafeDisc's loader; the game's code is in
`MIDTOWN2.ICD`, whose `.text` and `.data` sections are encrypted
(entropy 8.0 bits a byte). Its imports (DirectInput, DirectSound,
DirectPlay, WinMM's mixer) can be read, its code can't. So:

- Running the original program in the x86 interpreter, as Elite Force's
  single-player DLLs are run, isn't an option: it would mean emulating
  SafeDisc as well as Direct3D, DirectInput, DirectSound, DirectMusic and
  DirectPlay.
- Reluva reimplements the game, as for New Worlds: from the data files,
  public format write-ups, and observation of the game running.
- The temporary asset folder also contains a cracked executable
  (`MM2-CD/crack`). It must not be used for this work (circumventing the
  protection isn't something this project relies on), and it hasn't been
  opened.

## Detection

`reluva --hash` on the retail install:

```
{"mm2core.ar", "8ba3c06f784f6f7abff1d4b902ef3440", 21430272},
{"mm2tex.ar", "27dd94a1e010c39bd7ad77ad82f4cb2b", 113573888},
{"mm2aud.ar", "887a50a3eaab9a2a0abc95ba52e19303", 119619584},
{"Midtown2.exe", "fbcb14ab668d8fbe4dac1dec8ae2d4ae", 348576},
```

Detection keys on `mm2core.ar` (hash and size) with `Midtown2.exe`
present; a fallback by file names alone finds other releases as "unknown
release", as `idtech3` does.

## Phases

Each phase ends with something visible in Reluva, tests for what can be
tested without game data, and a format page under `docs/formats/age/`
recording how each format was worked out (CONTRIBUTING.md).

### 1. Engine skeleton, detection and the archives (done)

- `src/engines/age/` with `MetaEngine`, `games.cpp` (detection table
  above), registered in `registry.cpp`; CMake option
  `RELUVA_ENGINE_AGE`.
- `ar_archive.cpp`: the DAVE archive as a `common/archive.h` `Archive`,
  so the four `.ar` files join a `SearchSet` with loose files over them
  (the original reads loose files first, which mods rely on). Deflate
  from `common/zip.cpp`.
- `reluva-tool ar-list` and `ar-extract`.
- Unit tests with a small archive built in the test, as
  `tests/zip_writer.h` does for zips.
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
  aren't shipped ([tune.md](../formats/age/tune.md)).
- Worked out from the files since (compared over every road and block
  of both cities): the road map's sidewalk kinds and road flags (roads
  with none, kept free of traffic and pedestrians), the PSDL roads'
  flags, ends and vertices, block flag 0x40 (off the ground's level), the
  road blocks' second byte (the street furniture rule), the facade
  bounds' direction, the dividers' texture, kind and height (raised and
  solid now) ([psdl.md](../formats/age/psdl.md), [bai.md](../formats/age/bai.md)).
  Models and cars are lit as the city is, by all three lights, with their
  shaders' ambient colours as Direct3D uses them ([tune.md](../formats/age/tune.md)).
- Still not known: some flags and words of the tunnel attribute, a few
  road flags (the `.bai`'s 0x8, the PSDL's 0x100-0x800), block flags 0x20
  and 0x80, texture flags 0x4, 0x8000 and 0x10000, and whether the game
  uses the low-detail roads as Reluva does. For watching the game run.

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
- The HUD: the game's speedometer, damage meter and clock pictures, and
  its map (`hudmap_<city>.pkg`, `.mmHudMap`).
- Sounds: the engine, horn, clutch, tyres rolling and skidding by
  surface, knocks and the suspension.
- Drive page: any car round either city.
- Still not known: Aero's angular damping, `.vehGyro`, the tyres'
  finer terms (what can be said from the 48 cars' values is in
  [age-driving.md](age-driving.md)), where the game lays out its HUD,
  which particle texture the smoke uses, the rest of `.ter` after its
  grid's header ([bnd.md](../formats/age/bnd.md); the bounds' material
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
  game's locked pictures say ([age-racing.md](age-racing.md)).
- The menus redrawn from `tune/widget.csv`, the `jpg/` backgrounds and
  the buttons' pictures, with `mmlang.dll`'s strings: the main menu,
  races, Crash Course and garage ([age-menus.md](age-menus.md)).
- Opening logos: `logos.avi` is Indeo 5, which has no published
  description to write a decoder from; skipped, as the original's "No
  Movie" shortcut does, with the game's splash picture shown instead.
- Music: the DirectMusic segments and styles, composed as they play by
  Reluva's DirectMusic player on its DLS synthesizer (`common/audio`;
  [directmusic.md](../formats/directmusic.md), [dls.md](../formats/dls.md)),
  following the race or drive ([age-racing.md](age-racing.md)).
- Not done: the other menus and modes (Cops & Robbers, replays, options).

### 6. Multiplayer (done)

The original uses DirectPlay. Playing against the original isn't
practical; Reluva-to-Reluva play over `System::openUdp` is
([age-multiplayer.md](age-multiplayer.md)):

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
