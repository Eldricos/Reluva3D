# Reluva

**R**eimplemented **E**ngines for **L**egacy, **U**nsupported & **V**intage **A**dventures.

Reluva runs classic games on modern systems by reimplementing their engines,
in the spirit of ScummVM. Each engine reads a game's original files from
your own installed copy; where a game's logic is compiled code, Reluva runs
that code in an interpreter and supplies the parts of the old system it
calls. No game data is included: you point Reluva at games you own.

## Supported games

| Game | Engine | State | Docs |
|---|---|---|---|
| LEGO Alpha Team | `netimmerse` | Playable: menus, saving, sound, every mission; slow for now. Data viewer | [docs/engines/netimmerse](docs/engines/netimmerse/README.md) |
| Star Trek: Voyager - Elite Force | `idtech3` | The game's own menus and Holomatch levels, run by its own code; network play between Reluva copies; single player in progress. Data viewer | [docs/engines/idtech3](docs/engines/idtech3/README.md) |
| Star Trek: New Worlds | `newworlds` | Data viewer, with a first playable layer on the mission maps. Off in default builds | [docs/engines/newworlds](docs/engines/newworlds/README.md) |

Each engine's page says how far it has got, which folder to add, and its
keys.

## Requirements

**To run:**

- Linux, Windows or macOS (macOS not yet tested), 64-bit.
- For 3D pages: a GPU and driver with Vulkan (Linux, Windows) or Metal
  (macOS). Without one, Reluva falls back to SDL's renderer, and the pages
  that need 3D say so.
- Your own installed copy of a supported game.

**To build:**

- CMake 3.24 or later.
- Git.
- A C++20 compiler: GCC or Clang on Linux, Visual Studio 2022 on Windows,
  or Xcode's Clang on macOS.
- On Linux, the system headers SDL builds against (listed per distribution
  in the build guide).

SDL3 and Dear ImGui are downloaded and built automatically; nothing else
needs installing.

## Building

On Linux and macOS:

```sh
git clone https://github.com/Eldricos/Reluva3D.git
cd Reluva3D
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build    # the unit tests
./build/reluva
```

On Windows, open the folder in Visual Studio 2022 (it configures the CMake
project itself), or build from the *x64 Native Tools Command Prompt*:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\reluva.exe
```

**[docs/BUILDING.md](docs/BUILDING.md)** has the full steps for each
platform: Linux packages for each distribution, Visual Studio from install
to debugging, macOS, troubleshooting, and the build options. Engines can be
left out with `RELUVA_ENGINE_<NAME>=OFF`; New Worlds is off by default.

## Using Reluva

- **`reluva`** opens the launcher. *Add Game...* scans a folder for
  supported games and adds what it finds; pick a game and start it.
- **`reluva <target>`** starts a configured game directly (its target name
  as the launcher lists it), and **`reluva testbed`** runs a test pattern
  that checks graphics, input and sound.
- **`reluva --hash <files...>`** prints detection signatures, for an
  engine's table of known releases.

In every game:

- **Ctrl+F5** returns to the launcher.
- **Alt+Enter** toggles fullscreen.

Each engine's keys are in its docs.

Settings are kept in `reluva.ini`, and each game's own files (its saves and
settings) in `games/<target>/` beside it. That folder is SDL's preferences
folder:

| System | Folder |
|---|---|
| Linux | `~/.local/share/reluva/reluva/` |
| Windows | `%APPDATA%\reluva\reluva\` |
| macOS | `~/Library/Application Support/reluva/reluva/` |

**`reluva-tool`** has developer tools for studying game data. Some are
general (`hexdump`, `chunks`, `words`, `table`); the rest belong to the
engines and are listed in their docs. Run it without arguments for every
command it was built with.

## Repository layout

```
src/common/        the platform-independent core every engine uses: the System
                   interface, events, config, files and archives, detection,
                   images, text and fonts, the 3D rendering interface
  audio/           sound decoders and the mixer
  video/           movies: AVI and Indeo 5
  x86/             the x86 interpreter, and stand-ins for Windows, DirectX and A3D
src/backends/sdl3/ the only code that touches SDL: window, rendering (2D and 3D),
                   input, audio
src/launcher/      the Dear ImGui game list
src/engines/       one folder per engine, plus registry.cpp listing them
src/tools/         reluva-tool
tests/             unit tests for the core and each engine
third_party/       outside code built in, each with its licence (minimp3, stb_image)
docs/              the documentation: building, shared code, and a folder per engine
```

Engines use only `src/common/` and `src/engines/engine.h`; they never touch
SDL.

## Documentation

[docs/README.md](docs/README.md) is the index: the build guide, the shared
code, and each engine's folder (`docs/engines/<engine>/`). An engine's folder
holds its README, its file formats and its notes.

## Adding an engine

1. Create `src/engines/<name>/` with an `Engine` and a `MetaEngine` (copy
   `testbed`).
2. Add a detection table built with `reluva --hash`.
3. Register its accessor in `src/engines/registry.cpp`, and add its sources
   and a `RELUVA_ENGINE_<NAME>` option to `CMakeLists.txt`.
4. Document it in `docs/engines/<name>/`, laid out as the others are
   ([docs/README.md](docs/README.md)).

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: only clean-room work is
accepted, and each format's page records how it was worked out.

## License

Reluva is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE).

The Indeo 5 decoder (`src/common/video/indeo5*`) is derived from FFmpeg's
LGPL 2.1+ decoder by Maxim Poliakovski, used under the GPL 3 as the LGPL
allows.

Reluva doesn't include any game data. Games and their assets remain the
property of their respective owners.
