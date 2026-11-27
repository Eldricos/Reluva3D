# Star Trek: Starfleet Command II: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and
  may not be used.
- **Taldren's mission-script kit isn't used** ([PLAN.md](PLAN.md#decisions)).

## Sources

### The copy studied

Surveyed November 2026 from the owner's copy: the two English retail CDs,
an install made from them, and the 2.0.3.6 patch's download
(`sfc2patch20022036.exe`, 23,559,287 bytes). Only samples of them were
fetched and studied: the executables, one file of each data format, the
text files, and the installers' headers.

- **`StarFleet2.exe` 1.0:** 4,149,248 bytes, built 27 November 2000 (its
  PE time stamp), version resource 1.0.0.2, product name "StarFleet
  Command"; sections `.text`, `.rdata`, `.data`, `.tls`, `.rsrc`, none
  added by a protection. MD5 of the whole file
  `1ef06195eb139fdcd69c49fb174e4164`.
- **A mission, `Met_1Scout.scr` 1.0:** 356,352 bytes, built 27 November
  2000, a DLL at 0x10000000 importing only KERNEL32, USER32 and ADVAPI32,
  exporting `ScriptEntry` and `MissionSpecEntry`.
- **`SFC2Intro.exe`:** 68,947,456 bytes, built 3 October 2000, a Bink
  file (`BIKi`) inside a small player.
- **The CDs' installer:** `data1.hdr`'s version field 0x01005201
  (InstallShield 5).
- **The patch:** built 5 September 2001; one Microsoft cabinet inside,
  holding an InstallShield 5 setup whose `data1.cab` holds
  `PATCHSFC2-2002-2036.RTP` (22,748,524 bytes), `Patch.exe`,
  `Patchw32.dll`, a batch file and a readme dated 28 October 2002. The
  `.RTP`'s file names list what it changes; it's applied by running
  `Patch.exe` in the emulator ([systems/patching.md](systems/patching.md)).
- **The readmes:** the game's (26 November 2000) and the patch's: the
  releases, the patches' changes, the known issues.
- **Taldren's mission-script kit:** its folders' file list was seen, and
  its readme's requirements and installation steps read, while surveying
  the install; nothing of it was opened beyond that or is used.

### Published

- Star Fleet Battles' Cadet rules, in the install's `SFB/` folder: for
  how combat works, when the simulation is studied.

## Methods

- **Executables:** their PE headers, sections, imports, exports and
  version resources read with a short script, and the DirectX interfaces
  they ask for found by searching them for those interfaces' IIDs (from
  the DirectX SDK's documentation).
- **Data files:** each kind identified by its first bytes and its text,
  then its layout worked out from the files alone: chunk and record
  lengths that add up to each file's end, counts checked against what
  they count, values compared across every file of a kind (50 fonts,
  102 models, 19,353 images, those first downloaded) with short Python
  scripts, then a reader in Reluva run over every file (since, over the
  whole install imported from the CDs: 126 fonts, 317 models). Pictures and models were then looked at:
  images written as PNGs, and the data viewer run under Xvfb with
  Mesa's software Vulkan and screenshotted. Each format's page says what
  settled each field.
- **The program, hosted:** `StarFleet2.exe` run in Reluva's x86
  emulator (`reluva-tool sfc host`), each stop read from the stand-in it
  called, the C++ type it threw and the text in it, and the program's
  code disassembled around it (`reluva-tool sfc disasm`); the calls
  into the stand-ins listed from a chosen address (`-calls`), with the
  interfaces asked for and the names modules and functions are looked
  for by; the registers and memory where the program gets to an address
  (`-watch`); each change to a word of memory and the instruction that
  made it (`-writes`); the code first run between two frames
  (`-newcode`), which finds what a click runs; how many instructions ran
  in each 16 bytes of code between two frames (`-profile`), which finds
  where a busy program is busy; with `-watch`, the callers too, by the
  frame pointers' chain; how fast it ran between two frames, untraced
  (`-rate`): instructions and frames a second, and the program's own
  clock
  ([systems/hosting.md](systems/hosting.md)).
- **Installers:** the patch's Microsoft cabinet listed and taken apart
  with a short script, written from Microsoft's description of the
  format, and since by Reluva ([mscab.md](../../../../common/mscab.md)); its InstallShield 5 cabinet, and the CDs', listed
  with `unshield` (a free tool, MIT licence) as the check on Reluva's
  reading of version 5, worked out from the CDs' own bytes with short
  Python scripts ([InstallShield cabinets](../../../../common/installshield.md));
  `unshield`'s source wasn't read.
- **The owner's install:** made by the CDs' own setup on Windows,
  copied to a Google Drive folder. The first download of it was short:
  the script listing the folder read only the first 50 entries of each
  folder, so larger folders (`Assets/Scripts`, `MODELS`, `aafonts`,
  `textures`) came down cut short, and for a while the install was
  taken to be missing files. Listed in full since, it's whole, and the
  import from the CDs is byte for byte the same.

## The tools

`reluva-tool help sfc` lists the engine's commands
(`src/engines/sfc/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool sfc files` | Lists a game folder's files (the template's first command) |
| `reluva-tool sfc sprites` | Lists sprites.q3's images and decodes each |
| `reluva-tool sfc sprite-png` | Writes one of sprites.q3's images as a PNG |
| `reluva-tool sfc sprite-screen` | Draws one of sprites.q3's objects (a screen, a button) from its fields, as a PNG |
| `reluva-tool sfc fonts` | Reads every .bft font |
| `reluva-tool sfc models` | Reads every .mod model, and lists textures it names that aren't there |
| `reluva-tool sfc sounds` | Decodes every WAV in the sound zips |
| `reluva-tool sfc table` | Shows a tab-separated or string table |
| `reluva-tool sfc intro` | Reads the opening movie out of SFC2Intro.exe, decodes it and writes frames as PNGs |
| `reluva-tool sfc host` | Runs StarFleet2.exe in the x86 emulator, headless, and says how far it gets |
| `reluva-tool sfc patch` | Copies a game and applies the 2.0.3.6 patch to the copy, running its Patch.exe in the emulator |
| `reluva-tool sfc disasm` | Disassembles StarFleet2.exe, or another program given by its path (a mission's DLL), between two addresses |

## Working files not included

- The game's and the patch's files: from your own CDs, install and the
  patch's download.
