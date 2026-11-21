# LEGO Island: the game's files

> **Status:** partial · **Code:** `src/engines/legoisland/legoisland.cpp` · **Checked against:** directory listings of the English 1.0 CD and an install made from it, and the installer's log · **Confidence:** confirmed unless marked

## Overview

Where the game's files are, what each is for, and how a release is told
apart: the program and its world database on the hard disk, and the SI
archives it streams from the CD.

## How it works

### The install and the CD

The installer copies the CD's `DATA\DISK\` folder as it is, with no
cabinets or compression: the installed files are byte for byte the CD's.
Everything else the game reads stays on the CD.

| File | Where | What |
|---|---|---|
| `ISLE.EXE` | install; CD `DATA\DISK\` | The game's program: a window, the registry's settings, and calls into `LEGO1.DLL` |
| `LEGO1.DLL` | install; CD `DATA\DISK\` | The engine and the game ("LegoOmni Library"); draws through DirectDraw and Direct3D Retained Mode, plays through DirectSound, reads DirectInput |
| `CONFIG.EXE` (also as `LEGOISLE.EXE`) | install; CD `DATA\DISK\` | The settings dialog the Start menu opens; writes the registry's settings, then starts `ISLE.EXE` *(inferred)* |
| `MFC42.DLL`, `MSVCRT.DLL` | install; CD `DATA\DISK\` | Microsoft's libraries for `CONFIG.EXE` |
| `lego\data\WORLD.WDB` | install; CD `DATA\DISK\LEGO\DATA\` | The world database: models, textures and parts (9.9 MB) |
| `lego\data\*INF.DTA` | install; CD `DATA\DISK\LEGO\DATA\` | 19 files, one per world (`ACT1INF.DTA`, `HOSPINF.DTA`...) |
| `lego\Scripts\NOCD.SI` | install; CD | The SI shown when the CD isn't found *(inferred)* |
| `LEGO\SCRIPTS\**\*.SI` | CD only | 27 SI archives (570 MB): the worlds' scripts and the media they play (WAV, FLC, Smacker 2, BMP) |
| `REDIST\DIRECTX\` | CD only | DirectX 5's installer, not the game's |

### Where the game looks

The installer writes two values under
`HKEY_LOCAL_MACHINE\SOFTWARE\Mindscape\LEGO Island`: `diskpath`, the
install's folder, and `cdpath`, the CD's drive.

### Telling releases apart

Two retail builds are known, 1.0 (August 1997) and 1.1 (September 1997)
*(published)*. The copy these pages were written from is 1.0:
`LEGO1.DLL`'s version resource says 1.0.0.0, it was linked on 12 August
1997, and its MD5 isn't the one published for 1.1. Its files:

| File | Size | MD5 of the whole file |
|---|---|---|
| `ISLE.EXE` | 86,528 | `d3dc01ad31d5392be7660c1a08de2400` |
| `LEGO1.DLL` | 1,134,080 | `0b4cb9e080dbab1f1fac7c5b8593080d` |
| `CONFIG.EXE` | 95,744 | `764e23e8a713e1b66f146da8d9c49576` |

## In Reluva

The English 1.0 release is recognised by `LEGO1.DLL` and
`lego/data/WORLD.WDB` (their sizes, and the MD5 of their first 5,000
bytes), in `kVariants` in `src/engines/legoisland/legoisland.cpp`; any
other release by those two files' names, as an unknown release. The SI
archives aren't looked for yet, nor is a CD's folder imported.

## Sources and checks

- **Where the facts come from:** listings of the CD and of an install made
  by the game's installer, compared file by file; the installer's log
  (`INSTALL.LOG`: the files it copied and the registry values it wrote);
  the executables' version resources and headers. The two retail builds,
  their dates and 1.1's hash are from the
  [legoisland.org wiki](https://legoisland.org/wiki/LEGO_Island_Versions)
  and the decompilation project's
  [README](https://raw.githubusercontent.com/isledecomp/isle/master/README.md)
  (its published hashes only).
- **How it was checked:** `legoisland.unit.detection` checks the table's
  fallback; the release's own entry was checked on the copy above.

## Open questions

- Which values `CONFIG.EXE` writes, and which `ISLE.EXE` reads.
- 1.1's and other languages' files, to add them to `kVariants`.
