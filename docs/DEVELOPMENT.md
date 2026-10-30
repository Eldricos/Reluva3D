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
- **No game data needed.** The tests use their own data: files built in
  code, and small test programs compiled to the games' formats in
  `tests/data/`, `tests/x86/`. Their `build.sh` scripts remake them, which
  is only needed after changing their sources.
- **CI.** The workflow (`.github/workflows/ci.yml`) builds and runs the
  tests on Linux, macOS and Windows for every push and pull request.

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
