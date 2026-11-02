# Adding an engine

An engine is a folder in `src/engines/` with a line in `engines.cfg`. The
build, the launcher, `reluva-tool`, `reluva-tests` and CI find it from
there; nothing outside its own folders names it. This page goes from an
empty engine to one that runs a game.

## 1. Start it from the template

From the top of the repository:

```sh
cmake -DENGINE=<name> -DTITLE="<Name shown>" -P cmake/NewEngine.cmake
```

- **`<name>`** is lower case letters, digits and `_`. It's the engine's
  folder, its id, and its name on the command line
  (`reluva-tool <name> ...`, `reluva-tests <name>`).
- **`TITLE`** is what the launcher shows (by default, the name in
  CamelCase: `my_game` becomes `MyGame`).

It makes:

| File | What it is |
|---|---|
| `src/engines/<name>/CMakeLists.txt` | The engine's sources, tools and tests, for the build |
| `src/engines/<name>/<name>.cpp` | Its `MetaEngine` (name, detection table, making an `Engine`) and an `Engine` that shows an empty screen |
| `src/engines/<name>/tools/files.cpp` | A first `reluva-tool` command: `reluva-tool <name> files <game folder>` |
| `tests/engines/<name>/unit/detection.cpp` | Unit tests: `<name>.unit.detection` |
| `tests/engines/<name>/engine.cpp` | The basic engine test, `<name>.engine`, with its record in `data/engine-record.txt` |
| `docs/engines/<name>/README.md` | Its docs, to fill in |

and adds `<name>=enabled` to `engines.cfg`. Build it: it compiles, and
`reluva-tests <name>` passes. Commit that as the engine's start.

## 2. Recognise the game

- **Find files that tell its releases apart:** an executable, a data
  archive, files a patch changes.
- **`reluva --hash <files...>`** prints each file's signature: the MD5 of
  its start and its size.
- **Fill in `kVariants`** in `<name>.cpp`, one entry per release (language,
  platform, patch level). The launcher's *Add Game...* then finds it.
- **Copying from a CD or an installer** (*Import Game...*) is the
  `MetaEngine`'s `planImport`; settings shown in the launcher are its
  `options`. See `src/engines/engine.h`.

## 3. Make it run

- **The engine** gets a `System` (`src/common/system.h`): graphics (2D,
  or 3D through `src/common/gfx3d.h`), input events, sound, timing. Never
  include SDL; use `src/common/` for files, archives, images, sound,
  video and fonts, and add there what another engine could use too.
- **Code layout.** As it grows, keep files by topic in subfolders (the
  game's formats, its pages, its game logic), as `src/engines/idtech3/`
  does, and list them in its `CMakeLists.txt`.
- **Developer commands** for studying the game's files go in its `tools/`
  folder, by topic: a function and a line in the table at the end of the
  file. See [DEVELOPMENT.md](DEVELOPMENT.md#reluva-tool).

## 4. Test it

- **Unit tests** in `tests/engines/<name>/unit/`, a file per topic, each
  registering a suite `<name>.unit.<topic>`; list the file and the topic in
  the engine's `CMakeLists.txt` (`reluva_add_engine_tests`).
  `tests.coverage` fails if a suite isn't listed. Build test data in code;
  never commit a game's own files.
- **The basic engine test** (`tests/engines/<name>/engine.cpp`) grows with
  the engine: make a small install of its own, take it through what
  playing does, and add what happens to the record (`test::Record`,
  `test::Hash`). Then `reluva-tests <name>.engine --update`, and review the
  record's diff.
- **The game-files test**, once it runs the real game: a CMake script,
  `tests/engines/<name>/gamedata.cmake`, using the shared helpers
  (`cmake/ReluvaGamedata.cmake`), registered with
  `reluva_add_gamedata_test` (the template's `CMakeLists.txt` has it,
  commented out).

[DEVELOPMENT.md](DEVELOPMENT.md#engine-tests) has both engine tests in
full.

## 5. Document it

- **`docs/engines/<name>/README.md`:** what it runs, which releases, where
  it stands, how to use it.
- **A page per file format** beside it, as each is worked out, with where
  each fact came from. Read [CONTRIBUTING.md](../CONTRIBUTING.md) first:
  only clean-room work is accepted.
- **Add a row** for it to the engines tables in the main
  [README](../README.md#games) and [docs/README.md](README.md).

## If it doesn't build

Set it to `<name>=disabled` in `engines.cfg` (or in `engines.local.cfg`, for
your machine only), and the rest builds without it. CI builds each engine
named in `engines.cfg` alone, enabled or not, so no engine comes to rely
on another's code.
