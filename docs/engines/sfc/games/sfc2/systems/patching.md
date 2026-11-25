# Star Trek: Starfleet Command II: the 2.0.3.6 patch

> **Status:** done · **Code:** `src/engines/sfc/host/patcher.cpp`, `src/engines/sfc/sfc.cpp` (`addToImport`) · **Checked against:** `sfc2patch20022036.exe` applied to the English 1.0 import, in Reluva's x86 emulator · **Confidence:** confirmed unless marked

## Overview

Patch 2.0.3.6 (October 2002) is the game's last release
([PLAN.md](../PLAN.md#decisions)). Its download, `sfc2patch20022036.exe`,
is a setup that installs an RTPatch file and Pocket Soft's RTPatch
program to apply it, which changes the install in place. Reluva does the
same without Windows: it takes the patch's files out of the download
itself and runs the patch's own `Patch.exe` on the imported game in its
x86 emulator. The patched install is recognised as 2.0.3.6, and its
program runs to its main menu.

## How it works

### The download

- **A self-extracting program** holding one Microsoft cabinet (at 284,016),
  read by Reluva ([Microsoft cabinets](../../../../../common/mscab.md)):
  17 files, an InstallShield 5 setup (`Setup.exe`, `setup.ins`,
  `data1.hdr`, `data1.cab`...).
- **Its `data1.cab`**, read as the CDs' are
  ([InstallShield cabinets](../../../../../common/installshield.md)):
  `PATCHSFC2-2002-2036.RTP` (22,748,524 bytes, the patch), its `.RTD`,
  `Patch.exe` (34,816) and `Patchw32.dll` (171,520), RTPatch 5.20's
  program and library, `patchSFC2.bat` and the readme.
- **The batch file** is `patch.exe %1`: the program, given the install's
  folder. The setup that runs it isn't run by Reluva.

### Applying it

`Patcher` runs `Patch.exe` with `Patchw32.dll` loaded first, as their
import says, and Windows as Reluva stands in for it
([windows.md](../../../../../common/x86/windows.md)):

- **Drive C:** the imported game, which the patch changes in place.
- **Drive D:** the patch's own files, the program's folder; what it
  writes there (its temporary files) goes to a scratch folder instead.
- **Its command line:** `patch.exe C:\ D:\SFC2.RTP`, the install's
  folder and the patch file. The patch file is given a DOS (8.3) name,
  copied as `SFC2.RTP`: by its own name, `PATCHSFC2-2002-2036.RTP`, the
  program says "Could not find Patch File", as it looks for it by a
  short name Reluva's files don't have *(inferred)*.

On the 1.0 import it ends with 0 after about 49 seconds, and says what
it did:

| | Files |
|---|---|
| Patches applied | 309: 159 modified, 110 added, 40 deleted |
| Skipped, the new file already there | 15 |
| Ignored, the old file missing | 49: Taldren's script kit (`API\`), which the import leaves out |
| All | 373 |

It verifies each file it changes ("Verifying - Passed"). It adds files
of the script kit too; the import takes `API\` out again afterwards, as
it's not used ([PLAN.md](../PLAN.md)). `Patch.exe`, `Patchw32.dll` and
`patchSFC2.bat` are left in the game's folder, as the patch's setup
leaves them.

### Importing it

In the launcher, *Add patch...* (or `reluva-tool import <first CD>
<folder> -patch <second CD> -patch sfc2patch20022036.exe`) takes the
download. Once the CDs' files are copied, the import unpacks it, applies
it, and cleans up; a minute or so in all (76 seconds from both CDs).
The result is recognised as "Star Trek: Starfleet Command II (2.0.3.6)":
`StarFleet2.exe` (4,681,728 bytes, MD5 of the whole
`65e0f52967195e226c4251fec8bf4a4b`) and `sprites.q3` (80,550,682 bytes),
both changed by the patch ([README.md](../README.md#releases)).

`reluva-tool sfc patch <game> <download or its files> <out>` copies a
game to `<out>/SFC2` and patches the copy, saying what `Patch.exe`
writes; `-calls` lists the Windows functions it calls.

## In Reluva

The patched game runs as 1.0 does (`reluva-tool sfc host`): through its
start-up and title screen to its main menu, whose text now reads
"Dynaverse II or in standard multiplayer…", and 5 billion instructions
without a stop. It loads `ImportExportTable.dll`, which the patch adds
and which imports the C runtime's DLL, and a thread of it uses static
thread-local storage; both are stood in for.

## Sources and checks

- **Worked out** by running the patch's own program and following what
  it asked of Windows, until it ran to its end
  ([windows.md](../../../../../common/x86/windows.md)): its file
  mappings written back to their files, a 16-bit push and `JCXZ` with
  the address-size prefix in the CPU
  ([cpu.md](../../../../../common/x86/cpu.md)), its console output.
- **Checked:** the patched install from the download and the one from
  the patch's files taken out by a script are the same, 3,403 files; the
  import's is the same but for `API\`. The patch's own verification
  passes on every file it changes.
- **Not checked:** against an install patched on Windows (none to hand).

## Open questions

- Whether the patch's setup does more than run the batch file (its
  `setup.ins` isn't read): registry values it writes, say.
