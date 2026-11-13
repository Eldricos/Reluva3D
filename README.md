# Reluva

**R**eimplemented **E**ngines for **L**egacy, **U**nsupported & **V**intage **A**dventures.

Reluva runs classic games natively on modern systems by reimplementing
their engines, in the spirit of ScummVM. Games aren't included: you point
Reluva at your own copy, and it plays it on Windows, Linux or macOS. There's
no emulator of the old computer and no original engine, and nothing to
install from the game's CD. Where a game's logic is its own compiled code
(LEGO Alpha Team's, Elite Force's game DLLs, Need for Speed III's whole
program), Reluva runs that code and supplies the parts of Windows it calls.

## Games

Each engine is a module that can run a family of games.

| Engine | Games | State | Docs |
|---|---|---|---|
| `idtech3` | Star Trek: Voyager - Elite Force (1.0, 1.2, Expansion Pack); later, other Quake III engine games | Playable: single player, Holomatch (multiplayer, LAN and Internet), mods | [docs/engines/idtech3](docs/engines/idtech3/README.md) |
| `stharbinger` | Star Trek: Deep Space Nine - Harbinger (DOS, two CDs) | Every part of the game runs; being checked against the original | [docs/engines/stharbinger](docs/engines/stharbinger/README.md) |
| `newworlds` | Star Trek: New Worlds | Data viewer, first missions playable on their maps; off by default | [docs/engines/newworlds](docs/engines/newworlds/README.md) |
| `stgenerations` | Star Trek: Generations (Windows, two CDs) | Every part of the game runs; a full playthrough to come | [docs/engines/stgenerations](docs/engines/stgenerations/README.md) |
| `awe` | Agatha Christie: Murder on the Orient Express (English 1.0); later, AWE's other Agatha Christie games | Playable from a new game to the credits; being checked against the original | [docs/engines/awe](docs/engines/awe/README.md) |
| `clue` | Clue: Murder at Boddy Mansion, and in the UK Cluedo: Murder at Blackwell Grange (Windows, 2.0 and 2.2) | Playable: against computer players at three levels, hot seat, network play; being checked against the original | [docs/engines/clue](docs/engines/clue/README.md) |
| `netimmerse` | LEGO Alpha Team (English, Windows); later, other NetImmerse games | Playable: menus, saving, options, sound in 3D, every mission; slower than the original in busy scenes; a data viewer of its files | [docs/engines/netimmerse](docs/engines/netimmerse/README.md) |
| `nfs` | Need for Speed III: Hot Pursuit (US "Classics" CD); later, High Stakes | Playable: the retail game hosted, from its menus through every race type, single player and network (TCP, IPX, serial, modem); a data viewer of its files | [docs/engines/nfs](docs/engines/nfs/README.md) |
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
   1. In the toolbar, the configuration dropdown lists the project's
      presets (`CMakePresets.json`): choose **x64 Release (fast)** to play
      (**x64 Debug** for debugging, which runs games' own code several
      times slower). **x64 Release, Clang (fastest)** builds the x86
      emulator to faster code still; it needs **C++ Clang Compiler for
      Windows** and **MSBuild support for LLVM (clang-cl) toolset**, ticked
      under **Individual components** in the installer. If the dropdown
      shows Visual Studio's own **x64-Debug** and **x64-Release** instead,
      turn on **Tools > Options > CMake > General > Use CMakePresets.json**
      and reopen the folder.
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
stharbinger=enabled
newworlds=disabled
stgenerations=enabled
clue=enabled
nfs=enabled
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

### Settings

Settings are kept at three levels, and a game takes the most specific one
set:

- **Every game:** **Settings...**, with *Every game* chosen.
- **An engine's games:** **Settings...**, with *All <engine> games* chosen.
  An engine's own settings, where it has some, are here too.
- **One game:** *This game's settings*, under the selected game's buttons.

Each setting starts out unset: Reluva's default, or what's set at the
level above (the list shows which). The settings every engine has:

| Setting | Choices | Default |
|---|---|---|
| Start in | a window, full screen | a window |
| Window size | as it is, 2x where it fits, 1x to 4x the game's picture | as it is |
| Scaling | any size (letterboxed), whole multiples only | any size |
| Pixels | sharp, smoothed | sharp |
| Vertical sync | on, off | on |
| Master volume | 0% to 200% | 100% |

They're stored in `reluva.ini`: every game's in `[reluva]`, an engine's in
`[engine:<engine>]`, a game's in its own section.

In a game:

- **Ctrl+F5** returns to the launcher.
- **Alt+Enter** toggles fullscreen.

What each game's pages and keys are is in its engine's docs.

### From the command line

| Command | What it does |
|---|---|
| `reluva <name>` | Starts a game from the launcher's list directly, by its entry's name |
| `reluva testbed` | The test pattern: **Esc** to exit, **Space** for a tone, **C** for a cursor of its own |
| `reluva --hash <files...>` | Prints files' detection signatures, for an engine's table of releases |
| `reluva --detect <folders...>` | Lists the games found in each folder |
| `reluva-tool` | Developer tools for studying games' files: `reluva-tool <engine> <command>`; alone, it lists the engines and the commands for any game |

### Where Reluva keeps its files

Everything goes in SDL's preferences folder for Reluva:

| System | Folder |
|---|---|
| Linux | `~/.local/share/reluva/reluva/` |
| Windows | `%APPDATA%\reluva\reluva\` |
| macOS | `~/Library/Application Support/reluva/reluva/` |

In it:

- **`reluva.ini`:** the launcher's list of games, and the settings (for
  every game, each engine's games, and each game).
- **`games/<name>/`:** each game's own files: its settings, saved games,
  screenshots and downloads.
- **`library/`:** games copied by *Import Game...*.
- **`reluva.log`:** Reluva's messages (errors, warnings, what an engine
  reports), also written to the console; the run before's is kept as
  `reluva-previous.log`. Send it with a problem report.

## Repository layout

```
src/
  base/              main.cpp: the program
  common/            the core the rest builds on: the System interface, events, config and
                     settings, case-insensitive file lookup, byte streams, detection, archives
                     (folders, .zip/.pk3) and search sets, PKWARE DCL, Wise installers, PE files,
                     text, hashes, logging
  audio/             sound decoders, streams and the mixer: WAV (PCM, IMA ADPCM), MP3, raw PCM,
                     EA-XA and MicroTalk; a Dolby Pro Logic decoder model
  video/             video decoders: Bink, Smacker, Cinepak and Indeo 5 in AVI
  graphics/          surfaces and 8-bit drawing, palettes, fonts (built-in and TrueType),
                     cursors, the 3D rendering interface (gfx3d.h) and software 3D (soft3d/),
                     GPU compute, and the Glide stand-in's pipeline (glide/)
  image/             pictures (TGA, JPEG, PNG, BMP)
  emulation/x86/     an x86 emulator (with a JIT on x86-64) and stand-ins for Windows,
                     DirectX, A3D and Glide, for games' native code
  backends/          the only code that touches SDL or the system: platform/sdl3/ (window,
                     input, audio), graphics/sdl3/ (2D and 3D on SDL's GPU API),
                     networking/sockets/
  gui/               the launcher (Dear ImGui)
  engines/           one folder per engine, each laid out the same way
                     (docs/ADDING_AN_ENGINE.md), plus registry.cpp listing them
  devtools/          reluva-tool, with its commands for any game, and reluva-render-check
tests/               the tests (reluva-tests) and their data: shared code's in common/, each
                     engine's in engines/<engine>/
tools/               scripts for working on Reluva (docs checks, moving files)
third_party/         outside code built in, each with its licence (minimp3, stb_image,
                     stb_truetype)
docs/                documentation: shared in docs/common/, each engine's in docs/engines/<engine>/
```

Each folder under `src/` that is a library has its own `CMakeLists.txt`.

Engines use only the shared code (`common/`, `audio/`, `video/`,
`graphics/`, `image/`, `emulation/`) and `src/engines/engine.h`. They
never include SDL.

## Documentation

[docs/README.md](docs/README.md) is the map. In short:

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): working on Reluva.
- [docs/CODING_GUIDE.md](docs/CODING_GUIDE.md): how its code is written.
- [docs/COMMIT_STANDARD.md](docs/COMMIT_STANDARD.md): how its commits and
  pull requests are written.
- [docs/DOCUMENTATION_STANDARD.md](docs/DOCUMENTATION_STANDARD.md): how its
  docs are laid out, named and written.
- [docs/common/](docs/common/): formats and components shared by engines.
- `docs/engines/<engine>/`: everything about one engine and its games.

## Adding an engine

```sh
cmake -DENGINE=<name> -DTITLE="<Name shown>" -P cmake/NewEngine.cmake
```

starts one from a template: its folder in `src/engines/`, its first
`reluva-tool` command, its tests and its docs folder, and its line in
`engines.cfg`. It builds and passes its tests from the start.
[docs/ADDING_AN_ENGINE.md](docs/ADDING_AN_ENGINE.md) goes from there to an
engine that runs a game.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: only clean-room work is
accepted. Code follows [docs/CODING_GUIDE.md](docs/CODING_GUIDE.md), and
docs follow [docs/DOCUMENTATION_STANDARD.md](docs/DOCUMENTATION_STANDARD.md),
and commits and pull requests
[docs/COMMIT_STANDARD.md](docs/COMMIT_STANDARD.md).

## License

Reluva is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE).

The Indeo 5 decoder (`src/video/indeo5*`) is derived from FFmpeg's
LGPL 2.1+ decoder by Maxim Poliakovski, used under the GPL 3 as the LGPL
allows.

Reluva doesn't include any game data. Games and their assets remain the
property of their respective owners.
