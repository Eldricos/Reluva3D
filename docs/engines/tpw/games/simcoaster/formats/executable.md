# SimCoaster: the executable (`Game.exe`)

> **Status:** partial · **Code:** none (not read by Reluva) · **Checked on:** the US release's `Game.exe`, as installed and on the CD (the same file) · **Confidence:** confirmed unless marked

## Overview

`Game.exe` is the whole game, wrapped by SafeDisc 2: its code and data
are encrypted, and decrypted only when it runs from its CD with SafeDisc's
driver. Reluva needs none of it: it recognises the game by its data files
and rebuilds the game from them. What's here is what the file's
unencrypted parts say.

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `Game.exe` | 7,504,054 | `891a6ba536731ea643cc8693a38762da` |

## Layout

### The header

- Built 2001-01-10 with Microsoft's linker 6.0 (Visual C++ 6), image
  base `0x400000`, entry point in SafeDisc's own section `stxt371`.
- It carries the SafeDisc signature `BoG_ *90.0&!!  Yy>` followed by the
  version 2, 5 (SafeDisc 2.05). The install and the CD have the files
  SafeDisc 2 uses beside it: `secdrv.sys`, `drvmgt.dll`, and the
  `00000xxx.016` and `.256` files.

### The sections

| Section | Size in the file | Entropy (bits a byte) | What it is |
|---|---|---|---|
| `.text` | 5.1 MB | 7.92 | The code, encrypted |
| `CSEG` | 112 KB | 7.70 | More code, encrypted *(inferred)* |
| `.rdata` | 216 KB | 3.84 | Read-only data, in the clear |
| `.data` | 584 KB | 7.94 | Data, encrypted |
| `.idata` | 16 KB | 5.46 | Imports: only SafeDisc's |
| `DSEG`, `UVA_DATA`, `IDCT_DAT`, `TQIA_DAT`, `GRPOLY_D`, `LBMPEG_D` | 4 KB to 24 KB each | 3.8 to 7.7 | Libraries' own data. `IDCT`, `TQIA` and `LBMPEG` suggest the movie and MPEG audio decoders *(guessed)* |
| `.rsrc` | 16 KB | 3.39 | Resources |
| `.reloc` | 348 KB | 6.20 | Relocations |
| `stxt774`, `stxt371` | 4 KB, 14 KB | 7.67, 6.22 | SafeDisc's loader |

### What it says about the game

- **The import table lists only `GetModuleHandleA`, `GetProcAddress`,
  `GlobalAlloc`, `GlobalFree`, `ExitProcess` and `MessageBoxA`:**
  SafeDisc finds the rest itself when it runs. The DLLs' names are in the
  clear: `DDRAW.dll`, `DINPUT.dll`, `DSOUND.dll`, `QMIXER.dll` (QSound's
  mixer, installed beside the game), `WSOCK32.dll`, `USP10.dll`,
  `IMM32.dll`, `WINMM.dll`, and the online services' `wea*r.dll`. So it
  draws with DirectDraw and Direct3D, and plays sound through QSound on
  DirectSound *(inferred)*.
- **zlib 1.1.3** is linked in (its copyright strings are in the clear):
  some of the game's files are deflated *(inferred)*.
- **About 19,000 short strings** can be read in the clear sections, but
  hardly any of the game's: the file names and messages are in the
  encrypted ones.

## In Reluva

Nothing reads this file. Detection keys on the data files instead
(`src/engines/tpw/games.cpp`), so a replaced executable doesn't stop a
copy being recognised.

Running it in Reluva's x86 emulator, as Need for Speed III's program is
run, would mean decrypting it: that needs the CD and SafeDisc's driver,
which current Windows no longer loads, and getting round the protection
is a legal question each contributor would have to settle where they
live. The project doesn't rely on it: no decrypted or "no-CD" copy is
used, to run the game or to read its code
([PLAN.md](../PLAN.md#decisions), and
[CONTRIBUTING.md](../../../../../../CONTRIBUTING.md) for what may be
used).

## Sources and checks

- **Where the facts come from:** the US release's `Game.exe`, its
  headers, sections and import table read with a few lines of Python,
  each section's entropy measured, and its readable strings listed
  (November 2026). Nothing was decrypted or run.
- **How it was checked:** the installed copy and the CD's compared byte
  for byte: they're the same file.

## Open questions

- Whether a patch for SimCoaster was released, and whether its
  executable is still protected.
