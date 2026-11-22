# SimCoaster: the CD and what setup installs from it

> **Status:** decoded · **Code:** `src/engines/tpw/games.cpp` · **Checked on:** the US CD, imported and compared with the folder its setup installed · **Confidence:** confirmed unless marked

## Overview

The CD holds the game in an InstallShield 6 cabinet
([installshield.md](../../../../../common/installshield.md)), with
`Game.exe` and the language's own folders beside it. Setup copies the
cabinet's groups to the install, then those folders over them. Reluva's
import does the same, leaving out what it doesn't need.

## Layout

### The CD's top folder

| File or folder | What it is |
|---|---|
| `data1.hdr`, `data1.cab`, `data2.cab` | The cabinet: 1,254 entries in 22 groups; `data2.cab` (321 MB) has the game's files |
| `Game.exe` | The game ([executable.md](executable.md)): not in the cabinet; setup copies it |
| `Speech/American/` | The speech: `speechHD.SDT` (37 MB), its `.map`s and `lips.wad` |
| `CoasterData/American/` | The 16 coasters that come with the game (`.cos`) |
| `PostCardData/American/` | The postcards' pictures (`postcard/`) and the album (`My Postcards/`, HTML pages and pictures) |
| `Setup.exe`, `setup.inx`, `ikernel.ex_`, `layout.bin`, `Setup.ini`, `Installer.exe` | InstallShield's setup; `Setup.ini` names the game `SimCoaster` |
| `AUTORUN.INF` | Names it "Theme Park Inc", and runs `Installer.exe` |
| `secdrv.sys`, `drvmgt.dll`, `00000001.TMP`, `00000002.TMP`, `00000xxx.016`, `00000xxx.256` | SafeDisc's |
| `DirectX/`, `AOL/`, `eReg/` | DirectX's setup, AOL's, and EA's registration program |

### The cabinet's groups

| Group | Files | Installed in |
|---|---|---|
| `Data` | 27 to 1230: the game's data | `Data/` |
| `Training AVI` | 1253: `TPinc30.avi`, the opening movie | the install's top |
| `Program DLLs` | 1234 to 1251: QSound's mixer (`QMixer.dll`), the online services (`wea*.dll`), and `SndReverb.map` | the install's top |
| `DLL 95 98`, `DLL Win2000` | `usp10.dll`, and `usp10.dll.Win2000`, installed as `usp10.dll` on Windows 2000 and later | the install's top |
| `Program Executable Files`, `English` | an entry for no file each | |
| The rest (`<Support>`, `<Engine>`, `<Disk1>`) | InstallShield's own: setup's program, its pictures, its scripts | not installed |

### The language's folders

| On the CD | Installed in |
|---|---|
| `Speech/<language>/` | `Data/global/Speech/` |
| `CoasterData/<language>/` | `Data/my coasters/` |
| `PostCardData/<language>/postcard/` | `Data/postcard/` |
| `PostCardData/<language>/My Postcards/` | `My Postcards/` |

The US CD's language is `American`, as are its string tables'
(`Data/Language/American/`).

## Behaviour

Eleven of the language's files have a namesake in the `Data` group that
isn't the same file: the speech (`speechHD.SDT`, 39,885,137 bytes in
the cabinet and 37,785,774 on the CD; `lips.wad`; `cat_speechSFX.map`)
and eight of the postcards' pictures. The CD's are the ones installed,
so setup copies the folders after the cabinet. The cabinet's are
another language's, perhaps the British English of Theme Park Inc
*(guessed)*.

## In Reluva

`planImport` (`src/engines/tpw/games.cpp`) recognises the CD by
`data1.hdr` with a `Data` group and `Game.exe` beside it, and plans:

- the `Data` group's files in `Data/`;
- `TPinc30.avi` and `SndReverb.map`, at the top;
- `Game.exe`, which detection asks for;
- the language's folders, laid out as setup lays them, each file in
  place of the cabinet's namesake. The language's folder is the one the
  `Data` group's string tables are in.

It leaves out the DLLs, `ImportCoaster.exe` and SafeDisc's files, which
aren't used as the game isn't run, and the registration program
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#by-choice)). The release
is told from the cabinet by the detection table's files and their sizes,
and checked by detection once imported. `reluva-tool import <the CD>
<destination>` lists the plan and does it.

## Sources and checks

- **Where the facts come from:** the US CD's files, the cabinet listed
  with the shared reader, and each installed file matched by its MD5 to
  the cabinet's entry or the CD's file it came from (November 2026).
- **How it was checked:** the CD imported with `reluva-tool import`: its
  1,276 files are byte for byte those of the folder setup installed,
  which has besides only what's left out above: the DLLs,
  `ImportCoaster.exe`, SafeDisc's files, the icons, the licence and
  `eReg/`.
