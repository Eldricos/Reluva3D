# LEGO Alpha Team: how it was worked out

How Reluva's support for LEGO Alpha Team was built: where each piece of
knowledge came from, the tools used, and how the results were checked.
The details of each step are in [../../systems/](systems/), [../../research/](research/) and the
format documents under [../../formats/](formats/).

## Ground rules

Reluva is a clean-room project ([../../../../../CONTRIBUTING.md](../../../../../CONTRIBUTING.md)):

- **Used:** the game's own files (the install and the CD, provided by its
  owner for study), observing the game run, reverse engineering of the
  owner's own executables for interoperability, and public documentation:
  the Win32 API documentation, the DirectX 7 SDK documentation and
  headers, Aureal's A3D SDK documentation (`ia3dapi.h`), and NetImmerse's
  and NifTools' public material.
- **Not used:** the NetImmerse/Gamebryo SDK source (licensed to studios;
  copies circulate) or any leaked code for this game. Anyone who has read
  either doesn't work on this engine.
- **Not committed:** any of the game's files, or anything derived from
  them that would stand in for them. The tests run against a copy of the
  install kept outside the repository.
- **Documented:** each format document and each fix says how it was found.

## Sources

### What came from the game's owner

Game files can't go in the repository. The whole install has been provided
for study: the executables, the DLLs and all eight archives.

Also useful: the CD's `readme.txt` and `Strings_en.txt` (the game's own
description of its keys and options), and screenshots or a recording of
the original running.

## Methods

### 1. The install and the archives

- The install was listed file by file ([formats/install.md](formats/install.md)):
  the executable, the DLLs (NetImmerse's `Ni*.dll`, the game's own
  `*Comp.dll` and the behaviour DLLs), and eight `.pac` archives.
- The `.pac` layout ([formats/pac.md](formats/pac.md)) came from the files
  alone: `reluva-tool hexdump`, `words` and `table` show raw bytes as
  integers and floats; the directory's offsets and sizes were checked
  against the file's length and every entry's data. `pac-list` and
  `pac-extract` read them.

### 2. The data formats

- **Models (`.FIN`, [formats/fin.md](formats/fin.md))** are NetImmerse
  streams. The class names are written in the stream. Each class's
  fields were first worked out from the data (`chunks`, `words`), then
  settled from the DLLs' own exported functions: NetImmerse's DLLs export
  their classes' `LoadBinary`, `LinkObject` and accessors by name, so
  reading each `LoadBinary` (disassembly of the owner's DLLs) gives every
  field's order and type. Former guesses are marked as settled in the
  document.
- **`reluva-tool netimmerse fin-check`** reads every model in `Fin.pac`, counts each
  class and each field's values, and checks that every byte is consumed:
  a wrong field size shows as a model not read to its end.
- **The data viewer** (F12 in the game, or the engine without
  `LEGOATeam.exe`) draws what was decoded. Models with their textures,
  animations, morphs, lights and flip-books were compared by eye with the
  game's own frames.
- **Fonts (`.DDF`, [formats/ddf.md](formats/ddf.md)), textures (TGA),
  sounds (`.DDS`) and text** were worked out the same way: the data
  first, then the code that reads it.
- **The intro movie** is Indeo 5 in AVI
  ([../../../../common/video/indeo5.md](../../../../common/video/indeo5.md)). Intel
  never published the format; its one full public description is FFmpeg's
  LGPL decoder, which was used as the reference (a choice recorded in that
  document, the licence being compatible with Reluva's GPL 3). The
  result was checked picture by picture, byte for byte, against FFmpeg's
  decoding of the same file.

### 3. Running the game

Three ways were measured before choosing ([systems/running.md](systems/running.md)): reimplementing the game natively, running all its code with
NetImmerse in an emulator, or the game's code with a native NetImmerse.
The second (B1) was chosen: it needs no knowledge of NetImmerse's
internals, only of what the program calls.

- **The calls it makes** were counted from the import tables of the
  executable and every DLL: 792 into NetImmerse's DLLs, and the Windows,
  DirectX, A3D and C runtime functions listed in
  [running.md](systems/running.md).
- **The x86 emulator** ([../../../../common/x86/README.md](../../../../common/x86/README.md)) loads
  the PE images, relocates them, binds their imports, and runs them. Its
  instructions follow Intel's manuals; it was checked with unit tests and
  by running the game.
- **The stand-ins** for Windows (KERNEL32, USER32, GDI32, ADVAPI32,
  OLE32), the C runtime (MSVCRT, and our own MSVCP60 built from source),
  DirectDraw, Direct3D 7, DirectInput and A3D were written from their
  public documentation. Every COM method's vtable slot and the number of
  words it pops were checked against the game's call sites.
- **Finding out what the game does** at a given moment:
  - `at-run -watch dll!export` reports each call to a function with its
    arguments and results; `-calls`, `-trace`, `-log`, `-files` and
    `-dump` show calls, instructions, messages, files opened and memory;
  - `-from` and `-quit` limit a run to the frames of interest;
  - the game's DLLs were disassembled around what those showed, to follow
    the logic (for example the camera's choice of view, `DDIcam` and
    `FindNewBase`, or why `AutoLoadPuzzle` shows a black screen).
- **Where ours was wrong**, the trace pointed at the stand-in that
  answered differently from Windows or DirectX. Examples: A3D's
  `GetWaveFormat` answering success for an empty source (sounds loaded
  silent), Direct3D 7's single-precision FPU (the camera framed the skull
  door wrongly), RDTSC across `Sleep` (the game's processor-speed test),
  and MSVCRT's text mode (the save's CR-LF line ends).

### 4. Checking the results

- **Repeatable runs.** `at-run` with `-fixed-clock` makes the game's
  clock follow the instructions run, so a run with scripted input
  (`-click`, `-point`, `-key`) repeats exactly. Its frames (`-shots`) and
  its instruction count are the reference: speed work on the emulator,
  the JIT and the drawing was required to leave both unchanged to the
  byte, at 640x480x16 and 1024x768x32.
- **The engine's tests** keep these checks running
  ([TESTING.md](TESTING.md), "Automated checks"): the game-files test
  plays the Mission One routes at both sizes and compares their frames
  and instruction counts with the committed records; the basic engine
  test and the unit tests need no game files, and run in CI.
- **Every mission.** `at-sweep` runs all 54 missions past Go;
  `at-sweep -solved` runs every Adventure mission with the game's own
  solved layout (`<space>_SOLVED.PUZ`) until the game saves the next
  mission.
- **The original.** Recordings of the original game made by its owner
  (a full playthrough, and Mission One played with known camera input)
  were compared with Reluva's frames side by side. That's how the FPU
  precision difference was found and the camera at Mission One's end was
  confirmed.
- **Speed** was profiled on Windows with Visual Studio's profiler: its
  `.diagsession` traces were read by our own ETL parser and symbolized
  with the build's PDB, down to the hot instructions, and locally with the
  emulator's own sampling (`at-run -profile`, `-profile-addresses`).

## The tools

All in `reluva-tool`: the ones for any game in `src/tools/`, this engine's
in `src/engines/netimmerse/tools/` (`reluva-tool netimmerse <command>`;
`reluva-tool help netimmerse` lists them). The ones used for this game:

| Command | What it does |
|---|---|
| `hexdump`, `words`, `table`, `chunks` | show any file's bytes as hex, integers, floats or a chunk tree |
| `netimmerse pac-list`, `netimmerse pac-extract` | list and extract `.pac` archives |
| `netimmerse fin-check` | read every model and tally classes and fields |
| `netimmerse at-run` | run the game headless, with scripted input, tracing and frames |
| `netimmerse at-sweep` | run many missions at once, solved or not |

## Working files not included

None yet.
