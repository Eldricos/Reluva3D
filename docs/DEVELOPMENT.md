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
- **SDL and Dear ImGui** are fetched and built by CMake. The first configure
  needs internet access and takes a few minutes; later ones reuse them.
  `-DRELUVA_USE_SYSTEM_SDL=ON` uses an installed SDL3 instead, found through
  its CMake package (e.g. via `CMAKE_PREFIX_PATH`).

## Programs

| Program | What it's for |
|---|---|
| `reluva` | The launcher and games |
| `reluva-tool` | Developer commands for studying game data and running game code outside the window ([below](#reluva-tool)) |
| `reluva-render-check` | The 3D renderer's check against reference frames (below) |
| `reluva-tests` | Every test that needs no game files: the shared code's and each enabled engine's (below) |

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
  detecting and importing games) are in `src/tools/`. Each engine's are in
  its `tools/` folder, by topic (`src/engines/idtech3/tools/qvm.cpp`), and
  are built only with the engine.
- **Adding one.** Write it as `int cmdName(int argc, char** argv)` (its
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
  `hexdump`) are in `src/tools/tool.h`.

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
- **CI.** The workflow (`.github/workflows/ci.yml`) builds and runs the
  tests on Linux, macOS and Windows for every push and pull request. It
  also builds each engine alone, every other disabled, so no engine comes
  to rely on another's code. To try that locally, point
  `-DRELUVA_ENGINES_FILE` at a list of your own, or use
  `engines.local.cfg`.

## Engine tests

Each engine can have two engine tests, besides its unit tests. They test the
engine as a whole, so a change anywhere (in `src/common/`, say) that alters
what an engine does is caught, even where no unit test looks. Elite Force
(`idtech3`) has both.

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
  `ctest --test-dir build -C Release -L gamedata`. Without the variable the
  steps show as skipped, so a plain `ctest` stays green for everyone.
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
  `UPDATE`) to compare against it.
- **Repeatable runs.** The game code reads a clock that moves only with the
  game (`reluva-tool idtech3 sp-run -fixedclock`). Floating point is the same on
  every run.

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
- **In CI** it runs on macOS (Metal) and on Linux (Vulkan, on Mesa's
  software driver).
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
  `src/backends/sdl3/shaders/compiled.h`, which is committed. Only after
  changing `draw.vert` or `draw.frag`, run
  `src/backends/sdl3/shaders/build.sh`. It needs `glslang-tools` and
  `spirv-cross`.

## Floating point

The build turns off fused multiply-add (`-ffp-contract=off` with GCC and
Clang). Fusing rounds differently, so game code would give different results
on different machines: collision, movement, and what a networked game agrees
on. Keep it off.

## IDEs

- **VS Code** opens the folder directly, with the *CMake Tools* and *C/C++*
  extensions. CLion and Qt Creator open `CMakeLists.txt` directly.
- **Visual Studio.** To pass command-line arguments to `reluva.exe` (e.g. to
  start a game directly):
  1. In Solution Explorer, right-click `CMakeLists.txt` and choose **Add
     Debug Configuration** (or **Debug and Launch Settings**), then
     **reluva.exe**.
  2. In the `launch.vs.json` that opens, add an `args` line to the
     `reluva.exe` entry:

     ```json
     "args": [ "testbed" ]
     ```

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
