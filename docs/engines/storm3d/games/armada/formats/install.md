# Star Trek: Armada: install layout

> **Status:** partial · **Code:** `src/engines/storm3d/storm3d.cpp` (detection) · **Checked on:** the English retail CD and an install from it, 4,551 files · **Confidence:** confirmed unless marked

## Overview

What the English retail CD and a full install from it hold, and what of
it the game needs. Each kind of file gets its own page once it's read;
this page is the map. Sizes are in bytes.

## Layout

### The program and its archives

In the install's folder.

| File | Size | What it is |
|---|---|---|
| `Armada.exe` | 2,383,917 | The game: built 10 March 2000 (its PE header's time stamp), 1.8 MB of code. It imports DirectSound, Bink (`binkw32.dll`), `MSVCP60.dll`, `MSVCRT.dll`, `Strlkup.dll` and `WonInterface.dll`, and loads DirectDraw and Direct3D 7 itself. It exports 439 C++ functions and variables by name, the API the missions and the computer player call ([Missions](#missions)) |
| `assets.zfs` | 832,202 | An archive, `ZFSF` at its start, of 515 entries (`borg4.tt`...) |
| `objtxt.zfs` | 19,216 | An archive of the same kind, of 81 entries (`b1ob1.txt`...) |
| `startrek.zix` | 31,341 | Text, starting `001992` and a line naming the "Battlezone CD" |
| `string.ldl`, `string_e.ldl`, `string_fi.ldl`, `string_gi.ldl`, `default.ldl`, `language.ldl` | | Not text. All six start with the same 11 bytes, then differ: encoded rather than compressed, perhaps by a fixed key *(guessed)* |
| `Armada.pal` | 768 | A palette: 256 colours of 3 bytes *(inferred)* |
| `BZONE.FNT`, `BZONEJ.FNT` | 11,944 and 271,512 | Fonts |
| `RTS_CFG.h`, `ART_CFG.h`, `game_cfg.h`, `shell_cfg.h`, `Dynamic_Localized_Strings.h` | | The game's settings, as C declarations (`float OVERVIEW_MAX_ZOOM = 4.0;`) |
| `Input.map`, `Gamekey.map`, `label.map` | | The keys and the mouse, as text |
| `Collision.cfg`, `CCA.cfg`, `Logfile.cfg` | | Collision avoidance and logging settings, as text |
| `events.odf` | 52,762 | An object definition file, as text |
| `shell.set`, `mshell.set`, `mshelldemo.set` | | The front end's settings |
| `Credits.txt`, `Credits2.txt` | 10,962 each | The credits |

### The data folders

| Folder | Files | What they are |
|---|---|---|
| `SOD/` | 265 `.sod`, and 8 more in `SOD/Software/` | Models, `Storm3D_SW` at their start |
| `Textures/` | 1,368 `.tga`, in `Index8/` (1,078 files) and `RGB/` (993) | Textures; beside many a `.tga` are files of the same name ending `.1` to `.4`, a quarter of the size each step: smaller copies (mip levels) *(inferred)* |
| `Sprites/` | 24 `.spr` | Sprite tables, as text (`sprite_table`, `@reference=128`) |
| `bzn/` | 49 `.bzn`, 28 `.mdf`, 28 `.bmp` | Maps: `.bzn` as text (`version [1] = 2051`), `.mdf` their start positions (`StartLocations = 2`), `.bmp` their pictures |
| `missions/` | 26 `.dsl` and 26 `.drl` | The campaign's missions, as DLLs ([Missions](#missions)) |
| `AI/` | 7 DLLs, 85 `.aip` in `AI/AIPs/` | The computer player: its DLLs, its personalities as text, `strategic_AI_config.txt` and the developers' `Scheduler_README.txt` |
| `3D_Paths/` | 6 `.pth` | Paths as text: a count, then a point a line |
| `animations/` | 68 `.bik` | Bink movies |
| `sounds/` | 1,163 `.wav`, in `effects/`, `music/` and `voiceover/` | Sound effects, music, speech |
| `bitmaps/`, `bmp/` | 222 `.bmp` | The front end's pictures |
| `misc/` | 17 | The races' interface settings (`gui_fed.cfg`...), and a few more |
| `giddi/` | 11 `.std` | Joystick settings |
| `help/` | 140 | The manual (`Armada.pdf`) and the readme, in HTML |

### Missions

Each mission is two Windows DLLs, named after it: `<name>S.dsl` and
`<name>R.drl`, for 26 missions: `Federation1` to `5`, `Klingon1` to
`5`, `Romulan1` to `5`, `Borg1` to `5` and `Finale1` to `6`. They run on
the game's own code: each imports functions, classes and data from
`Armada.exe` by their names (C++ decorated names: `SetDilithium`,
`CineractiveShot`, `AI_Team_Load_AIP`, a `script` class).

- A `.dsl` imports 165 different names from `Armada.exe` over the 26
  missions, and exports the mission's own data by name (`Borgcon`,
  `Blocutuspoint`...).
- A `.drl` imports its mission's `.dsl` and 50 names from `Armada.exe`.
- `S` and `R` stand for script and rules *(guessed)*: the AI's DLLs are
  named the same way, `Borg_Medium_Script.dsl` and
  `Borg_Medium_Rules.drl`.
- The AI's seven DLLs import 25 names. With the missions', 213 of
  `Armada.exe`'s 439 exports are called.

### The CD

The CD's `setup/data/` holds the same files as the install. What only
the CD has:

- `setup/Data_f/Input.map`, `setup/Data_g/Input.map`: the keys, for French
  and German *(inferred)*.
- `setup/animations/STIntro.bik`: the opening movie.
- `setup/Help/`: the manual and the readme.
- `DirectX/`, `Preview/` (trailers and extras), `WONSwap/` and the
  installer (`setup.exe`, `setup/rsrc/`): not needed by Reluva.

### Not needed by Reluva

The online service's and the installer's files: `WonInterface.dll`,
`woncfg.cfg`, `gamewatch.dll`, `gwfile.dll`, `gwstartrek.dll`,
`Getinfo.dll`, `Sysinv.dll`, `Dxver.dll`, `uninstall/`, `URL/`, and the
network drivers in `dll/`.

## In Reluva

The engine finds the game by `Armada.exe`, `assets.zfs` and
`objtxt.zfs`. With all three hashes in its table, it's the English
retail release; with the three names alone, an unknown release. Nothing
else is read yet. `reluva-tool storm3d files <game folder>` lists the
files with their sizes.

## Sources and checks

- **Where the facts come from:** the files of the English retail CD and
  an install from it, listed with their sizes; the executables' PE
  headers, imports and exports, read with `objdump -p` and `reluva-tool pe-info`; the text files
  read as they are.
- **How it was checked:** every count on this page was taken over the
  whole install (November 2026). Detection was tried on the install: it
  is found as the English release.

## Open questions

- What `startrek.zix` and the `.ldl` files hold.
- Whether the patches (1.1, 1.2) replace the archives or only the
  program.
