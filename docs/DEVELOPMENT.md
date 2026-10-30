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
| `reluva-tool` | Developer tools for studying game data and running game code outside the window; `reluva-tool` alone lists its commands |
| `reluva-render-check` | The 3D renderer's check against reference frames (below) |
| `reluva_tests`, `reluva_idtech3_tests`, `reluva_newworlds_tests` | Unit tests, run together by `ctest` |

## Tests

- **Running them:**
  - `ctest --test-dir build --output-on-failure` runs every unit test;
  - with Visual Studio's generator, add `-C Release` (or `Debug`);
  - in Visual Studio itself, pick a test program as the startup item, or use
    **Test > Test Explorer**.
- **No game data needed,** but for the game-files tests ([below](#engine-tests)),
  which are skipped unless pointed at a game. The tests use their own data:
  files built in code, and small test programs compiled to the games'
  formats in `tests/data/` and `tests/x86/`. Their `build.sh` scripts
  remake them, which is only needed after changing their sources.
- **Labels:** `ctest -L engine` runs the engine tests, `ctest -L gamedata`
  the game-files tests, and `ctest -L idtech3` everything for one engine.
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
- **Elite Force's.** `tests/idtech3_engine_test.cpp` (ctest
  `idtech3_engine`), with its record in
  `tests/data/idtech3/engine-record.txt`. It covers:
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
  `reluva_idtech3_engine_test --update`, review the record's diff, and
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
- **Elite Force's steps** (`tests/idtech3_gamedata.cmake`, ctest
  `idtech3_gamedata_<step>`):

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
- **Records.** They are kept in `tests/data/idtech3/gamedata/<release>/`,
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
  game (`reluva-tool sp-run -fixedclock`). Floating point is the same on
  every run.

### Adding them to a new engine

1. **The basic test.** Write a program that makes a small install, takes
   the engine through what playing does, and writes a record. Copy the
   `Hash` and `Record` classes from `tests/idtech3_engine_test.cpp`, which
   also has the `--update` handling. Register it in the engine's
   `CMakeLists.txt`:

   ```cmake
   reluva_add_engine_test(<engine> NAME <engine>_engine TARGET reluva_<engine>_engine_test
       SOURCES ${PROJECT_SOURCE_DIR}/tests/<engine>_engine_test.cpp LABELS engine)
   ```

   It's compiled with `RELUVA_SOURCE_DIR`, for finding its record under
   `tests/data/<engine>/`.
2. **The game-files test.** Write a CMake script that runs one step (`STEP`)
   of `reluva-tool` commands on the game. Copy
   `tests/idtech3_gamedata.cmake`: its `run_tool` and `record_run`
   functions, its skip when the variable isn't set, and its record
   handling. Register its steps:

   ```cmake
   reluva_add_gamedata_test(<engine> GAME <game id> ENV RELUVA_<GAME>
       SCRIPT ${PROJECT_SOURCE_DIR}/tests/<engine>_gamedata.cmake
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

`reluva-render-check tests/data/render` draws two test frames through the 3D
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
