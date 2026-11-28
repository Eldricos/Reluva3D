# Need for Speed: `.viv` archives

> **Status:** decoded · **Code:** `src/engines/nfs/formats/viv.cpp` · **Checked on:** every archive · **Confidence:** confirmed unless marked

## Overview

EA's "BIGF" archives: a directory, then the files it lists. NFS III
keeps its cars, speech and some front-end models in them; NFS II SE its
cars' data, its race screen's pictures and its speech
([NFS II SE](#what-they-hold-in-nfs-ii-se)), with looser alignment.

## Layout

The header and directory are **big-endian**.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `BIGF` |
| 4 | u32 | the archive's size (equal to the file's in all 115 of NFS III's and all 6 of NFS II SE's) |
| 8 | u32 | number of files |
| 12 | u32 | offset of the first file's data (just after the directory, rounded up to 4) |
| 16 | ... | directory |

Each directory entry is:

- u32 offset;
- u32 size;
- the name, ended by a 0.

The entries are in the same order as the files' data. Files start on
4-byte boundaries, with 0 to 3 bytes of padding between them.

### What they hold in NFS III

- **Cars** (`GameData/CarModel/<car>/car.viv`):
  - `carp.txt` (performance);
  - `car.fce` (model);
  - `car00.tga` (skin);
  - `fedata.*` (front-end text in 7 languages);
  - `fedata.fsh`;
  - the engine sounds (`car.bnk`, `ocar.bnk`, `ocard.bnk`, `scar.bnk`);
  - `dash.qfs` (cockpit).

  Traffic cars hold only the first three.
- **Speech** (`ipspch?.viv`): one `.bnk` per voice.
- **Front end:** `trkgo.viv` holds `go*.fce` and `go*.tga`, track models
  for the menus.

### What they hold in NFS II SE

Four archives, two of them also in the install, the same files
(`reluva-tool nfs viv-list` and a survey script, November 2026):

- **Cars' data** (`GameData/Sim/CarData/cardata.viv`, 127 files): the
  53 `.geo` models ([geo.md](../games/nfs2se/formats/geo.md)), 73 `.dat`
  files and `CarHsvt.sjh`.
- **The race screen** (`GameData/DashHud/pc/huddata.viv`, 38 files):
  10 `.rlf` and 16 `.msk` files (each `.msk` starts `BIGF` itself), copies of the software program's skies (`.hrz`,
  [hrz.md](../games/nfs2se/formats/hrz.md)), `sky.fsh`, and three
  entries of size 0 (`hud.fsh`, `hud*.fsh`, `mapdots.fsh`).
- **Speech**, on the CD only: `FEDATA/PC/AUDIO/ENGLISH/ENGLISH.VIV`
  (169 files) and `SPANISH/SPANISH.VIV` (152), each a sound bank
  ([bnk.md](bnk.md#nfs-ii-se)), named by number (`000`, `c00`,
  `h00`...).

The speech archives aren't aligned: their files follow each other with
0 to 4 bytes between them, 130 of English's 169 and 118 of Spanish's
152 off a 4-byte boundary, and English's first file starts a byte after
where the header's offset says. The other two are aligned as NFS III's.

## In Reluva

Read as `VivArchive` (`src/engines/nfs/formats/viv.cpp`), and checked on all
115 retail archives in the NFS III install and on its CD, and all 6 of
NFS II SE's (its install's 2 and its CD's 4). The reader takes each
file where the directory puts it, so the speech archives' alignment
needs nothing of its own.
`reluva-tool nfs viv-list` lists an archive's files and `viv-extract`
writes them to a folder.

## Sources and checks

- **Where the facts come from:** the games' own files: NFS III's, and NFS II SE's install and CD; the game's code wasn't needed for this format.
- **How it was checked:** the reader run on every archive of both games to its end; the unit tests (`nfs.unit.formats`) and the game-files tests (`nfs.gamedata.formats`, `nfs.nfs2se.gamedata.formats`).

The layout is as the NFS modding community describes EA's "BIGF"
archives. Each field was confirmed on the retail files with a survey
script, October 2026: magic, sizes, the first file's offset, alignment
and order; NFS II SE's the same way, November 2026.
