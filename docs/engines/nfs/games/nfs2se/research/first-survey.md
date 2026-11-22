# Need for Speed II SE: the first survey (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026, the first study of the retail files
and executables. Its facts go to their pages (formats/, systems/) as
each is worked out.

## The copy studied

A US "Need For Speed II SE" PC CD (English and Spanish set-up) and an
install made from it.

- **CD:** 535 MB without its DirectX folder. It holds both executables,
  the whole game's data, the tracks (`GAMEDATA/TRACKS/SE`, `.../PC`), the
  music (`GAMEDATA/AUDIO/PC`), the movies (`FEDATA/PC/MOVIES`) and the
  speech (`FEDATA/PC/AUDIO/ENGLISH|SPANISH/*.VIV`). Its set-up is
  InstallShield (`SETUP/ENGLISH`, `SETUP/SPANISH`); `SETUP/GLIDENR` and
  `SETUP/GLIDERSH` hold 3dfx's `glide2x.dll` for the Voodoo Graphics and
  the Voodoo Rush.
- **Install:** 89 MB, with `nfs2sen.exe` only, its DLLs and the data
  that isn't left on the CD. `install.win` gives the game its folders;
  those marked `D:` (tracks, music, movies, speech) are read from the
  CD. The CD's own `INSTALL.NFS` lists the same folders as `.`, for
  playing from the CD.

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `nfs2sen.exe` (install and CD) | `0b44f8449fa8889ecd5c4d17b90a3a40` | 900096 |
| `NFS2SEA.EXE` (CD) | `d00259994605b45c515bb3f31dfe30f2` | 975360 |
| CD `GAMEDATA/TRACKS/SE/TR00.TRK` | `0302003a6ed6f308384f0ed905b65422` | 675972 |
| `FeData/pc/text/TEXT.ENG` | `cd66e371ad322ad1eaf16e6892b8af7e` | 37862 |
| `GameData/Sim/CarData/CARDATA.VIV` | `f7cf339beaa69552581e6e1a91a82ab0` | 242816 |

## The executables

| File | Built (PE time stamp) | Renderer |
|---|---|---|
| `nfs2sen.exe` | 1997-09-23 00:18:02 | software, into DirectDraw surfaces |
| `NFS2SEA.EXE` | 1997-09-23 00:14:25 | 3dfx: loads `glide2x.dll` with `LoadLibraryA` and takes 33 Glide 2 functions by `GetProcAddress` (`_grDrawTriangle@12`, `_grLfbLock@24`...); the message "Could not load glide2x.dll" otherwise. It also names `SGL.DLL` (PowerVR's library), not yet looked at |

- **Toolchain:** Watcom C/C++32 (its run-time's copyright string;
  sections `AUTO`, `.idata`, `DGROUP`, `.bss`, `.reloc`; linker 2.18),
  image base `0x400000`, with relocations; as `nfs3.exe`.
- **Imports:** ADVAPI32 (the registry, 4), GDI32 (1), USER32 (39),
  KERNEL32 (100-101: threads, files, file mappings, the serial port),
  WSOCK32 (12: UDP), WINMM (6: the multimedia timer, joysticks), DDRAW
  (`DirectDrawCreate`, `DirectDrawEnumerateA`), DINPUT
  (`DirectInputCreateA`). TAPI32, `eacsnd.dll` and `dsetup.dll` are
  loaded by name.
- **Sound:** `EACSND.DLL` (1997-09-13, Watcom) imports only
  `DirectSoundCreate` and exports 6 functions with Watcom's register
  names: `iSNDdirectcaps_`, `iSNDdirectserve_`,
  `iSNDdirectsetfunctions_`, `iSNDdirectstart_`, `iSNDdirectstop_`,
  `iSNDdllversion_`. NFS III's has 16.
- **In Reluva's emulator** (`reluva-tool x86-run`, which has only the
  generic stand-ins): `nfs2sen.exe` loads, relocates and has its
  imports bound, and runs Watcom's start-up until `VirtualQuery`, which
  that tool doesn't stand in for. `reluva-tool nfs host` stands in
  for far more, for NFS III.

## What the data holds

| Files | What | NFS III's readers (`reluva-tool nfs survey`) |
|---|---|---|
| `*.viv` | `BIGF` archives: `cardata.viv` (cars' `.geo` models and `.dat` data), `huddata.viv` (`.rlf`, `.msk`, `.fsh`, `.hrz`), the speech | all 6 read |
| `*.qfs`, `*.fsh` | RefPack (`10 FB`) SHPI banks; cars' skins are 8-bit pictures (type `0x7B`) with one palette entry (`0000`, type `0x2A`) and an `SHLB` entry (type `0x7D`) | 363 of 595 banks; 5,266 of 13,575 pictures fail: no palette of their own (pause screens, cars), or types `0x24`, `0x2A`, `0x7A` unknown |
| `*.qda` | RefPack with a `30 FB` header: the cars' and the simulation's data | not tried |
| `*.ffn` | `FNTF` fonts | all 14 read |
| `text.*` | the front end's strings, 7 languages | none read: "bad first offset" |
| `*.bnk` | `BNKl` sound banks | all 284 banks, 838 sounds read |
| `*.mus` + `*.map` | `SCHl` music streams and `PFDx` maps | all 19 maps; 3 of 21 streams: a `.mus` holds several streams, with padding between them |
| `*.asf` | `SCHl` streams, one each | not run through the survey; their chunks follow each other to the file's end |
| `*.dct` | movies: `SCHl` sound with `mTCD` video chunks (560 in `CALAAV.DCT`) | not a format NFS III has |
| `demo.0`-`demo.2` | the attract mode's recorded races (guessed) | not tried |
| `trNN.trk` | tracks, magic `TRAC`; eight, `TR00`, `TR02`-`TR08` | not a format NFS III has |
| `trNN.col` | `COLL` version 11, as NFS III's | 2 of 8 read: "a structure runs past its block" |
| `trNN.hrz`, `3trNN.hrz` | sky settings, text | none read: 26 or 27 numbers |
| `trNN.lgt` | lights: commented text (`/* Depth cueing ...`) | not tried |
| `trNN.can`, `trNNa.can` | camera paths | not tried |
| `tracks/speeds/*` | AI speed and line tables (`.spd`, `.spl`, `.qps`...) | not tried |
| `*.geo` | car models, in `cardata.viv` (53) | not a format NFS III has |
| `gamedata/tmptrk/main.trk` | 1,126,400 bytes, not `TRAC`: the game's work file (guessed) | not tried |

Seven tracks' names are in `text.eng`: Proving Grounds, North Country,
Pacific Spirit, Mediterraneo, Outback, Mystic Peaks, Monolithic Studios.

## Sources to check against

Community write-ups of NFS II's `.trk` and `.geo` formats, as used by
the track and car editors of the time; MultimediaWiki's pages on EA's
formats (SHPI, RefPack, SCHl, the `mTCD` video); OpenNFS (MIT), which
reads NFS II, and FFmpeg's EA demuxer and decoders (LGPL), as references
only: no code is taken from any of them.
