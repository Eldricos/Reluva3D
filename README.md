# Reluva

**R**eimplemented **E**ngines for **L**egacy, **U**nsupported & **V**intage **A**dventures.

Reluva runs classic games natively on modern systems by reimplementing
their engines, in the spirit of ScummVM. Games aren't included: you point
Reluva at your own copy, and it plays it on Windows, Linux or macOS. There's
no emulator and no original engine, and nothing to install from the game's
CD.

## Games

Each engine is a module that can run a family of games.

| Engine | Games | State | Docs |
|---|---|---|---|
| `idtech3` | Star Trek: Voyager - Elite Force (1.0, 1.2, Expansion Pack) | Playable: single player, Holomatch (multiplayer, LAN and Internet), mods | [docs/engines/idtech3](docs/engines/idtech3/README.md) |
| `newworlds` | Star Trek: New Worlds | Data viewer, first missions playable on their maps; off by default | [docs/engines/newworlds](docs/engines/newworlds/README.md) |
| `testbed` | A test pattern, no game | For checking a system's graphics, sound and input | — |

## Requirements

**To play:**

- 64-bit Windows, Linux (X11 or Wayland) or macOS.
- For 3D games, a graphics driver with Vulkan (Windows, Linux) or Metal
  (macOS). Current drivers for AMD, Intel and NVIDIA have it; on Linux,
  Mesa's is the `mesa-vulkan-drivers` package (or your distro's
  equivalent).
- Your own copy of the game. Each engine's docs say which releases it
  recognises.

**To build:**

- CMake 3.24 or later, Git, and a C++20 compiler: GCC, Clang, Visual Studio
  2022, or Apple's command line tools.
- SDL3 and Dear ImGui are downloaded and built by CMake. The first configure
  needs internet access and takes a few minutes.
- On Linux, the system headers SDL builds against (below).

## Building

### Linux

1. **Install the tools and SDL's build dependencies.**

   **Ubuntu / Debian / Mint / Pop!_OS** (tested on Ubuntu 24.04):

   ```sh
   sudo apt install build-essential git cmake ninja-build pkg-config \
     libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
     libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
     libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
     libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
     libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev liburing-dev
   ```

   **Fedora:**

   ```sh
   sudo dnf install gcc-c++ git cmake ninja-build \
     alsa-lib-devel pulseaudio-libs-devel pipewire-devel pipewire-jack-audio-connection-kit-devel \
     libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel \
     libXScrnSaver-devel libxkbcommon-devel libdrm-devel mesa-libgbm-devel \
     mesa-libGL-devel mesa-libGLES-devel mesa-libEGL-devel vulkan-devel \
     wayland-devel wayland-protocols-devel libdecor-devel dbus-devel ibus-devel systemd-devel liburing-devel
   ```

   **Arch / Manjaro / SteamOS:**

   ```sh
   sudo pacman -S base-devel git cmake ninja alsa-lib jack libpulse pipewire sndio \
     libx11 libxcursor libxext libxinerama libxkbcommon libxrandr libxrender libxss libxtst \
     mesa vulkan-headers wayland wayland-protocols libdecor ibus libusb hidapi
   ```

   The Fedora and Arch lists follow SDL's own
   [Linux build notes](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies).
   Check there if a package name has changed.

2. **Build and run:**

   ```sh
   git clone https://github.com/Eldricos/Reluva3D.git
   cd Reluva3D
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ./build/reluva
   ```

### Windows (Visual Studio 2022)

1. **Install Visual Studio.**
   1. Run the [Visual Studio 2022 installer](https://visualstudio.microsoft.com/vs/)
      (Community edition is free).
   2. Tick the **Desktop development with C++** workload. Its default
      components include **C++ CMake tools for Windows** and a **Windows
      SDK**, the two you need.
   3. Under **Individual components**, also tick **Git for Windows**, unless
      you have Git already.
2. **Open the project.**
   1. Start Visual Studio, choose **Clone a repository**, and paste
      `https://github.com/Eldricos/Reluva3D.git`. If you've cloned it
      already, use **File > Open > Folder...** instead.
   2. Visual Studio configures the project from `CMakeLists.txt`. The first
      time, it downloads and builds SDL3 and ImGui. Wait for *"CMake
      generation finished."* in the **Output** window, with "CMake" chosen
      in its dropdown.
3. **Build and run.**
   1. In the toolbar, choose **x64-Release** for a fast build (or
      **x64-Debug** for debugging).
   2. In **Select Startup Item**, next to the green ▶ button, choose
      **reluva.exe**.
   3. Press **F5** to build and run under the debugger, or **Ctrl+F5** to
      run without it.

   A console window beside the game shows Reluva's log.

**From a command prompt instead:** use the **x64 Native Tools Command Prompt
for VS 2022** (in the Start menu):

```bat
git clone https://github.com/Eldricos/Reluva3D.git
cd Reluva3D
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\reluva.exe
```

With this generator, pick Debug or Release with `--config` when building.

### macOS

1. **Install the tools:** Apple's command line tools
   (`xcode-select --install`), then CMake and Ninja, for example with
   [Homebrew](https://brew.sh): `brew install cmake ninja`.
2. **Build and run:**

   ```sh
   git clone https://github.com/Eldricos/Reluva3D.git
   cd Reluva3D
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ./build/reluva
   ```

macOS builds and passes the tests and the 3D render check on Apple Silicon
in CI. It hasn't been played on a Mac yet.

### Choosing engines

`engines.cfg`, at the top of the repository, lists the engines to build, one
per line:

```
idtech3=enabled
newworlds=disabled
```

- **Leaving an engine out.** Set it to `disabled` (say, one that doesn't
  build at the moment), and back to `enabled` to build it again. The next
  build picks the change up by itself, in Visual Studio too.
- **Just for your machine.** Put the lines to change in `engines.local.cfg`
  beside it. Its lines win, and git ignores it.
- **The testbed** is always built.
- **New Worlds** is disabled by default: two of its files hold string
  literals longer than Visual Studio accepts. With GCC or Clang, enable it.

A typo is caught when CMake configures:

- **A line naming no engine, or a value other than `enabled` or
  `disabled`,** stops it with a message.
- **An engine folder with no line** is reported and left out.

The summary line, `Reluva engines: ...`, shows what's built.

### Build options

| Option | What | Default |
|---|---|---|
| `RELUVA_BUILD_TESTS` | the unit tests | on |
| `RELUVA_USE_SYSTEM_SDL` | use an installed SDL3 instead of building it | off |
| `RELUVA_ENGINES_FILE` | another engine list, instead of `engines.cfg` | `engines.cfg` |

In Visual Studio the options are under **Project > CMake Settings**.
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) has debug builds, tests, the
render check and troubleshooting.

## Using Reluva

`reluva` opens the launcher, a list of your games.

- **Add Game...** finds the games in a folder, such as an existing install,
  and plays them from there.
- **Import Game...** copies a game from its CD, a disc image, or an install
  into Reluva's own library folder. It takes only the files Reluva needs, so
  nothing has to be installed, on any system. Some engines can apply a
  game's patches and expansions as they're copied.
- **Start** (or a double-click) plays the selected game. Settings for the
  selected game, where an engine has them, are shown below the buttons.

In a game:

- **Ctrl+F5** returns to the launcher.
- **Alt+Enter** toggles fullscreen.

What each game's pages and keys are is in its engine's docs.

### From the command line

| Command | What it does |
|---|---|
| `reluva <name>` | Starts a game from the launcher's list directly, by its entry's name |
| `reluva testbed` | The test pattern: **Esc** to exit, **Space** for a tone |
| `reluva --hash <files...>` | Prints files' detection signatures, for an engine's table of releases |
| `reluva-tool` | Developer tools for studying games' files; alone, it lists its commands |

### Where Reluva keeps its files

Everything goes in SDL's preferences folder for Reluva:

| System | Folder |
|---|---|
| Linux | `~/.local/share/reluva/reluva/` |
| Windows | `%APPDATA%\reluva\reluva\` |
| macOS | `~/Library/Application Support/reluva/reluva/` |

In it:

- **`reluva.ini`:** the launcher's list of games, and their settings.
- **`games/<name>/`:** each game's own files: its settings, saved games,
  screenshots and downloads.
- **`library/`:** games copied by *Import Game...*.

## Repository layout

```
src/common/          platform-independent core: the System interface, events, surfaces, config,
                     case-insensitive file lookup, detection, archives (folders, .zip/.pk3) and
                     search sets, images, text and fonts, the 3D rendering interface (gfx3d.h),
                     installers (Wise), and shared interpreters and decoders
  audio/             sound decoders and the mixer: WAV (PCM, IMA ADPCM), MP3
  video/             video decoders (Bink)
  x86/               an x86 interpreter and Windows stand-ins, for games' native code
src/backends/sdl3/   the only code that touches SDL: window, rendering (2D and 3D, on SDL's GPU
                     API), input, audio
src/backends/sockets/ networking
src/launcher/        the launcher (Dear ImGui)
src/engines/         one folder per engine, plus registry.cpp listing them
src/tools/           reluva-tool and reluva-render-check
tests/               the tests (reluva-tests) and their data: shared code's in common/, each
                     engine's in engines/<engine>/
third_party/         outside code built in, each with its licence (minimp3, stb_image)
docs/                documentation: shared in docs/common/, each engine's in docs/engines/<engine>/
```

Engines use only `src/common/` and `src/engines/engine.h`. They never
include SDL.

## Documentation

[docs/README.md](docs/README.md) is the map. In short:

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): working on Reluva.
- [docs/common/](docs/common/): formats and components shared by engines.
- `docs/engines/<engine>/`: everything about one engine and its games.

## Adding an engine

1. **Create `src/engines/<name>/`** with an `Engine` and a `MetaEngine`
   (copy `testbed`).
2. **Add a detection table,** built with `reluva --hash`.
3. **Give it a `CMakeLists.txt`** in its folder (copy `testbed`'s):
   - `reluva_add_engine(<name> ACCESSOR <function> SOURCES ...)` names the
     function that returns its `MetaEngine`, and lists its sources;
   - `reluva_add_engine_tests(<name> SOURCES ... TOPICS ...)` adds its
     tests, from `tests/engines/<name>/`, to `reluva-tests`.

   The build finds the folder, and adds the engine to the registry, by
   itself. Code elsewhere that uses the engine (its `reluva-tool`
   commands) goes inside `#ifdef RELUVA_ENGINE_<NAME>`. See
   `cmake/ReluvaEngines.cmake`.
4. **Add a line `<name>=enabled` to `engines.cfg`.**
5. **Give it engine tests:** a basic one that needs no game files (run by
   CI), and one that runs on a developer's copy of the game. See
   [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#engine-tests).
6. **Document it** in `docs/engines/<name>/`, starting with a
   `README.md`: what it runs, where it stands, how to use it.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: only clean-room work is
accepted.

## License

Reluva is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE).

Reluva doesn't include any game data. Games and their assets remain the
property of their respective owners.
