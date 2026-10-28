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
| Cities | done: `.psdl` streets and buildings, generated from every attribute but tunnels ([psdl.md](../formats/age/psdl.md)); `.inst` placed models ([inst.md](../formats/age/inst.md)); the sky dome and the 16 lightings ([tune.md](../formats/age/tune.md)); both retail cities read whole, every texture and model they name found; `reluva-tool psdl-info` |
| Driving | a first cruise mode, Reluva's model of the game's tuning ([age-driving.md](age-driving.md)): every car the menus offer, in either city; its simulation (suspension, tyres, engine, automatic gearbox, drag) from its `.vehCarSim`; collisions with the city's generated geometry, facade bounds and placed models' `.bnd` bounds ([bnd.md](../formats/age/bnd.md)); the near, far and in-car cameras from its camera files; its engine sound from its sound table; a HUD (speed, gear, revs). Not compared with the game yet |
| Races | a first version ([age-racing.md](age-racing.md), [race.md](../formats/age/race.md)): every Blitz, Circuit and Checkpoint race of both cities from the game's race files, at Amateur or Professional, against opponents driving their recorded paths along a road graph Reluva builds from the city (88% of them finish; every checkpoint race's all do); checkpoints, laps, a Blitz's time limit, positions, results; the game's checkpoint stands and HUD arrow. Not yet: cops, traffic, pedestrians, Crash Course, the game's menus and HUD pictures, unlocking |
| Viewer | Race page (3D); Drive page (3D); World page (3D): London and San Francisco flown through, from the road nearest the middle, at each time and weather; Models page (3D, on the GPU renderer): every model with its parts placed, by level of detail and paint job, the lit parts (lamps, glows) on request; Textures page; Files page: every file with its size and archive, by folder; text files shown as text, the rest as hex dumps |

Decided: Midtown Madness 1 comes after Midtown Madness 2 runs; the
DirectMusic player comes after the game runs.

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

Binary formats still to work out (models, textures, cities and placed
models are done, see the table above): `.bai` (`CAI1`, AI road network),
`.cpvs` (`PVS0`, visibility), `.lmap` (`LMP0`), `.anim`/`.skel`.

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
- Left for later phases: the texture flags (probably repeat or clamp,
  checked when the city is drawn), animating water, drawing a model's
  placed models (xrefs), what the `offset` section and the headlight
  squares are for, and how the game uses the damage textures (`_dmg`),
  which are among a car body's own shaders.

### 3. The city (done)

- PSDL: the street network as blocks of typed attributes (roads,
  sidewalks, crosswalks, facades, roofs), turned into meshes.
- `.inst` placed models, `.sky`, `.lt00`-`.lt15` lighting.
- World page: fly through London and San Francisco on the
  `gfx::Renderer3D` the id Tech 3 engine already uses.
- Left for later: tunnels (their attributes aren't worked out), the
  texture flags (repeat or clamp), `.cpvs`/`.pvs` visibility (the World
  page culls by the view and distance instead), `.water`, `.lmap` and
  the ambient maps, how the original lights the city (Reluva's
  approximation is too bright at night), low-detail road textures far
  away, animated water, and the PSDL's tail after the bounds.

### 4. Driving (first version done)

- Collision against the city (PSDL geometry, facade bounds, `.bnd`).
- The car simulation from `.vehCarSim`: suspension, tyres, engine,
  automatic gearbox, drag; Reluva's model of the numbers, to be checked
  against recordings of the original (acceleration, top speeds, the
  `.info` figures) and tuned.
- Cameras from `tune/camera`, a HUD (speed, gear, revs), engine sounds
  through `common/audio` (with a new pitch control).
- Drive page: any car round either city.
- Left for later: comparing the handling with the game; damage; the
  dashboard models and in-car dashboard camera; the HUD's map
  (`.mmHudMap`) and the game's own HUD pictures; the horn and clutch
  sounds, tyre and collision sounds; tunnels (London's Underground
  cutting is open); the `.bbnd`/`.ter` bounds; manual gears.

### 5. The game (races done; the rest next)

- Done: Blitz, Circuit and Checkpoint races from `race/`, opponents
  following their `.opp` paths (routed along the roads), results; a Race
  page with its own menu.
- Next: traffic and pedestrians on the AI road network (`.bai`,
  `.aimap`), police chases (the races' Cops column, `copchase.csv`),
  Crash Course (`crash*`), unlocking; the menus redrawn from
  `tune/widget.csv`'s layout, the `jpg/` backgrounds and `mmlang.dll`'s
  strings; the game's HUD pictures.
- Music: DirectMusic segments and styles on DLS instruments needs a
  DirectMusic player in `common/audio`. Decided: it comes after the game
  runs; until then, no music.
- Opening logos: an Indeo 5 decoder, or skipped (as the original's
  "No Movie" shortcut does).

### 6. Multiplayer (later, optional)

The original uses DirectPlay. Playing against the original isn't
practical; Reluva-to-Reluva play over `System::openUdp` is.
