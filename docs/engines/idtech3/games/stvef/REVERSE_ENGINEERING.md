# Star Trek: Voyager - Elite Force: how it was worked out

Reluva runs Star Trek: Voyager - Elite Force from the player's own copy, with
none of Raven's or id's code in it. This page describes how the facts
behind that were found and checked, so they can be checked again,
extended, or applied to another game. The facts themselves are in
[../../formats/](../../formats/) and this engine's [README](../../README.md), each with the
address or file it came from.

## Ground rules

- **All of Reluva's code is new.** Libraries such as SDL, Dear ImGui and the
  ones in `third_party/` are the exceptions.
- **Published sources are read for facts only, never copied.** These are:
  - the GPL source of Quake III Arena (id's 2005 release and ioquake3),
    for the formats and protocols Elite Force shares with it;
  - the GPL source of Jedi Outcast and Jedi Academy, from the same studio
    and engine generation, as a guide to the shape of Raven's single-player
    interface;
  - FFmpeg's Bink decoder (as ScummVM carries it), for the video format.
- **Raven's Elite Force game source is off-limits.** The single-player
  source Raven released in 2002 comes under a licence that forbids
  derivative works and reverse engineering. It isn't read, and nothing is
  taken from it ([idtech3.md](../../PLAN.md#game-code-open-questions),
  [CONTRIBUTING.md](../../../../../CONTRIBUTING.md)). The same goes for leaked source
  of any kind.
- **The retail game is studied as a legally owned copy, for
  interoperability.** Its binaries are disassembled and its data files
  read, to learn what an engine must provide for the game's own code and
  data to run. The game's code and data stay in the player's copy:
  - Holomatch's QVM bytecode and single player's DLLs are run, not
    translated;
  - no retail file is ever committed to the repository.

## Sources

### Sources of evidence

#### 1. Disassembly of the retail binaries

Each binary is disassembled into a text listing, which is then annotated:
every address that points at a string gets the string beside it. That
makes most functions easy to find:

- A function that prints `"Attempting write of chunk %s length %d\n"` is
  the saved-game chunk writer.
- A function that registers `"vid_restart"` names the command's handler
  next to it.
- A function that pushes `"$modlist"` is the file listing's mod branch.

From there:

- **Call tables.** The engine hands each DLL a table of function pointers
  (`GetGameAPI`, `GetUIAPI`). The DLL copies it to a global, so every
  engine call in the DLL is `call [global + 4*slot]`. Counting the calls to
  each slot, and reading the arguments pushed before them, gives each
  slot's signature.
- **Structures.** These are worked out from the offsets the code uses, for
  example the player state's fields in the save code at `+0xbc`, `+0x17c`
  and `+0x18c`.
- **Comparing versions.** 1.0, 1.1/1.2 and the Expansion Pack each have
  their own binaries. The same function is found in each by its strings,
  and the listings are compared:
  - the order of the chunk IDs written and read showed that the
    Expansion's engine saves exactly as 1.2's does;
  - the Expansion's import table turned out to have two new slots, with
    the old ones moved up (see
    [sp-interface.md](systems/sp-interface.md)).
- **Recording the result.** Each fact goes into the docs with the address
  it came from (for example "0x415700 in the Expansion's `stvoy.exe`"), so
  anyone with the same files can look again.

#### 2. Running the game's code and watching what it asks for

`reluva-tool` runs the game's programs outside the game window and logs
what they do:

| Command | What it runs |
|---|---|
| `qvm-info`, `qvm-dis`, `qvm-refs`, `qvm-run` | A QVM's numbered calls, its code, what refers to an address, and a run that answers every call with 0 and logs each call with its arguments and the text they point at |
| `x86-run`, `pe-info` | A DLL's exports and imports, and one of its functions run in the interpreter |
| `ui-run` | The menus (QVM or DLL), with clicks, key presses, cvar sets and screenshots |
| `game-run`, `sp-run` | Holomatch's server game, or single player's game, on a map: entities, frames, a joining player, saving and loading (`-saveload`), traces of every engine call (`-trace`) |
| `demo-run`, `net-run`, `net-find`, `net-master` | Recorded matches; hosting or joining a server; the network's server lists |

These runs report calls the host doesn't answer yet ("? not answered
yet"). That list of gaps drove much of the work. A call is understood when
the code around it in the disassembly and its arguments at run time both
fit.

#### 3. The data files

Surveys run over the whole retail data, so a reader is checked against
every file, not just a sample:

- `bsp-check`: all 67 maps load.
- `shader-check`: Elite Force's 1872 shaders use no directive outside
  Quake III's.
- `entity-survey`: every entity class and key the maps use
  ([entities.md](formats/entities.md)).
- `model-check`, `image-check`, `tex-survey` and `pk3-list`: models,
  textures and archives.

#### 4. Installers and releases

- **Releases.** Each release is told apart by the MD5 of the first 5000
  bytes of key files, plus their sizes (`reluva --hash`), taken from the
  CD, a fresh install, the 1.2 patch and the Expansion Pack's CD and
  install.
- **The 1.2 patch** is a Wise installer. Its layout was worked out from
  the file itself: a stub, a header, then raw deflate streams, each
  followed by a CRC-32. That lets Reluva take the patched files out
  without running it, on any system ([wise.md](../../../../common/wise.md)).

#### 5. The original game as the reference

- **Network play** was checked against the retail game patched to 1.2,
  with Reluva as server and as client
  ([network.md](systems/network.md)).
- **In-game behaviour** was checked by playing the retail game and Reluva
  side by side on Windows, and reporting what differed. Each report became
  a fix with its cause written down: the scoreboard's missing calls, the
  mode buttons' key check, latched cvars, autosave pictures and so on.

## Methods

### The approach: run the game's own code

The engine (`stvoy.exe` for single player, `stvoyHM.exe` for Holomatch) is
what Reluva replaces. The game logic is what Reluva runs as it is:

- **Holomatch:** three QVM programs in the paks (`vm/ui.qvm`,
  `vm/cgame.qvm`, `vm/qagame.qvm`), run by a QVM interpreter
  ([qvm.md](../../formats/qvm.md)).
- **Single player:** two 32-bit Windows DLLs (`efgamex86.dll`,
  `efuix86.dll`, built with Visual C++ 6), run by an x86 interpreter with a
  small Windows stand-in ([x86.md](../../../../common/x86/README.md)). This runs on any
  machine, not just x86 Windows.

So most of the reverse engineering is about the boundary: the calls each
program makes to the engine, with their numbers, arguments, structures and
meanings, and what the engine does in answer.

### Checking the work

- **Unit tests** (`ctest`) cover the readers, the interpreters and the
  hosts. Where the retail game can't be used, they use test programs of
  Reluva's own:
  - **QVMs** compiled from C in `tests/engines/idtech3/data/qvm/` with the Quake III
    compiler tools (built from ioquake3). `fake_game.c` and `fake_ui.c`
    exercise the engine's calls the way the retail programs do, and
    `test_vm.c` checks the interpreter against the same C compiled
    natively.
  - **A DLL** compiled from `tests/common/x86/testdll.c`, whose functions run both
    in the x86 interpreter and natively, for comparison.
- **The render check** (`reluva-render-check`) draws test frames through
  the 3D renderer and compares them with reference frames. CI runs it on
  macOS (Metal) and Linux (Vulkan), and builds and tests on Windows
  ([DEVELOPMENT.md](../../../../DEVELOPMENT.md#the-render-check), `.github/workflows/ci.yml`).
- **Across machines.** The same code must give the same results on every
  machine: CI on ARM found that fused multiply-add changed collision
  results, so it's turned off in the build.

### Writing it down

- Facts go in this folder's `formats/` (file formats, interfaces,
  protocols), and shared code's in [docs/common/](../../../../common/), each
  with its source: an address in a named binary, a file and its hash, or
  the published source it was read in.
- What Reluva does differently on purpose is listed in
  [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).
- What's assumed rather than confirmed is listed in
  [UNCONFIRMED.md](UNCONFIRMED.md).
- What still needs playing is listed in [TESTING.md](TESTING.md).

## The tools

`reluva-tool help idtech3` lists the engine's commands
(`src/engines/idtech3/tools/`); those used in the research are in the table under
[2. Running the game's code and watching what it asks for](#2-running-the-games-code-and-watching-what-it-asks-for).

## Working files not included

- **The disassembly listings** of the retail binaries (`stvoy.exe` and
  `stvoyHM.exe`, `efgamex86.dll` and `efuix86.dll`, 1.0's, 1.2's and the
  Expansion Pack's). They are made from the game's own code, so they stay
  out of the repository. To make them again from your copy:
  1. `objdump -d -M intel --no-show-raw-insn <binary> > <binary>.asm`
     (GNU binutils);
  2. list the binary's strings: each run of printable bytes ending in a
     NUL, by its address (the image base plus the section's virtual
     address plus its offset in the section);
  3. append each string, as a comment, to every line of the listing that
     names its address. A short script does it; that's what
     "[Disassembly of the retail binaries](#1-disassembly-of-the-retail-binaries)"
     searches.
- **Run logs and surveys.** Logs from `reluva-tool` (`qvm-run`,
  `game-run -trace`, `sp-run -trace`) and survey files (`entity-survey`'s
  output) are made again by running the command on your copy. The
  game-files test keeps each command's output in the build, under
  `gamedata/idtech3/` ([DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests)).
