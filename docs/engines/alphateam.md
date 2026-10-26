# LEGO Alpha Team: plan

Status: planning. Nothing is implemented yet; the choice of how the game
runs (phase 3) is made, from measurements of the retail files. This is the plan for adding
LEGO Alpha Team (LEGO Media, 2000), the first of the LEGO games Reluva
is meant to run. The install is described in
[../formats/alphateam/install.md](../formats/alphateam/install.md).

## What the install shows

- **It's a NetImmerse game.** `NiMain`, `NiAnimation`, `NiCollision`,
  `NiDirect3d` and `NiA3dSound` are NetImmerse's DLLs (Numerical Design
  Ltd.; the engine later became Gamebryo). The DLLs call themselves
  "NetImmerse Version 7.0". The models (`Fin.pac`'s `.FIN` files) are its
  streams under a header of the game's own (`Dweezil 23`).
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
  through `common/audio`. Built behind `RELUVA_ENGINE_NETIMMERSE`, on by
  default (it was off at first, which left the launcher not recognising
  the game in a default build).
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

- Done: detection by `Locale_en.pac`'s hash, with a fallback by file names
  for other releases. The launcher finds the game. Starting it opens every
  archive and shows what each holds.
- Done: the `.pac` format ([pac.md](../formats/alphateam/pac.md)), checked
  against all eight retail archives.
- Done: `PacArchive : Archive` (`src/engines/netimmerse/pac_archive.cpp`), so
  `.pac` files join a `SearchSet` like any other archive. It's tested in
  `tests/test_netimmerse.cpp`, on archives the tests build.
- Done: `reluva-tool pac-list` and `pac-extract`, as for New Worlds' `.cat`.
  On all eight archives they give the same 2,462 files, byte for byte, as
  a separate script written from the format notes.
- Done: what the archives hold ([pac.md](../formats/alphateam/pac.md)):
  sounds as headerless PCM (`.DDS`), models (`.FIN`), TGA textures and
  lightmaps, puzzles, fonts and text.

### 2. Data viewer

Like New Worlds and Elite Force before they ran, the engine first opens
as a viewer; it checks each reader against all the retail files. Started
from the launcher, the engine now shows these pages (keys in
`src/engines/netimmerse/viewers.h`; Tab moves between them):

- Done: **Archives**: what each `.pac` holds.
- Done: **Models**: every `.FIN` model, characters, parts and levels alike,
  drawn with the GPU renderer as its scene graph places its shapes. It has
  textures, lightmaps (a second pass multiplied over the first), vertex
  colours, materials and alpha blending, and the nearest level of
  level-of-detail nodes. The format is in
  [../formats/netimmerse/fin.md](../formats/netimmerse/fin.md);
  `reluva-tool fin-check` reads all 147 models. Every object of the 21
  classes read ends exactly where the next begins, and every link and image
  name resolves.
  - **Characters.** Their limbs are skins that follow bones; all 15 are
    whole, standing in the skeleton's rest pose.
  - **Billboards** face the camera.
  - **Hidden objects** (effects the game shows on events) and the
    characters' far-away stand-ins are left out unless H shows them.
  - Not yet: animation (so no poses but the rest pose), morph shapes,
    lights, glows (`DDCorona`), and flip-book textures (the first frame
    shows).
- Done: **Textures**: all 1,609 TGA images in `Map.pac`, `LMap.pac` and
  `Etc.pac`. All decode with `common/image`, 479 of them with alpha.
- Done: **Sounds**: all 498 `.DDS` sounds (`decodeDds`), played through the
  mixer.
- Done: **Text**: the language's text files and the puzzles.
  `parseLocaleText` reads the `==key==` format for later use.
- **Fonts**: the game's own (`.DDF` with their TGA pages), still to do.
- **Intro movie** (`lmiintro.avi`, CD only): an AVI reader and whichever
  codec it uses. Not needed to play, so last.

The viewer was checked in this container with a software stand-in for the
renderer, since it has no GPU: frames of characters, parts and levels
looked as expected. Sound playback wasn't heard, as the container has no
audio device.

### 3. How the game will run: measured, B1 chosen

Measured on the retail files (October 2026) with a PE import census of
every module, the 74 DLLs inside `Bhvr.pac` included.

**The game is mostly compiled code.** Alpha Team is a contraption-puzzle
game. Each puzzle part (`T0001`, `U0188`...) has its own behaviour DLL
in `Bhvr.pac`, and the puzzles are text files (`.PUZ`) that place the
parts. Most missions ship a `_SOLVED.PUZ` too. The game's code adds up to:

| Code | Size |
|---|---|
| `LEGOATeam.exe` and the four `*Comp.dll` | 1.0 MB |
| 74 behaviour DLLs in `Bhvr.pac` | 4.5 MB |

**What that code calls:**

| Called | Unique functions | Notes |
|---|---|---|
| NetImmerse (`Ni*.dll`) | 792 | NiMain 596, NiAnimation 119, NiA3dSound 41, NiCollision 32, NiDirect3d 4 |
| ↳ of which constructors | 58 | 42 NetImmerse classes created directly (NiNode, NiTriShape, NiCamera, NiStream...) |
| ↳ vtables / static data | 19 / 95 | the game subclasses NetImmerse's classes |
| MSVCP60 (C++ library) | 129 | `std::string`, fstreams, locale |

**What everything calls, NetImmerse included**, i.e. what B1 has to supply:

| Library | Functions |
|---|---|
| KERNEL32 | 44: files, file mapping, find-file, critical sections, one event, timers, `LoadLibraryA`/`GetProcAddress`, the CD check |
| USER32 / GDI32 | 27 / 3: one window, its messages, the cursor |
| ADVAPI32 | 5: the registry |
| WINMM | 1: `timeGetTime` |
| DDRAW | 3: `DirectDrawCreate(Ex)` and `Enumerate`. Direct3D 7 is reached through it ("NiDirect3D NetImmerse Version 7.0"). Only `NiDirect3d.dll` uses it |
| DINPUT | 1: `DirectInputCreateA` (LoadComp) |
| OLE32 | 3: `CoCreateInstance` for Aureal A3D (NiA3dSound) and Direct3D |
| MSVCRT | 79: the C library, `_beginthread` (one thread, started by the exe), C++ exceptions (`_CxxThrowException`, LoadComp) |
| MSVCP60 | 156 |

No DirectSound, no other threads, no SSE or MMX needed (the interpreter
reports a Pentium, so any CPU-specific paths stay off).

#### A. Reimplement the game natively

Pros:
- Fastest at run time, and the most portable (no interpreter).
- Free to improve: widescreen, higher resolutions, modern input.
- The data side is needed anyway (phase 2), so it isn't wasted.

Cons:
- 5.5 MB of compiled C++ to rewrite: 74 puzzle parts' behaviours, plus
  the rules, physics and menus in the four `*Comp.dll`.
- A contraption puzzle only works if every part behaves exactly as the
  original's did (timings, momentum, electricity). Small differences
  break the shipped puzzles' solutions.
- Needs a complete native NetImmerse too: scene graph, animation,
  collision, NIF loading.
- By far the most work, with the least certain result.

#### B1. Run all the original code in the x86 interpreter, NetImmerse included; stand in for Windows and DirectX

Pros:
- Faithful by construction. Every behaviour DLL, NetImmerse's own physics,
  collision and animation run as shipped. The `_SOLVED.PUZ` files give a
  ready test: load one, run it, see it solve.
- The Windows surface is small and fully documented: about 170 plain
  functions plus three COM areas (DirectDraw/Direct3D 7, DirectInput,
  A3D). Elite Force's Win32 process and PE loader already cover the
  loading, relocation, imports and much of KERNEL32.
- Every interface it implements is public (Microsoft's API docs, the
  DirectX 7 SDK docs, Aureal's A3D SDK docs). No NetImmerse internals
  need to be guessed.
- The work carries over to any later LEGO game built the same way, and to
  other Win9x/DirectX 7 games.
- It can become native piece by piece. Any export of `NiMain.dll` (matrix
  maths, bounds, culling) can later be swapped for a native function where
  profiling shows the time goes.

Cons:
- Speed. The interpreter runs about 50 million instructions a second.
  The game asked for a Pentium II 233, roughly 4 to 6 times that, and
  here NetImmerse's per-frame scene-graph work runs interpreted too. It
  needs a decoded-instruction cache (already noted in x86.md as the next
  speed-up) or a simple JIT, plus native replacements for hot NiMain
  exports.
- MSVCP60 and MSVCRT aren't in the install. MSVCRT can be stood in for
  natively (C functions). MSVCP60 is C++ with object layouts
  (`std::string`, streams) that must match VC6's. That could be a native
  stand-in, or a small replacement DLL built for x86 with clang, like the
  tests' DLL, and run in the interpreter.
- New interpreter features:
  - C++ exceptions, i.e. structured exception handling dispatch, which is
    listed as a known limit today;
  - one extra thread (`_beginthread`), run cooperatively;
  - COM objects (vtables of native functions);
  - `GetVolumeInformationA` answering the CD check.
- Direct3D 7's fixed-function pipeline (render states, texture stages,
  vertex formats) must be mapped onto `gfx::Renderer3D`, which may need a
  few additions.

#### B2. Run only the game's code in the interpreter, with a native NetImmerse

Pros:
- NetImmerse's heavy per-frame work runs natively, so speed isn't a worry.
- Little of Windows or DirectX is needed: just what the game's own
  modules call.

Cons:
- The boundary is C++, not C. The game creates 42 NetImmerse classes
  itself, subclasses them through their vtables and reads 95 of their
  static data items. Its compiled code also reads and writes their fields
  directly through inlined accessors. A native NetImmerse would need every
  class's binary layout and vtable order to match the original exactly,
  with nothing to check against but the binaries.
- 792 functions to reimplement with matching behaviour, including NIF
  streaming (the game registers its own loaders with `NiStream`) and
  NetImmerse's collision and animation, which the puzzles' physics depend
  on.
- It amounts to rewriting NetImmerse to be binary-compatible: more work
  than B1 and less faithful, while still needing the interpreter.

**Decision: B1.** It's the only option whose hard parts are all documented
public interfaces, it keeps the puzzles' physics exact, and the
`_SOLVED.PUZ` files give an automatic check. Its main risk, speed, has
known remedies that can be measured as the work goes. Phase 2's native
readers (`.pac`, NIF, TGA, fonts) are still worth having, for tools and
the viewer, and as the start of native replacements later.

B1's order of work:

1. `.pac` reader and the viewer's images, fonts and puzzles (phases 1
   and 2).
2. Load `LEGOATeam.exe` and every DLL in the interpreter, with stand-ins
   for the libraries above. Then let `DDPackedDirFileLib`'s loader pull a
   behaviour DLL out of `Bhvr.pac`. `reluva-tool x86-run` shows how far it
   gets.
3. MSVCRT, MSVCP60, C++ exceptions and the extra thread, until the game
   reaches its first `DirectDrawCreateEx`.
4. DirectDraw/Direct3D 7 on `gfx::Renderer3D`: the menus first, then a
   puzzle.
5. DirectInput on `System` events, then A3D on the mixer.
6. Speed: an instruction cache, then native NiMain hot spots, measured
   against a puzzle's frame time.

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

Game files can't go in the repository. The whole install has been provided
for study: the executables, the DLLs and all eight archives.

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
