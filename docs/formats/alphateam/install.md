# LEGO Alpha Team: install layout

Source: directory listings of the English Windows retail CD (volume label
`LEGOATEN`) and of a full install from it (October 2026). Nothing below has
been read from inside the files yet; meanings marked *guess* come from the
names alone and are to be confirmed.

## Identification

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `LEGOATeam.exe` | 61,440 | `f4e48a14ef43f73ded1536266e16b94b` |
| `Bhvr.pac` | 4,532,456 | `6e2ed1e3cc81583995361fa0ff31bd77` |
| `Etc.pac` | 11,794,154 | `4a1e77f9c0b6d1d912571666838c91c5` |

`Locale_en.pac` is still to be hashed. Detection will need it to tell
the languages apart.

## Install folder (`C:\Program Files\LEGO Media\Games\LEGO Alpha Team`)

| File | Size | What it is |
|---|---|---|
| `LEGOATeam.exe` | 61,440 | the game; small, so the work is done in the DLLs below |
| `Launcher.exe` | 507,904 | same size as the CD's `AutoPlay.exe`; *guess*: the same menu program (play, uninstall, website) |
| `BaseClassComp.dll` | 28,672 | the game's own code, as "components" (*guess* from the names) |
| `BehaviorComp.dll` | 40,960 | 〃 behaviours, *guess*: runs what `Bhvr.pac` holds |
| `LegoLogicComp.dll` | 163,840 | 〃 the game's rules |
| `LoadComp.dll` | 749,568 | 〃 loading; the largest of the four |
| `DDPackedDirFileLib.dll` | 69,632 | reads the `.pac` archives ("packed directory") |
| `NiMain.dll` | 667,648 | NetImmerse (Numerical Design Ltd.'s engine, later Gamebryo): scene graph, NIF files |
| `NiAnimation.dll` | 270,336 | NetImmerse: keyframe animation, skinning |
| `NiCollision.dll` | 188,416 | NetImmerse: collision |
| `NiDirect3d.dll` | 225,280 | NetImmerse's Direct3D renderer |
| `NiA3dSound.dll` | 69,632 | NetImmerse's sound, through Aureal A3D |
| `Ambient.pac` | 126,598,304 | *guess*: ambient sound and music |
| `Audio.pac` | 92,796,224 | *guess*: sound effects |
| `Bhvr.pac` | 4,532,456 | 74 DLLs, the puzzle parts' code ([pac.md](pac.md)) |
| `Etc.pac` | 11,794,154 | interface images, fonts, the 111 puzzles ([pac.md](pac.md)) |
| `Fin.pac` | 20,642,530 | unknown (*guess*: the ending, or "final" assets) |
| `LMap.pac` | 53,475,662 | *guess*: level maps, or lightmaps |
| `Map.pac` | 19,654,043 | *guess*: maps |
| `Locale_en.pac` | 82,323,720 | English text and voice; other languages presumably ship `Locale_<xx>.pac` |
| `Config.txt` | 0 | empty |
| `Game.txt` | 0 | the game's settings, which it writes when they're changed. The readme names its `Width`, `Height` and `BPP` lines |

Detection should key on `Locale_<xx>.pac` (one variant per language) plus
`LEGOATeam.exe` existing, as New Worlds keys on its archive: patches tend
to replace the executable, not the large archives.

## CD only

- `lmiintro.avi` (39,840,492): the LEGO Media intro movie. Not copied by the
  installer; Reluva can take it from the CD (or a copy beside the install)
  and play it if present. Its video codec is to be checked (Cinepak and
  Indeo were usual in 2000).
- `data1.cab`/`data1.hdr`/`data2.cab`, `ikernel.ex_`, `layout.bin`, `setup.*`:
  InstallShield 6 installer. The install folder already holds what it
  unpacks, so Reluva doesn't need to read cabinets.
- `readme.txt`, `license.txt`, `Strings_en.txt`: text worth reading for the
  game's documented behaviour (keys, options, known issues), as New Worlds'
  readme was.
- The game checks for its CD: `LEGOATeam.exe` calls `GetVolumeInformationA`
  and holds the text "Please place game CD in drive" and `LEGOAT`. Reluva
  will answer that check itself, so no CD is needed.

## Not needed by Reluva

`AutoPlay.exe`/`AUTORUN.INF`, `DirectX/` (DirectX installer), `ereg/`
(electronic registration), `lego.ico`, `setup.bmp`.
