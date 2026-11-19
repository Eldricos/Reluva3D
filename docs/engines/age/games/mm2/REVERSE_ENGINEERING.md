# Midtown Madness 2: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- **The game's program isn't read.** It's encrypted (below), and a copy
  with its protection removed isn't used.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files**, from the English Windows release: every format
  page was worked out from them.
- **Public gameplay videos**, watched for what the files don't say (the
  HUD's places, the damage meter, the music): measurements and
  observations only, in [research/footage.md](research/footage.md).
- **Published documentation**: Microsoft's PE/COFF specification (the
  string tables of `mmlang.dll`), the DirectX documentation (vertex
  layouts, lighting, DirectMusic and DLS), the Windows Help format's
  public descriptions, UPnP's and NAT-PMP's specifications, and public
  notes by the game's modding community where a page says so.
- **The game's own help file** (`MM2HELP.HLP`): the keys, the modes' rules
  as the player is told them.

### What the retail copy holds

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

Of the binary formats, `.lmap` (`LMP0`) holds nothing to go on ([tune.md](formats/tune.md)).

### The program

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
- A copy of the program with its protection removed must not be used
  for this work (circumventing the protection isn't something this
  project relies on); none has been opened.

### Detection

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

## Methods

- **Formats** were worked out from the files with a few lines of
  throwaway Python: guessing at a field's size, reading every file with
  that guess, and checking that it ends where it should, that counts and
  numbers run in step, and that coordinates land in the city. A reader
  then went into Reluva, and `reluva-tool age check` reads every file of
  its kinds (see [TESTING.md](TESTING.md)).
- **Values whose meaning isn't written down** were compared across all the
  files that have them (every car, every road, every block), and read by
  what they go with: each page says how.
- **Behaviour** is Reluva's reading of the files' numbers and names, then
  drawn and driven in Reluva and compared with the videos.

## The tools

`reluva-tool help age` lists the engine's commands
(`src/engines/age/tools/`).

| Command | What it does |
|---|---|
| `ar-list <file.ar> [filter]` | An archive's files, sizes and how they're stored |
| `ar-extract <file.ar> <dir> [filter]` | An archive's files unpacked |
| `tex-info <file.tex or game folder> [out.tga]` | A texture's format and flags, or a survey of every texture |
| `pkg-info <file.pkg or game folder> [out.obj]` | A model's meshes and parts, as an OBJ, or a survey |
| `psdl-info <game folder> <city> [out.obj]` | What a city's `.psdl` holds |
| `check <game folder>` | Every texture, model, city, road map, placement list, path set and visibility file read |
| `game-run <game folder>` | The game run headless, for the game-files test |
| `viewer-run <game folder>` | The data viewer run headless, for the game-files test |

## Working files not included

- **The throwaway scripts** used to work out the formats: each page says
  what was compared, which is enough to do it again.
- **Frames from the videos**: never kept here. Take them again from the
  videos named in [research/footage.md](research/footage.md).
- **The game's files**: from your own copy.
