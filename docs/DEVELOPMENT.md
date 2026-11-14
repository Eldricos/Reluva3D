# Working on Reluva

Notes for developers. Building and running are covered in the
[main README](../README.md#building); this page has the rest.

## Build folders

- **Configure once per folder.** A build folder keeps the configuration it
  started with. For a debug build, use another folder:
  `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
- **Run from the top of the repository**, with the folder in the path
  (`./build/reluva` on Linux and macOS, `build\Release\reluva.exe` with
  Visual Studio's generator). There's no need to `cd build`.
- **After pulling,** `git pull && cmake --build build` rebuilds what changed.
- **Release builds for players** turn the developer keys off:
  `-DRELUVA_DEVELOPER_KEYS=OFF` leaves out the games' debug keys and the
  launcher's *Developer:* settings (`src/common/developer.h`). They're on
  by default, for working on the engines and for the tests. The data
  viewer is never in `reluva` either way ([below](#the-data-viewer)).
- **SDL and Dear ImGui** are fetched and built by CMake. The first configure
  needs internet access and takes a few minutes; later ones reuse them.
  `-DRELUVA_USE_SYSTEM_SDL=ON` uses an installed SDL3 instead, found through
  its CMake package (e.g. via `CMAKE_PREFIX_PATH`).

## Programs

| Program | What it's for |
|---|---|
| `reluva` | The launcher and games: what players get, with no developer tools |
| `reluva-viewer` | The engines' data viewers and the testbed ([below](#the-data-viewer)) |
| `reluva-tool` | Developer commands for studying game data and running game code outside the window ([below](#reluva-tool)) |
| `reluva-render-check` | The 3D renderer's check against reference frames (below) |
| `reluva-tests` | Every test that needs no game files: the shared code's and each enabled engine's (below) |

## The data viewer

- **Kept out of the game.** Each engine's viewer is a library of its own,
  `reluva_engine_<engine>_viewers`, which `reluva-viewer`, `reluva-tool`
  and `reluva-tests` link and `reluva` doesn't: so nothing a player runs
  can open it, and the game's code can't come to rely on it.
- **Adding one.** Put the viewer's pages in the engine's `pages/` folder,
  and declare them in its `CMakeLists.txt`:

  ```cmake
  reluva_add_engine_viewers(clue
      ACCESSOR clueMetaViewer
      SOURCES
          pages/viewer.cpp
          pages/image_viewer.cpp)
  ```

  `clueMetaViewer()` returns the engine's `MetaViewer`
  (`src/engines/viewer.h`), whose `create` makes an `Engine` that runs
  the viewer as a game is run. The game's own code never includes a
  viewer's header.
- **Running it.** `reluva-viewer` shows the launcher's games, each opened
  in its engine's viewer; `reluva-viewer <name>` or
  `reluva-viewer <folder>` opens one directly.

## reluva-tool

- **Using it.** Commands for any game are run as
  `reluva-tool <command> ...`, an engine's as
  `reluva-tool <engine> <command> ...` (`reluva-tool idtech3 bsp-check
  <game folder>`).
  - `reluva-tool` alone lists the commands for any game, and the engines;
  - `reluva-tool help <engine>` lists an engine's commands, with what each
    does;
  - `reluva-tool help [engine] <command>` shows one.
- **Where the commands are.** The ones for any game (bytes, Windows DLLs,
  detecting and importing games, AVI movies and CD images' audio tracks)
  are in `src/devtools/`. Each engine's are in
  its `tools/` folder, by topic (`src/engines/idtech3/tools/qvm.cpp`), and
  are built only with the engine.
- **Adding one.** Write it as `int cmdName(int argc, char **argv)` (its
  name is `argv[1]`, its arguments start at `argv[2]`, with or without the
  engine's name before it), and add it to the table at the end of its
  file, with its arguments and a description:

  ```cpp
  const Commands commands("idtech3", {
      {"bsp-check", "<game folder>", "load every maps/*.bsp of a game folder", cmdBspCheck},
  });
  ```

  A new file goes in the engine's `reluva_add_engine_tools` list, in its
  `CMakeLists.txt`. Helpers every command can use (`readFile`, `rd32`,
  `hexdump`) are in `src/devtools/tool.h`.

## Code style

Code follows [CODING_GUIDE.md](CODING_GUIDE.md); how the existing code
was brought in line is in [code-style-migration.md](code-style-migration.md).
Five tools check it; the first three need version 18 of their LLVM
program (another lays code out differently):

- **`python3 tools/format.py`** formats with clang-format
  (`.clang-format`): every file, the ones named
  (`tools/format.py src/engines/clue`), or `--changed` (those changed since
  the branch left `main`). `--check` lists what needs formatting instead.
- **`python3 tools/tidy.py`** checks with clang-tidy (`.clang-tidy`: names,
  `nullptr`, `override`, const-correctness...): every file, the ones named,
  or `--changed`. It makes a compile database in `build-tidy/` with every
  engine enabled. `--fix --checks=-*,<check>` applies one check's fixes;
  `--rename-declared-in <folder>` renames what that folder declares to the
  guide's names, with every use (`--dry-run` lists them first).
- **`python3 tools/split_members.py`** lists class and struct members
  declared several to a line (`int x, y;`), which clang-tidy splits only
  for local variables; `--fix` splits them. Reads `tidy.py`'s compile
  database (run `tidy.py` first) with libclang's Python bindings
  (Ubuntu: `apt install python3-clang-18`; else
  `pip install clang==18.1.8`).

- **`python3 tools/check-headers.py`** lists source files without the
  license and copyright lines every file starts with; `--fix` adds them.

- **`python3 tools/comments.py`** flags comments that break the guide's
  Comments rules in ways a script can be sure of: commented-out code,
  history ("Fixed: …", "previously"), Doxygen tags that say only the
  name, and a bare `TODO`. Whether a comment only repeats the code is for
  the author and reviewer to judge. With `--changed` it looks only at the
  lines added.

Before a pull request, run `format.py`, `tidy.py`, `split_members.py` and
`comments.py` with `--changed`, and `check-headers.py`, which looks at
every file. CI's "Code style" job does the same, and fails on an
unformatted file, a clang-tidy finding or members declared together in a
changed file, or a file without its license lines; what `comments.py`
flags shows as a warning on the pull request's lines, without failing it.
A name that has to stay as it is (see the guide's Naming: Exceptions)
gets a `// NOLINT(<check>): reason`, or `// NOLINTNEXTLINE(<check>): reason`
on the line before when the reason is long. Generated files and `third_party/`
are never formatted (`.clang-format-ignore`). Commits that only reformat
or rename go in `.git-blame-ignore-revs`; run
`git config blame.ignoreRevsFile .git-blame-ignore-revs` once, and
`git blame` passes over them.

## Tests

- **Names.** Tests come in suites named `<area>.<kind>.<topic>`: the area is
  `common` (the shared code) or an engine, the kind is `unit`, `engine` or
  `gamedata`. For example `common.unit.audio`, `idtech3.unit.collision`,
  `idtech3.engine` (the basic engine test, below) and
  `idtech3.gamedata.maps`. ctest uses the same names.
- **Running them:**
  - `ctest --test-dir build --output-on-failure` runs them all;
  - with Visual Studio's generator, add `-C Release` (or `Debug`);
  - `ctest -L <label>` runs some: a kind (`unit`, `engine`, `gamedata`) or an
    area (`common`, `idtech3`);
  - `reluva-tests` runs them directly: alone, every suite; with names, those
    suites, or every suite under a name (`reluva-tests idtech3`,
    `reluva-tests idtech3.unit`); `--list` lists the suites and their cases;
  - in Visual Studio itself, pick `reluva-tests.exe` as the startup item, or
    use **Test > Test Explorer**.
- **No game data needed,** but for the game-files tests ([below](#engine-tests)),
  which are skipped unless pointed at a game. The tests use their own data:
  files built in code, and small test programs compiled to the games'
  formats (`tests/common/x86/`, `tests/engines/idtech3/data/qvm/`). Their
  `build.sh` scripts remake them, which is only needed after changing their
  sources.
- **Where they are:**

  ```
  tests/support/           the framework (test.h) and reluva-tests' main
  tests/common/            the shared code's tests and their data
  tests/engines/<engine>/  each engine's: unit/, engine.cpp, gamedata.cmake, data/
  tests/render/            the render check's reference frames
  ```

- **Writing one.** A test file registers its suite, with its cases
  (`tests/support/test.h` shows how), and `CHECK(condition)` checks.
  Each case gets an empty folder of its own. The engine's `CMakeLists.txt`
  lists the file in `reluva_add_engine_tests` (`reluva_add_tests` for
  `common`), and the suite's topic in `TOPICS`, so ctest runs it. The
  `tests.coverage` test fails if a suite is missing from `TOPICS`.
- **From a window.** `reluva-viewer`'s **Tests...** button opens a
  window of the build's tests, the game-files steps beside the games in
  the launcher's list (see [below](#the-tests-window)).
- **CI.** The workflow (`.github/workflows/ci.yml`) builds and runs the
  tests on Linux, macOS and Windows for every push and pull request (macOS
  is off for now: its job has `if: false`, as no macOS runner is available
  to the repository; remove that line to turn it back on). It
  also builds each engine alone, every other disabled, so no engine comes
  to rely on another's code. To try that locally, point
  `-DRELUVA_ENGINES_FILE` at a list of your own, or use
  `engines.local.cfg`.

## Engine tests

Each engine can have two engine tests, besides its unit tests. They test the
engine as a whole, so a change anywhere (in `src/common/`, say) that alters
what an engine does is caught, even where no unit test looks. Elite Force
(`idtech3`) has both, as do Harbinger (`stharbinger`, see
[its TESTING.md](engines/stharbinger/TESTING.md)), Murder on the Orient
Express (`awe`, `RELUVA_MOTOE`, see [its TESTING.md](engines/awe/games/motoe/TESTING.md)),
LEGO Alpha Team (`netimmerse`, `RELUVA_ALPHATEAM`, see
[its TESTING.md](engines/netimmerse/games/legoalphateam/TESTING.md)), and
Need for Speed III (`nfs`, `RELUVA_NFS3`, see
[its TESTING.md](engines/nfs/games/nfs3/TESTING.md)).

### The basic engine test: no game files

- **What it does.** It builds a small install of its own: paks, a map, a
  model, shaders, and test programs in place of the game's code. Then it
  plays it through as the game would be played, and writes everything that
  happens into a record: counts, and checksums of states, snapshots and
  scenes. It passes when the record matches the committed one.
- **Who runs it.** Anyone, and CI on Linux, macOS and Windows. `ctest`
  runs it with the rest, or alone with `ctest -L engine`.
- **Elite Force's.** `tests/engines/idtech3/engine.cpp` (`idtech3.engine`),
  with its record in `tests/engines/idtech3/data/engine-record.txt`. It covers:
  - the game folder mounted, with paks and a loose file overriding each
    other, and detected;
  - shaders, a map and a model read;
  - collision traces across the map, directly and through the game code;
  - a level on the server, with a player joining over the network
    (losing a datagram in four), moving, `map_restart`, and leaving;
  - the client game taking its snapshots and drawing scenes;
  - the menus drawing and taking keys.
- **When it fails.** It prints each line of the record, with the old value
  beside any that changed. If the change is meant, run
  `reluva-tests idtech3.engine --update`, review the record's diff, and
  commit it with the change.

### The game-files test: with your own copy of the game

- **What it does.** It runs the engine on the real game, through
  `reluva-tool`'s commands: every map, shader, model and image read; the
  game's own code run on several maps; saving and loading; the menus; the
  videos. Each command's output (paths and timings taken out) is
  checksummed, with a summary line, into a record. A step passes when its
  record matches the one committed for that release.
- **Who runs it.** Developers who have the game. Point an environment
  variable at the game's folder, then run the steps (labelled `gamedata`):

  ```sh
  RELUVA_STVEF=/path/to/EliteForce ctest --test-dir build -L gamedata
  ```

  On Windows: `set RELUVA_STVEF=C:\Games\EliteForce`, then
  `ctest --test-dir build -C Release -L gamedata`.
- **Or from the launcher's list.** Without the variable, a step takes the
  game's folder from the launcher's entry for it (`reluva.ini`), so a game
  added in the launcher needs nothing set: `ctest --test-dir build -L
  gamedata` runs every game you've added. Where there are several entries
  for one game (two releases, say), name the one to use with
  `RELUVA_GAMEDATA_TARGET=<its name>`; `RELUVA_CONFIG` points at another
  `reluva.ini`. With neither the variable nor an entry the steps show as
  skipped, so a plain `ctest` stays green for everyone.
- **Elite Force's steps** (`tests/engines/idtech3/gamedata.cmake`, ctest
  `idtech3.gamedata.<step>`):

  | Step | What it runs |
  |---|---|
  | `release` | What the game is: 1.0, 1.2, with the Expansion Pack |
  | `maps` | Every map read |
  | `shaders` | Every shader script read |
  | `models` | Every model and skin read |
  | `images` | The images every map's shaders name |
  | `holomatch` | Holomatch's game code on its maps: the server, bots, the client game |
  | `menus` | Holomatch's menus, and single player's to its main menu |
  | `single_player` | Single player's game code on three maps, then saved and loaded |
  | `expansion` | The Expansion Pack's: Virtual Voyager and two holodeck missions, saved and loaded |
  | `cinematics` | The videos' pictures and sound decoded |

  Game code and data are paired as the launcher pairs them: 1.2's code
  without the Expansion Pack's pak, and the Expansion's with it.
- **Records.** They are kept in `tests/engines/idtech3/data/gamedata/<release>/`,
  one file per step. There's a folder for each of 1.0, 1.2, 1.2 with the
  Expansion Pack imported (both versions' code kept), and an install made
  with the Expansion (only its code). The records hold only checksums,
  counts and summary lines, none of the game's own content.
- **When a step fails.**
  - It prints the recorded and the new lines.
  - Each command's full output is kept in the build, under
    `gamedata/idtech3/<step>/`, with the previous run's beside it
    (`.previous.txt`). `diff` shows exactly what changed.
  - If the change is meant, run again with `RELUVA_GAMEDATA_UPDATE=1`,
    review the records' diff, and commit them.
- **On Windows and macOS.** The committed records were made on Linux.
  When the game's code calls `sin`, `cos` and the like, Reluva uses the
  system's maths library, which can differ in the last bit between
  systems. So steps that run game code can differ from the records on
  another system with nothing broken. Keep a baseline of your own before
  making changes:

  ```sh
  RELUVA_STVEF=... RELUVA_GAMEDATA_RECORDS=~/ef-records RELUVA_GAMEDATA_UPDATE=1 ctest -L gamedata
  ```

  Afterwards, run with `RELUVA_GAMEDATA_RECORDS=~/ef-records` (and without
  `UPDATE`) to compare against it. For several engines at once,
  `RELUVA_GAMEDATA_RECORDS_ROOT=~/records` keeps each engine's in a folder
  of its own (`~/records/idtech3/`).
- **Repeatable runs.** The game code reads a clock that moves only with the
  game (`reluva-tool idtech3 sp-run -fixedclock`). Floating point is the same on
  every run.

### The Tests window

`reluva-viewer`, run from its build folder, has a **Tests...** button: a
window that runs the build's tests through `ctest`, as the command line
does, with the game-files steps on the games in the launcher's list.

- **Picking.** Tick games, then the engines' game-files steps to run on
  them, and any of the tests that need no game files (by area, or one by
  one), then **Run Selected**. **Run All** runs every test, and every
  step on every game in the list. **Test Game**, beside the list, runs
  every step on the selected game.
- **Records.** What the steps compare with: the committed records; a
  baseline of your own (`RELUVA_GAMEDATA_RECORDS_ROOT`, a folder per
  engine, in the launcher's folder by default), compared with or made
  anew; or this run's written over the committed ones, which asks first.
  A baseline made from the command line with `RELUVA_GAMEDATA_RECORDS`
  (one engine's records in one folder) isn't found by the window, which
  looks in a folder per engine: make each engine's again in the window,
  with **Make my baseline**, before the changes it's to check. Each
  engine's `TESTING.md` says so too.
- **Two releases of one game** (two entries for it) run one after the
  other, as each step reads the game's folder from one variable.
- **Results.** Each test's result and time as it ends; a failure's
  output when it's picked, and for a game-files step a button opening
  its output folder (`build/gamedata/<engine>/`, the previous run's
  beside it). **Stop** asks `ctest` to stop, then makes it.
- **How it knows them.** The build writes its tests to
  `build/reluva-tests.txt` (`reluva_finish_tests`). The window plans the
  `ctest` runs (`src/devtools/test_runs.h`, tested as
  `devtools.unit.test_runs`) and runs them on a thread of its own
  (`src/devtools/ctest_runner.h`), so a run goes on while a game or a
  viewer is open. A copy of `reluva-viewer` outside its build folder has
  no tests, and says so.

### Adding them to a new engine

An engine started from the template (`cmake/NewEngine.cmake`, see
[ADDING_AN_ENGINE.md](ADDING_AN_ENGINE.md)) has a basic engine test
already. Then:

1. **The basic test** (`tests/engines/<engine>/engine.cpp`). Add steps to
   it as the engine grows: make the install, take the engine through what
   playing does, and add what happens to the record. `test::Record` and
   `test::Hash` (`tests/support/record.h`) keep the lines and the
   checksums; `Record::check` compares the record with the committed one,
   or writes it with `--update`.
2. **The game-files test.** Write `tests/engines/<engine>/gamedata.cmake`,
   which runs one step (`STEP`) of `reluva-tool <engine>` commands on the
   game. The shared helpers (`cmake/ReluvaGamedata.cmake`) do the rest:

   ```cmake
   include("${HELPERS}")
   gamedata_begin("the one holding <its data folder>")   # game_dir, or skipped
   # ... work out the release (reluva-tool detect), then the step:
   if(STEP STREQUAL "maps")
       record_run(maps "maps read" <engine> map-check "${game_dir}")
   endif()
   gamedata_end("${release}" "${release_folder}")         # compare or write
   ```

   `record_run` runs a command and records a checksum of its output (and a
   summary line); `record_file` records a file a command wrote. Register
   its steps (the template's `CMakeLists.txt` has this, commented out):

   ```cmake
   reluva_add_gamedata_test(<engine> GAME <game id> ENV RELUVA_<GAME>
       SCRIPT ${tests}/gamedata.cmake
       STEPS release maps ...)
   ```
3. **Make runs repeatable.** Any clock or random seed the game's code sees
   must be fixed for the tests (as `-fixedclock` does), or the records
   won't repeat.
4. **Make the first records.** Run with `--update` or
   `RELUVA_GAMEDATA_UPDATE=1`, and check that a second run passes. Then
   prove the test catches something: change a constant in code the engine
   uses, and see a step fail.

## The render check

`reluva-render-check tests/render` draws two test frames through the 3D
renderer and compares them with the reference frames there:

- **The frames.** The first is a grid of cells, each trying one feature:
  textures and filtering, vertex colours, blending, the alpha test, depth,
  culling, lighting, mipmaps, texture-coordinate transforms, the stencil and
  vertex deforms. The second tries the brightness pass.
- **The comparison.** Probe points must match within a tolerance, and the
  whole frame must match in 4×4 blocks, but for a few at edges, which GPUs
  rasterize slightly differently.
- **In CI** it runs on Linux (Vulkan, on Mesa's software driver), and on
  macOS (Metal) when that job is on (it's off for now: see Tests, CI).
- **On Linux without a display,** run it under `xvfb-run`.
- **`--out <folder>`** keeps the frames drawn, and where they differ.
- **After a change meant to alter the frames,** `--update` writes new
  references. Look at them, then commit them.

## 3D rendering and shaders

- **Graphics APIs.** 3D uses SDL's GPU API: Vulkan on Linux and Windows,
  Metal on macOS. Nothing extra is needed to build it.
- **Without a driver.** If the graphics driver has no Vulkan, Reluva falls
  back to SDL's usual renderer: 2D still works, and 3D pages say 3D isn't
  available. Setting `SDL_RENDER_DRIVER` to another renderer (e.g.
  `opengl`) forces that fallback.
- **Shaders.** They are compiled into
  `src/backends/graphics/sdl3/shaders/compiled.h`, which is committed. Only after
  changing `draw.vert` or `draw.frag`, run
  `src/backends/graphics/sdl3/shaders/build.sh`. It needs `glslang-tools` and
  `spirv-cross`.

## Cursors

- **A game's own pointer** (`System::setMouseCursor`) becomes the system's
  pointer, made again whenever the picture's scale changes (the window
  resized, full screen, another display), with a second, sharper picture
  for high-density screens.
- **Where a system can't show it** (too large, say), the backend draws it
  over the picture itself. `RELUVA_SOFTWARE_CURSOR=1` forces that, to try
  it. The testbed shows both: press C in it.

## Floating point

The build turns off fused multiply-add (`-ffp-contract=off` with GCC and
Clang). Fusing rounds differently, so game code would give different results
on different machines: collision, movement, and what a networked game agrees
on. Keep it off.

## IDEs

- **VS Code** opens the folder directly, with the *CMake Tools* and *C/C++*
  extensions. CLion and Qt Creator open `CMakeLists.txt` directly.
- **Visual Studio.** To pass command-line arguments to `reluva.exe` (e.g. to
  start a game directly, by its name in the launcher's list) or to
  `reluva-viewer.exe` (a game in its viewer, or the testbed):
  1. In Solution Explorer, right-click `CMakeLists.txt` and choose **Add
     Debug Configuration** (or **Debug and Launch Settings**), then
     **reluva.exe** or **reluva-viewer.exe**.
  2. In the `launch.vs.json` that opens, add an `args` line to that
     program's entry; for `reluva-viewer.exe`, the testbed:

     ```json
     "args": [ "testbed" ]
     ```
  3. Environment variables (e.g. `RELUVA_PERF=1`, with which the AWE
     engine writes where each frame's time goes) go in the same entry:
     `"env": { "RELUVA_PERF": "1" }`. From a terminal, set them before
     starting `reluva.exe` there: `set RELUVA_PERF=1` in a Command Prompt,
     `$env:RELUVA_PERF = "1"` in PowerShell (where `set` makes a
     PowerShell variable the program never sees).

## Troubleshooting

- **The first configure fails while downloading.** CMake fetches SDL and
  ImGui from GitHub, so it needs internet access the first time.
- **"Does not match the generator used previously".** The build folder was
  configured with another generator. Delete it, or use a new folder name.
- **Linux: "Couldn't find dependency package for ...".** One of SDL's `-dev`
  packages is missing; the error names the feature, e.g. `XCURSOR` →
  `libxcursor-dev`.
- **Linux: no sound or no Wayland.** At the end of the configure step, CMake
  prints a summary of SDL's features. `SDL_X11`, `SDL_WAYLAND`, and at least
  one of `SDL_PIPEWIRE`, `SDL_PULSEAUDIO` or `SDL_ALSA` should say `ON`. If
  one says `OFF`, install its `-dev` package, delete the build folder and
  configure again.
- **Strange errors after changing dependencies or compilers.** Delete the
  build folder (in Visual Studio: **Project > Delete Cache and
  Reconfigure**) and configure again.
- **Visual Studio: many LNK2019 "unresolved external symbol" errors for
  `reluva::x86::...` or `reluva::video::...`.** The files defining them
  didn't compile, and the linker only reports what's missing. Look for the
  first `error C....` above the link errors in the **Output** window
  (**Show output from: Build**). Error C1128 ("number of sections exceeded
  object file format limit") needs `/bigobj`, which the CMake files pass
  to Visual Studio; after updating, use **Project > Delete Cache and
  Reconfigure**.
- **Visual Studio: games running Windows code (LEGO Alpha Team, Elite
  Force's single player) are slow.** Use the **x64-Release**
  configuration, not x64-Debug. It's CMake's RelWithDebInfo, whose
  defaults (`/Ob1`, `/INCREMENTAL`) suit debugging; the CMake files change
  those for Visual Studio to full inlining (`/Ob2`) and plain linking
  (`/INCREMENTAL:NO /OPT:REF /OPT:ICF`), keeping the debug information.
  After updating, use **Project > Delete Cache and Reconfigure** so the
  new flags are picked up. `RELUVA_NO_JIT=1` (an environment variable,
  set as above under IDEs) turns the x86 emulator's native code off, to
  tell a JIT problem from another.
