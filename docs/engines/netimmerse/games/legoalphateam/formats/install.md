# LEGO Alpha Team: install layout

> **Status:** partial · **Code:** `src/engines/netimmerse/netimmerse.cpp` (detection) · **Checked on:** the English Windows retail CD (`LEGOATEN`) and a full install from it · **Confidence:** confirmed unless marked

## Overview

What the English Windows retail CD and a full install from it hold: the
program, its DLLs and eight `.pac` archives in the install folder, and
what stays on the CD.

## Layout

### Install folder (`C:\Program Files\LEGO Media\Games\LEGO Alpha Team`)

| File | Size | What it is |
|---|---|---|
| `LEGOATeam.exe` | 61,440 | the game; small, so the work is done in the DLLs below |
| `Launcher.exe` | 507,904 | same size as the CD's `AutoPlay.exe`; *(guessed)*: the same menu program (play, uninstall, website) |
| `BaseClassComp.dll` | 28,672 | the game's own code, as "components": its base classes, `DDNamedObject` (a named object) and `DDEvent` (events, with their own memory pages) *(from the code)* |
| `BehaviorComp.dll` | 40,960 | 〃 the behaviours' framework: hierarchical state machines (`DDHFSM`, `DDState`, `DDTransition`, `DDTrigger`, entry and exit ports, choice points) and `DDBehavior`, which runs one for an actor. The parts' DLLs in `Bhvr.pac` import it, with LoadComp, LegoLogicComp and BaseClassComp *(from the code)* |
| `LegoLogicComp.dll` | 163,840 | 〃 the game's rules |
| `LoadComp.dll` | 749,568 | 〃 loading; the largest of the four |
| `DDPackedDirFileLib.dll` | 69,632 | reads the `.pac` archives ("packed directory") |
| `NiMain.dll` | 667,648 | NetImmerse (Numerical Design Ltd.'s engine, later Gamebryo): scene graph, NIF files |
| `NiAnimation.dll` | 270,336 | NetImmerse: keyframe animation, skinning |
| `NiCollision.dll` | 188,416 | NetImmerse: collision |
| `NiDirect3d.dll` | 225,280 | NetImmerse's Direct3D renderer |
| `NiA3dSound.dll` | 69,632 | NetImmerse's sound, through Aureal A3D |
| `Ambient.pac` | 126,598,304 | 27 long stereo sounds, one per area: ambience or music |
| `Audio.pac` | 92,796,224 | 169 sound effects |
| `Bhvr.pac` | 4,532,456 | 74 DLLs, the puzzle parts' code ([pac.md](pac.md)) |
| `Etc.pac` | 11,794,154 | interface images, fonts, the 111 puzzles ([pac.md](pac.md)) |
| `Fin.pac` | 20,642,530 | 147 NetImmerse models (`.FIN`) |
| `LMap.pac` | 53,475,662 | 582 lightmaps (TGA) |
| `Map.pac` | 19,654,043 | 678 textures (TGA) |
| `Locale_en.pac` | 82,323,720 | 302 English voice clips and 6 text files; other languages presumably ship `Locale_<xx>.pac` |
| `Config.txt` | 0 | empty |
| `Game.txt` | 0 | the game's settings, which it writes when they're changed. The readme names its `Width`, `Height` and `BPP` lines |

### CD only

- `lmiintro.avi` (39,840,492): the LEGO Media intro movie, 640 × 480 Indeo 5
  at 15 frames a second with 22,050 Hz stereo PCM, 108.8 s. Not copied by
  the installer, and the game's program never names it. Reluva plays it
  before the game when a copy is beside the install
  ([../../../../../common/video/avi.md](../../../../../common/video/avi.md)).
- `data1.cab`/`data1.hdr`/`data2.cab`, `ikernel.ex_`, `layout.bin`, `setup.*`:
  InstallShield 6 installer. The install folder already holds what it
  unpacks, so Reluva doesn't need to read cabinets.
- `readme.txt`, `license.txt`, `Strings_en.txt`: text worth reading for the
  game's documented behaviour (keys, options, known issues), as New Worlds'
  readme was.
- The game checks for its CD: `LEGOATeam.exe` calls `GetVolumeInformationA`
  and holds the text "Please place game CD in drive" and `LEGOAT`. Reluva
  will answer that check itself, so no CD is needed.

### Not needed by Reluva

`AutoPlay.exe`/`AUTORUN.INF`, `DirectX/` (DirectX installer), `ereg/`
(electronic registration), `lego.ico`, `setup.bmp`.

## In Reluva

### Identification

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `LEGOATeam.exe` | 61,440 | `f4e48a14ef43f73ded1536266e16b94b` |
| `Bhvr.pac` | 4,532,456 | `6e2ed1e3cc81583995361fa0ff31bd77` |
| `Etc.pac` | 11,794,154 | `4a1e77f9c0b6d1d912571666838c91c5` |
| `Locale_en.pac` | 82,323,720 | `333f2476ec15acf658af87f09dcbca43` |
| `Map.pac` | 19,654,043 | `9d61f551f7649d7e3456a57dcb266339` |
| `LMap.pac` | 53,475,662 | `c78850aa53e5ff5230ff9818b6738df2` |
| `Fin.pac` | 20,642,530 | `3044e816442cbf6965c128991ade421f` |
| `Audio.pac` | 92,796,224 | `2881a2a26b94d9d2c8401f3450a19759` |
| `Ambient.pac` | 126,598,304 | `5b5a637eff72489fe191e6ba575dd3af` |

Detection (`src/engines/netimmerse/netimmerse.cpp`) uses `Locale_en.pac`'s
hash and requires `LEGOATeam.exe` to exist. An install with `LEGOATeam.exe`
and `Bhvr.pac` but no known locale archive is still found, as "unknown
release".

Detection should key on `Locale_<xx>.pac` (one variant per language) plus
`LEGOATeam.exe` existing, as New Worlds keys on its archive: patches tend
to replace the executable, not the large archives.

## Sources and checks

Worked out from directory listings of the CD and the install (October
2026). The archives' contents come from
reading them ([pac.md](pac.md)); meanings still marked *(guessed)* come from
the names alone and are to be confirmed. Those marked *(from the code)* come
from the DLLs' export and import tables.
