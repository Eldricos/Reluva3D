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
  - **Drawing state, as Direct3D's defaults:**
    - back faces culled, since nothing in the files asks for two-sided
      drawing and the stored normals agree with the triangles' winding;
    - depth written unless an `NiZBufferProperty` says otherwise;
    - shapes that blend drawn after the rest, the farthest first.

    Before this, blended closed shapes showed their insides: the
    skeletons' heads, whose skull texture blends, looked as if their
    normals were wrong.
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
4. DirectDraw/Direct3D 7: the menus first, then a puzzle. Done in
   software first (see below); `gfx::Renderer3D` can come later, for
   speed.
5. DirectInput on `System` events, then A3D on the mixer.
6. Speed: an instruction cache, then native NiMain hot spots, measured
   against a puzzle's frame time.

Progress:

- Done: `src/engines/netimmerse/alphateam/game_process.cpp` loads
  `LEGOATeam.exe` and the 9 DLLs it needs from the game folder, each after
  those it imports from: `DDPackedDirFileLib`, `NiMain` (relocated: its
  address is taken), `NiAnimation`, `BaseClassComp`, `BehaviorComp`,
  `NiA3dSound`, `NiCollision`, `NiDirect3d`, `LoadComp`, then the program.
- The process believes it's installed in
  `C:\Program Files\LEGO Media\Games\LEGO Alpha Team\`, which is the
  game folder. Writing files isn't allowed yet.
- The CD-ROM drive (D:) has the retail CD's label, `LEGOATEN`.
- `reluva-tool at-run <game folder> [-log]` runs all of it and reports how far it
  gets: the imports nothing stands in for, by DLL, and on a failure the
  last instructions (as module+offset) and the last stand-ins called.
- Done: MSVCRT (all 79 imports, `src/common/x86/msvcrt.cpp`) and KERNEL32's
  files.
- Done: MSVCP60, as our own DLL (below), and the rest of KERNEL32's
  imports (`src/common/x86/win32_system.cpp`: system information, the
  performance counter, critical sections and events for one thread, date
  and time text).
- Every DLL's start-up (static constructors and `DllMain`) now runs
  through: 130,057 instructions, NiMain's processor check included.
- Done: USER32 and GDI32 (`win32_user.cpp`), the registry (ADVAPI32) and
  COM start-up (ole32) (`win32_registry.cpp`). Everything the game imports
  is now stood in for except DirectDraw (3 imports) and DirectInput (1).
- What the program does before it needs DirectDraw (`at-run -log` lists
  every stand-in called):
  - checks the CD;
  - registers its window class and makes its window ("LEGO Alpha Team",
    sized with `AdjustWindowRect`), and shows it;
  - confines the pointer to it;
  - finds the `.pac` archives and maps each into memory;
  - asks the desktop's colour depth, then sets the window to 640×480 at
    80,60 (`at-run` reports the window);
  - calls `DirectDrawCreate` from NiDirect3d.
- Done: DirectDraw and Direct3D 7 (`src/common/x86/directx/`, below).
  The game sets its 640×480 16-bit fullscreen mode and makes its renderer:
  - a flipping primary surface with a back buffer;
  - a Z buffer;
  - the device, on the "HAL";
  - 288 multi-texture modes found by its 594 test draws.
  Then it shows its loading screen and plays its attract-mode demo: the
  levels, with lightmaps, water, transparency and animated minifigures,
  under the logo and "Click the mouse to begin!".
- Fixed on the way:
  - `VirtualAlloc` handed out addresses already in use. The behaviour
    DLLs ask for NiMain's and DDPackedDirFileLib's base, 0x10000000.
  - `LoadLibraryA` now loads DLLs from the game folder.
    `LegoLogicComp.dll` is needed only by behaviour DLLs.
  - Export names may be up to 4 KB; one of DDPackedDirFileLib's is 530
    characters.
  - Every DLL stood in for (DDRAW's, MSVCRT's...) has a module handle for
    `GetProcAddress`. NiDirect3d finds `DirectDrawCreateEx` that way.
- `at-run -shots <folder>` saves the frames shown (TGA). 3 billion
  instructions take about 100 seconds and show 74 frames, so the game runs
  at under one frame a second for now; speed is step 6.
- Done: input. Found from LoadComp's `DDMouse` and the program's window
  procedure, the game reads:
  - **the pointer by its movement:** each frame `DDMouse::GetState` takes
    `GetCursorPos` against where it last put the pointer, then puts it
    back in the window's middle with `SetCursorPos`. Its own pointer, drawn
    by the game, is that movement added up: from 0 to 1 across the screen
    from the bottom left, 1/640 a pixel times the sensitivity;
  - **clicks and keys as window messages:** `WM_LBUTTONDOWN` and the like
    go to `DDMouse::ProcessMouseClick`, which clicks where the game's
    pointer is, not where the message says. `WM_KEYDOWN` and the like go
    to `DDKeyboard::processKey`;
  - **DirectInput not at all:** `DDMouse::InitSystem` would make a
    DirectInput mouse, but only if a flag is set, and nothing sets it.
    DirectInput is stood in for all the same (below), for other games.
- In exclusive fullscreen, changing the display mode now makes it the
  desktop's size and puts the program's window over the whole screen, as
  DirectDraw does. The pointer and the window then agree.
- `at-run -click <frame>[@x,y]` clicks when a frame is shown (where the
  game's pointer is, without a place), and
  `-move <frame>@dx,dy` moves the pointer. A click on frame 20 leaves the
  demo for "Destination: Alpha Team Headquarters" and the main menu
  (Adventure, Single Missions, Options, Exit Game). Moving the game's
  pointer onto "Exit Game" and clicking brings up "Really Exit Game?".
  Everything the game imports is now stood in for.
- Into a mission, from the menus. The `at-run` options that help:
  - `-fixed-clock`: the clock follows the instruction count (40,000 to a
    millisecond) and the main thread's sleeps, so runs repeat exactly.
  - `-quit <frame>`; `-limit 0` for no instruction limit.
  - `-point <frame>@x,y`: puts the game's own pointer on the screen with
    `DDMouse::SetPosition`. Relative moves land unevenly, so this is the
    reliable way to aim.
  - `-click <frame>` clicks where the pointer is.
  - `-key <frame>@vk` presses a key.
  - `-watch dll!export`: each frame, the calls to a function of the
    program's, with their first arguments and what they returned.
  - Tracing (`-log`, `-trace`) only when asked, so the instruction cache
    stays in use.

  The route (`-fixed-clock`): click on frame 25 (past the demo); the main
  menu is up by frame 1500. Then Single Missions (`-point 1510@320,184
  -click 1512`), "Alpha Team Training Area" (`-point 1560@321,128 -click
  1562`) and the check mark (`-point 1600@134,315 -click 1602`), each
  where `-buttons` puts it. The mission loads (by frame 1800) and is
  drawn correctly: the camera flies over the board, and the goal text says
  to click to begin. A click (1900) starts the exercise: the text bar
  becomes the camera controls and the go button. (Frame numbers depend on
  how many instructions frames take: before the cycle counter's stand-in,
  below, the menu was up by frame 100 and the mission began at 420. The
  frames quoted in the investigation that follows are from then.)

  Then it goes no further: Dash stands still and the go button does
  nothing. Traced (with `-watch`, `-calls` and `-dump`, below), the cause
  is sound:
  - Input works. The stop button (bottom left) opens the in-game menu
    (Keep Playing, Start Over, Skip Training Mission...).
  - The go button (`GoStop`) is locked on purpose. Clicks reach
    `DDUIManager::ProcessMouseClick`, which only hits buttons in its 4x4
    grid of screen cells (`ScreenButtons`). A button is added there only
    once it's clickable (`DDUIButton::SetClickable`: enabled at `+0x50`,
    clickable at `+0x51`, rectangle at `+0x40..+0x4c`, 0..1 from the
    bottom left).
  - The begin click enters a sub-machine of the interface's behaviour (in
    `PE0300.DLL`) whose entry code makes go unclickable. Its exit code
    makes go clickable again. The transition out is triggered by event
    `0xea7d`, which a state's entry code sends.
  - In the Adventure, "Skip Training Mission" leads to Mission One. There
    that state is entered when the mission loads, go unlocks, and the
    mission plays: go starts Dash running, and the others move. So
    simulation, input and interface are fine.
  - The Training Area (M0010) is driven by voice lines: `Locale_en.pac`
    has `M0010_010`, `M0010_020A`/`B`, ... up to `110`. Each step waits for
    its line. But every `DDStreamingAudioManager::AddSource` returns null
    (no A3D, so `NiA3dSoundSystem::Startup` fails), including the two the
    begin click starts. So the first line never "finishes", and nothing
    ever ends the lock.
  - So the fix was the A3D stand-in (step 5): streaming sources that
    exist, play and end. With it, the begin click plays `M0010_010`
    (10.3 seconds) and then `M0010_020A` (1 second), each for its full
    length. The training then highlights a tile on the board and waits
    for the player. That needed real wave events: the streaming manager
    refills a source's buffer each time play passes the event at its
    middle (`DDStreamingAudioManager::UpdateBuffers`, `InitStreamData`).
    With every event read as set, it raced through the file, and the
    first line ended after about 3 seconds.
  - From there the training is a guided tour, and it now plays to the
    end. Each step plays a line, then waits for the player to do one
    thing. Played with the fixed clock and the route below, Dash runs on
    frame 3050, reaches the top of the tower, and the game returns to
    headquarters on frame 3200.
  - The tour is the interface behaviour's training machine (PE0300,
    behaviour `3c3140`, machine `3c3580`). A choice point picks one of
    four tour composites; M0010's is a chain of about 50 states. A step's
    state waits (`DDState::registerEvts`) for one event in
    `0xea60`..`0xea7d`, each sent only in training (the level's `Training`
    option, read by `DDSpace::GetOptionValue`) by the code for one player
    action. Interface buttons raise their own events first: a table in
    PE0300 gives each button's (`Menu` `c358`, `NextCam` `c38f`, `GoStop`
    `c350`, `RotateCW` `c35b`, ...), and the in-game interface machine
    (`3c3c40`, in state `3c3f08` while playing) turns them into the
    tour's. The steps, in order:

    | Step | Event | What the player does |
    |---|---|---|
    | 1 | `ea61` | click the trainer TV (`T0001`, "teevee", a unit); later it repeats the current line |
    | 2 | `ea62` | open the menu (stop button); sent once the menu is fully open |
    | 3 | `ea63` | Keep Playing |
    | 4, 5 | `ea64` | next camera (right arrow) |
    | 6 | `ea65` | previous camera (left arrow) |
    | 7 | `ea66` | Nav_Pivot |
    | 8, 9 | `ea67` | Nav_InOut |
    | 10–13 | `ea68`, `ea69`, `ea6a`, `ea6b` | scroll by holding the pointer at the left, right, top and bottom edges (not over a button) |
    | 14 | `ea6c` | go |
    | 15 | `ea6d` | a team member's button |
    | 16 | `ea6e` | ICam |
    | 17 | `ea6f` | SmartCam |
    | 18 | `ea70` | stop |
    | 19 | `ea71` | drag the highlighted direction tile onto the blue square |
    | 20 | `ea72` | select it (click it) |
    | 21, 22 | `ea73` | rotate it clockwise; the second step waits until it faces 90 degrees |
    | 23 | `ea74` | Info |
    | 24 | `ea72` | select it again |
    | 25 | `ea75` | close its panel (a click beside it) |
    | 26 | `ea6c` | go: Dash runs |
    | 27 | `5a` | Dash's run ends; the tour is over |

    Some steps are entered only after a long line (one of about 400
    frames, before step 14).
  - The go button unlocks at `0xea7d`. The in-game interface machine
    sends it when it enters state `3c3ee8` in training (its entry code,
    PE0300 `0x10052310`). The training machine leaves the tour on it. In
    Mission One that state is entered as the level loads, which is why go
    works there.
  - Reading a step: `-graph` dumps the machines (states, their
    transitions, the events each waits for, the targets); `-watch` on
    `registerEvent` shows the steps as they're entered and on
    `??0DDEvent@@QAE@H@Z` the events raised. The sender of an event is
    found in PE0300 (`push $0xea..`), and its vtable says which state or
    transition it belongs to. PE0300 is mapped at `0x730000` in these
    runs, so its code can be watched by address.
  - The route, as `at-run` options after the menu route above (through
    `-click 1900`):

    ```
    -step ea61+10@click:193,92  -step ea62@click:65,435  -step ea63+20@click:320,98
    -step ea64@click:412,428  -step ea64@click:412,428  -step ea65@click:214,428
    -step ea66@click:348,428  -step ea67@click:282,425  -step ea67@click:282,425
    -step ea68@point:4,240  -step ea69@point:636,240  -step ea6a@point:320,3
    -step ea6b@point:320,477  -step ea6c@click:572,434  -step ea6d+10@click:419,428
    -step ea6e+10@click:470,428  -step ea6f+10@click:534,433  -step ea70+10@click:572,434
    -step ea71+10@drag:488,262,405,215  -step ea72+10@click:405,215
    -step ea73+10@click:389,140  -step ea73+10@click:389,140*4
    -step ea74+10@click:428,140  -step ea72+10@click:405,215
    -step ea75+10@click:432,182  -step ea6c+10@click:572,434
    ```

  The tracing options:
  - `-watch` takes `dll!export` or a raw `0x` address (for code in the
    behaviour DLLs, which `DDPackedDirFileLib` maps itself). For each
    frame it shows the calls, `this` (ECX), the first arguments (as text
    where they point to some), the caller, EAX and the x87 top of stack.
  - `-calls <frame>@<function>` prints the tree of calls one call makes.
    Functions are named as module!nearest-export+offset; for DLLs the
    program mapped itself, from their headers in memory.
  - `-dump <frame>@<address>,<bytes>` prints memory as words and floats.
  - `-graph <frame>@<node>[,<node>...][,<lo>-<hi>]` walks behaviour
    state machines through BehaviorComp's virtual functions, from the
    nodes given and every node found between `lo` and `hi`.
  - `-buttons <frame>` lists LoadComp's interface buttons: name, whether
    enabled and clickable, and where on the screen.
  - `-step <event>[+<delay>]@<action>` acts when a state starts waiting
    for the event (`DDBehavior::registerEvent`): `click:x,y` (with `*n`,
    clicked again every 40 frames until the training moves on),
    `point:x,y`, `key:vk`, `drag:x,y,x2,y2`, or `buttons`.
- Done: the engine plays the game (`alphateam/player.{h,cpp}`).
  - The program's main loop never returns, so it runs on a thread of its
    own. The engine's thread queues the player's input and takes the
    frames the program shows.
  - The program's thread hands the input to the process at each frame,
    and whenever the program looks for a message and finds none.
  - The mouse is held by the window, and its movement is passed on.
  - F12 pauses the game at its next frame and opens the data viewer.
  - Quitting stops the program with `Cpu::requestStop`; an `exit` of its
    own is told from a failure by `Win32Process::exited`.
- Done: sound, through the A3D stand-in (`src/common/x86/a3d.cpp`,
  docs/engines/x86.md). `NiA3dSoundSystem::Startup` now succeeds, and
  `DDStreamingAudioManager::AddSource` returns sources. The streaming
  thread fills them as their play positions move, and the engine hears
  them through its mixer.
- Speed, measured headless (frames given to the engine, a click past the
  demo): the menu at headquarters runs at about 14 frames a second on 4
  cores, after about 22 seconds of loading.
  - Inlining instruction execution (memory access, fetching, operands)
    and the rasterizer's per-draw set-up got it there from about half
    that.
  - The decoded-instruction cache (x86.md) cut the time to reach the menu
    from about 41 seconds to 28.
  - The rasterizer then took a third or so of the time at the menu. The
    rasterizer section below has what was done; drawing 120 frames went
    from 7.4 seconds to 2.0. The interpreter is now most of the time.
  - Checking the speed-ups: with the clock driven by the instruction count
    and the click at a set frame, runs repeat exactly. Frames saved by the
    game's own frame number came out byte-identical before and after each
    change. The engine's frame count doesn't do for this: which game frame
    it shows depends on timing.
  - Then, measured in the training mission (600 frames from the start, a
    run that's two thirds loading and the menus): 42.0 seconds.
    - Blocks in the instruction cache (x86.md): 29.6 seconds.
    - Drawing on threads of their own, in order, while the program goes on
      (the rasterizer section below): 25.5 seconds.
  - Then, where the interpreter's time went while playing, sampled by
    the program's own addresses: over a quarter of it was in
    `DDCycleCounter::Start` and `Stop` (DDPackedDirFileLib). They time the
    program's work with RDTSC around a great many of its functions, for a
    profile only the interface behaviour reads (`Get`, `Zero`; PE0300
    `0x10065940`, which shows it only when a flag at `+0x31` is set).
    `GameProcess` makes them return at once (both cdecl, void); the profile
    then reads zero. Mission frames went from 32 a second to 42 (780
    frames, 24.3 seconds to 18.5).
    - With fewer instructions to a frame, a fixed clock moves less in a
      frame: the game's timed waits (the loading screen before
      headquarters) take more frames, so the routes' frame numbers changed.
      The fixed clock now also counts the main thread's `Sleep`s, as the
      time that would have passed.
  - Loading reads the archives a byte at a time through our MSVCP60's
    `istream::get` (a quarter of the instructions while loading): a
    buffered `filebuf` is next.
- Not there yet:
  - **more speed**: the interpreter is still most of the time while
    playing (NetImmerse's scene graph, picking and its Direct3D renderer).

#### DirectDraw and Direct3D 7

`src/common/x86/com.{h,cpp}` makes COM objects programs can call. Each
interface is a vtable of native functions in the program's memory,
dispatching to a C++ object. `directx/` has the objects, written from the
DirectX 7 documentation. Interfaces, method orders and IIDs are the SDK
headers' (`directx/interfaces.cpp`).

- **DirectDraw:**
  - one adapter with 3D;
  - the modes `DirectX::modes` lists;
  - surfaces in the program's memory, as programs lock them and write to
    them;
  - blits (stretching, colour keys, colour and depth fills) and flipping
    chains;
  - palettes and clippers.

  What reaches the primary surface becomes `DirectX::frame()`, handed to
  the engine. In a window, that's the window's client area.
- **Direct3D 7:** RGB emulation and HAL devices, vertex buffers and state
  blocks. The drawing is a software rasterizer (`directx/raster.cpp`):
  - the fixed-function pipeline: transforms, lighting, materials and their
    vertex-colour sources, and texture coordinate generation and
    transforms;
  - clipping to the view volume, and culling by winding;
  - pixel centres at whole coordinates, with a top-left fill rule;
  - perspective-correct interpolation;
  - up to 8 texture stages with all the D3D7 operations from DISABLE to
    BLENDCURRENTALPHA, mip levels and bilinear filtering;
  - colour keys, the alpha test, vertex and table fog, all the blend
    factors, and the depth test into any Z format.

  How it's kept quick, each change giving the same pixels:
  - A draw's triangles are drawn by rows on as many threads as there are
    processors (`RowThreads`). Each thread takes groups of 8 rows, dealt
    round, and draws every triangle there in order. Small draws stay on
    one thread. `DirectX::drawThreads` can choose one thread, or threads
    always, which the test uses to check both give the same image.
  - Each row is filled from its first inside pixel to its last; the edge
    tests are only made at the ends.
  - The depth test comes before a pixel's attributes are worked out.
  - Samplers are set up per triangle: the mip level, and power-of-two
    wrapping by mask. Bilinear sampling indexes directly when all four
    texels are inside.
  - Stages that only select or modulate plain arguments (all of this
    game's) are combined from sources picked once per draw. The general
    path handles the rest.
  - Targets, depth buffers and textures are read and converted through
    tables (`ArgbConverter`), also for presenting a frame.

  Software first, rather than `gfx::Renderer3D`: the game locks and blits
  surfaces as well as drawing into them, and doing both in one place keeps
  them consistent. It also lets the frames be checked without a GPU.
- `testDirectX` in `tests/test_common.cpp` checks these, called through
  the vtables: a clear, a Gouraud triangle, the depth test, a textured
  quad with point sampling, and a flip.

- Registry keys the game's code names, for later:
  - `HKEY_CLASSES_ROOT\A3d` and the A3D COM class
    `{d8f1eee0-f634-11cf-8700-00a0245d918b}`, which the program registers
    itself;
  - `ALPHABLENDMETHOD` under `HKEY_LOCAL_MACHINE\Software\NVIDIA
    Corporation\RIVA 128\Direct3D`, a driver setting it writes;
  - the installed locales, which LoadComp enumerates;
  - `Software\LEGO Media\LEGO Alpha Team\Version`, read by a behaviour
    DLL, with `DiscPath`, `SoundVolume` and similar names nearby.

#### MSVCP60

The game's code derives from the C++ library's classes, builds them
inline and reads their fields, so stand-ins written natively (as MSVCRT's
are) won't do: the objects have to be laid out as Visual C++ 6's were.
`src/common/x86/runtime/msvcp60.cpp` is a replacement written here, each
function doing what the C++ standard says. `build_msvcp60.sh` builds it
with clang (`i686-pc-windows-msvc`, freestanding, importing only MSVCRT)
into a DLL embedded in Reluva and loaded before the game's modules.
`msvcp60.def` maps each of the 167 names the game imports to its function.
clang was checked to give the same names for the same declarations, but
plain structs at measured offsets plus the `.def` keep the layouts under
our control. The tests call it as the game does (`testMsvcp60`).

Measured from the game's code (its inline constructors, the offsets it
reads, the vftables it builds for its own subclasses):

| Class | Size | What the game touches |
|---|---|---|
| `string` | 16 | allocator byte, characters +4, length +8, capacity +12; a sharing count in the byte before the characters (255: frozen); calls `_Grow`, `_Copy`, `_Eos`, `_Split`, `_Freeze`, `_Refcnt`, `_Tidy` |
| `ios_base` | 0x28 | state +4, exceptions +8, flags +0xc, precision +0x10, width +0x14, the locale pointer +0x20, the standard-stream index +0x24 (set to 0 inline) |
| `basic_ios` | 0x34 | stream buffer +0x28, tied stream +0x2c, fill +0x30 (set to `' '` inline) |
| `basic_streambuf` | 0x38 | get/put starts +4/+8; pointers to where the starts, next characters (+0x14, +0x18) and counts (+0x24, +0x28) are kept (+0xc, +0x10, +0x1c, +0x20, +0x2c, +0x30); locale +0x34. The get count is from the next character |
| `basic_filebuf` | 0x54 | its own locale +0x4c, the `FILE*` +0x50 |
| `strstreambuf` | 0x50 | built inline by NiMain, then `_Init(n, get, put, mode)` |
| `istream`, `ostream` | 8, 4 + the ios | the vbtable pointer (the ios's offset is its second entry), the read count +4 |
| `ifstream`, `ofstream`, `fstream` | ios at +0x5c, +0x58, +0x60 | filebuf at +8, +4, +0xc |
| `istrstream`, `ostrstream` | ios at +0x58, +0x54 | |
| `ctype<char>` | 0x18 | built inline by LoadComp and a behaviour DLL: the reference count +4, `_Getctype`'s 16 bytes copied to +8, the table +0x10 |
| `fpos<int>` | 24 | offset, a 64-bit position at +8, state +16 |

- Destructors and the vftable entries of the streams take `this` at the
  ios. Constructors take a hidden last argument, whether to build the
  ios.
- Exported vftables, in order:
  - `ctype<char>`: destructor, `do_tolower` (range), `do_tolower` (char),
    `do_toupper` (range), `do_toupper` (char);
  - `basic_streambuf`: destructor, `overflow`, `pbackfail`, `showmanyc`,
    `underflow`, `uflow`, `xsgetn`, `xsputn`, `seekoff`, `seekpos`,
    `setbuf`, `sync`, `imbue`.
- Open modes: in 1, out 2, ate 4, app 8, trunc 0x10, binary 0x20. State
  bits: eof 1, fail 2, bad 4.
- File buffers read and write the `FILE` directly and keep no characters
  of their own, so inline `sgetc` and similar always reach the virtual
  functions.
- Only the "C" locale, with `ctype<char>` its one facet.
- `_Lockit` does nothing while the process runs one thread.
- What would throw (`_Xlen`, `_Xran`, a stream's exception mask) prints why
  and aborts, as exceptions aren't dispatched yet.

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
