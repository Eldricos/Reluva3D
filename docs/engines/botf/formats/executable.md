# Star Trek: Birth of the Federation: the executables (`trek.exe`, `Trek.icd`)

> **Status:** partial · **Code:** none (not read by Reluva) · **Checked on:** the English retail install's `trek.exe` and `Trek.icd` · **Confidence:** confirmed unless marked

## Overview

`trek.exe` is SafeDisc's loader, and `Trek.icd` the game itself, its code
and data encrypted. Reluva needs neither: it recognises the game by its
data files, and nothing of its code is read. What's here is what their
unencrypted parts (headers, imports, resources, relocations) say about the
game.

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `trek.exe` | 237,547 | `a6e91db92cf3e0d6eac8e3ef666fa9b7` |
| `Trek.icd` | 1,771,520 | `c59c1835fc11b571482b7d330c068f9d` |

## Layout

### `trek.exe`: only the SafeDisc loader

- Built 1999-03-22 with Microsoft's linker 5.10. It carries the SafeDisc
  signature `BoG_ *90.0&!!  Yy>` followed by the version 1, 6, 0 (SafeDisc
  1.06.000), and names `drvmgt.dll`, `Secdrv`, `wnaspi32.dll` and C-Dilla's
  splash bitmaps.
- Its imports are what a loader needs: `DeviceIoControl` (to the `secdrv`
  driver), `GetThreadContext`/`SetThreadContext`/`ResumeThread` (to start
  the game's process and patch it), `GetDriveTypeA`/`GetLogicalDriveStringsA`
  (to find the CD). None of the game is in it.

### `Trek.icd`: the game, encrypted

- Built 1999-05-13. Linker version 2.18 and the section names `AUTO`,
  `DGROUP`, `.bss` are Watcom C/C++'s: a 32-bit Watcom build, as many
  MicroProse games of the time were.
- Code (`AUTO`, 1.5 MB) and initialised data (`DGROUP`, 172 KB) have an
  entropy of 7.99 and 7.92 bits per byte: they're encrypted. There are no
  readable strings in them.
- The import table, resources (one 32x32 icon, `TREK_ICON`) and relocation
  table are in the clear.
- **The 37,096 relocated dwords are in the clear too.** Every one holds an
  address inside the image, which encrypted bytes would almost never do:
  the encryption skips them so Windows can relocate the image before it's
  decrypted *(inferred)*. They give the addresses the code refers to, but
  not the code or data around them: too little to work from on its own.

### What the imports say about the game

| DLL | Imports | What it means for Reluva |
|---|---|---|
| `mpr97.dll` | `MPRInitialize`, `MPRCreateSurface`, `MPRBlt`, `MPRBltTransparent`, `MPRBltFill`, `MPRSetChromaKey`, `MPRLockSurface`, `MPRGetBackbuffer`, `MPRGetDeviceResolution`... and 16 by ordinal | MicroProse's own renderer (the install also has `mpr555*.dll`, `mpr565*.dll` and `gen.mpr`), with variants for 15-bit (555) and 16-bit (565) colour: the game draws in hi-colour, 2D with colour-keyed pictures. The imports by ordinal are probably the 3D side, for ship combat *(guessed)* |
| `mss32.dll` | 25 `AIL_*` sample functions, among them `AIL_load_sample_buffer`, `AIL_sample_buffer_ready` and `AIL_set_sample_file` | Miles Sound System, digital samples only (no MIDI). Double-buffered playback: music and speech are streamed |
| `MSVFW32.dll` | `ICLocate`, `ICDecompress`, `ICSendMessage` | Video for Windows codecs, for the `.avi` movies; they're all Cinepak (`cvid`), 800x300 |
| `DPLAYX.dll` | ordinals 1 and 4 | DirectPlay (`DirectPlayCreate`, `DirectPlayLobbyCreateA`): multiplayer |
| `ole32.dll` | `CoCreateInstance`, `CoInitialize` | COM, for DirectX |
| `WINMM.dll` | `timeGetTime`, `timeBeginPeriod`... | Timing |
| `ADVAPI32.dll` | `RegOpenKeyExA`, `RegEnumValueA` | Settings (the install and CD paths) from the registry |
| `KERNEL32.dll` | `GetDriveTypeA`, `CreateFileA`, `FindFirstFileA`, threads... | Finds the CD; plain file access, by relative names that Reluva's case-insensitive lookup can resolve |

## In Reluva

Nothing reads these files. Detection keys on the data files instead
(`src/engines/botf/botf.cpp`), so a patched or replaced executable doesn't
stop a copy being recognised.

Decrypting `Trek.icd` needs the original CD and the SafeDisc driver
(`secdrv.sys`), which current Windows no longer loads. Getting round the
protection is a legal question (anti-circumvention rules in copyright
law, with exceptions for interoperability in some countries) that each
contributor would have to settle where they live, and the project doesn't
rely on it: see [PLAN.md](../PLAN.md#decisions), and
[CONTRIBUTING.md](../../../../CONTRIBUTING.md) for what may be used. The
unofficial "Joker patch", a replacement `trek.exe` without SafeDisc that
the modding community's own patches build on, isn't used either.

## Sources and checks

- **Where the facts come from:** the English retail install's `trek.exe`
  and `Trek.icd`, their headers, import tables, resources and relocations
  read with a PE parser, and each section's entropy measured (October
  2026). Nothing was decrypted or run.
- **How it was checked:** by reading the files; the movies' codec from the
  CD's AVI headers.

## Open questions

- Whether the official 1.0.2 update's `trek.exe` is still protected.
