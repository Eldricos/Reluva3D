# LEGO Alpha Team: plan

Status: planning. Nothing is implemented yet. This is the plan for adding
LEGO Alpha Team (LEGO Media, 2000), the first of the LEGO games Reluva
is meant to run. The install is described in
[../formats/alphateam/install.md](../formats/alphateam/install.md).

## What the install shows

- **It's a NetImmerse game.** `NiMain`, `NiAnimation`, `NiCollision`,
  `NiDirect3d` and `NiA3dSound` are NetImmerse's DLLs (Numerical Design
  Ltd.; the engine later became Gamebryo). Models and scenes will be NIF
  files of an early version (NetImmerse 3.x is the likely one for mid-2000).
  NIF is well documented in public by the NifTools project (`nif.xml`),
  which can be read as a format reference, as we used published specs for
  PE/COFF and x86.
- **The game's own code is in four DLLs.** `LEGOATeam.exe` is 60 KB;
  `BaseClassComp`, `BehaviorComp`, `LegoLogicComp` and `LoadComp` (about
  1 MB together) hold the game. They call NetImmerse through its C++
  classes (mangled exports, virtual functions), not a C table like Elite
  Force's.
- **Data is in `.pac` archives** read by `DDPackedDirFileLib.dll`, a
  format of the developer's own. One archive per kind of data, and one
  per language (`Locale_en.pac`).
- **Sound** went through Aureal A3D (`NiA3dSound`), which no current
  machine has; Reluva's mixer replaces it.

## Where the engine goes

Following idtech3/eliteforce: an engine for the technology, a subfolder
for the game.

- Engine id `netimmerse`, in `src/engines/netimmerse/`: `.pac` archives,
  NIF reading, the scene graph, rendering through `gfx::Renderer3D`, sound
  through `common/audio`. Built behind `RELUVA_ENGINE_NETIMMERSE`, off by
  default until the game starts.
- Game id `legoalphateam`, with what is specific to it in
  `src/engines/netimmerse/alphateam/`.
- Formats documented under `docs/formats/alphateam/` (`.pac`, the
  game's own files) and `docs/formats/netimmerse/` (NIF, as this game
  uses it), with how each was worked out, as CONTRIBUTING.md asks.
- Tests in `tests/test_netimmerse.cpp`, on small files built by the tests
  themselves (no game data in the repository).

If later LEGO games turn out to use NetImmerse too, they join this
engine; others get their own. Check each one's install for `Ni*.dll`
before deciding.

## Phases

Each phase ends with something that runs and is committed.

### 1. Detection and the archives

- Hash the key files (`reluva --hash LEGOATeam.exe Locale_en.pac Bhvr.pac`)
  and add the detection table; the launcher then finds the game.
- Work out the `.pac` format from the files: compare the archives'
  headers, find the directory and how names, offsets and sizes are kept,
  check for compression. `Bhvr.pac` (4.5 MB) is the smallest to start on.
  `DDPackedDirFileLib.dll` is small enough to read directly if the files
  alone don't settle it.
- `PacArchive : Archive`, so `.pac` files join a `SearchSet` like any
  other archive.
- `reluva-tool pac-list` and `pac-extract`, as for New Worlds' `.cat`.
- Write down what the archives hold: extensions and counts per archive.
  That inventory decides phases 2 and 3.

### 2. Data viewer

Like New Worlds and Elite Force before they ran, the engine first opens
as a viewer; it checks each reader against all the retail files.

- **Textures**: whatever format the inventory shows (NIF-embedded pixel
  data, or loose BMP/TGA, which `common/image` reads already).
- **Models**: NIF, the block types this game's files use (nodes,
  triangle shapes and their data, texturing and material properties,
  keyframe controllers, skinning). A `reluva-tool nif-check` that reads
  every NIF and lists any block type not handled, as `shader-check` did.
- **Sounds**: likely WAV (PCM or IMA ADPCM, both decoded already).
- **Text**: the strings in `Locale_en.pac`.
- **Levels**: what `Map.pac`/`LMap.pac` hold, drawn in 3D.
- **Intro movie** (`lmiintro.avi`, CD only): an AVI reader and whichever
  codec it uses. Not needed to play, so last.

### 3. How the game will run: a decision

Two ways, as with Elite Force:

- **A. Reimplement the game.** Its rules, behaviours and menus written
  anew from the data, observation and study of the DLLs. Fully native,
  but every mission's logic has to be worked out.
- **B. Run the game's own DLLs in the x86 interpreter** (`common/x86`),
  as Elite Force's single player runs `efgamex86.dll`. Faithful by
  construction. Its cost is the boundary:
  - B1: run the four game DLLs and NetImmerse's in the interpreter;
    stand in only for Windows, DirectX 7 (Direct3D, DirectInput,
    DirectSound) and A3D. That's a far wider Windows than Elite Force
    needed: files, windows and messages, COM interfaces, timers,
    probably threads and C++ exceptions (structured exception handling
    isn't dispatched yet). NetImmerse's scene graph would run in the
    interpreter every frame, so speed matters.
  - B2: run the four game DLLs in the interpreter, and stand in for
    NetImmerse natively: the classes and functions they import from
    `Ni*.dll`, backed by phase 2's scene graph. Less Windows, and the
    heavy per-frame work runs natively, but C++ objects cross the
    boundary (vtables and object layouts in emulated memory must match
    NetImmerse's).

What decides it is measurable, so measure first:

- `reluva-tool pe-info` on every DLL: how many imports each game DLL
  takes from `Ni*.dll`, from each other, and from Windows/DirectX.
- `reluva-tool x86-run` on each game DLL: how far its start-up gets
  and what it calls.
- How much of the game is data (behaviours in `Bhvr.pac`) and how much
  is code.

Recommendation: decide after that census. If the game DLLs use a
bounded part of NetImmerse (a few hundred imports), B2 gives the most
faithful game for the least risk, and phase 2's native renderer is
what it draws with. If they use most of it, or the logic turns out to
be mostly data, A becomes the cheaper path.

### 4. Playing

Whichever way phase 3 chose:

- the front end (title, menus, mission select) and saving, in the
  game's save folder (`GameTarget::savePath`);
- a mission played start to finish, then every mission;
- sound placed in 3D as A3D placed it, and music;
- the game's options (resolution, detail, volume) applied through
  `System`;
- checked against the original running on a period machine or a
  virtual machine: screenshots and recordings side by side.

## What's needed from the game owner

Game files can't go in the repository, and this environment doesn't
have them. Development needs one of:

- the files themselves in the session (uploaded, or in a folder the
  session can read), for format work; `Bhvr.pac`, `Etc.pac`,
  `LEGOATeam.exe` and the DLLs first;
- or the output of the tools above run on a local install
  (`reluva --hash`, `reluva-tool pe-info`, `pac-list` once written).

Also useful: the CD's `readme.txt` and `Strings_en.txt` (the game's own
description of its keys and options), and screenshots or a recording of
the original running.

## Clean-room notes

- Allowed: the game's data, observing it running, reverse engineering
  of the user's own executables, NetImmerse's and NifTools' public
  documentation.
- Not allowed: NetImmerse/Gamebryo SDK source (it was licensed to
  studios, and copies circulate), or any leaked code for this game. If a
  contributor has read either, they shouldn't work on this engine.
- Each format document says how it was worked out.
