# Code style migration plan

How to bring every source file in line with [CODING_GUIDE.md](CODING_GUIDE.md)
without breaking the build, the tests or anyone's work in progress, and
the options for reorganizing where files live. Nothing here has been done
yet except the tool configuration (step 1.1); the rest waits on the
decisions in "Decisions needed".

## Where the code is today

The code already follows one consistent house style. It's just not the
guide's style. Measured on the current tree with clang-format 18 and
clang-tidy 18. newworlds is disabled in `engines.cfg`, so clang-tidy didn't
see it; expect about another 10% on top of the tidy numbers.

| Area | Files | Lines |
| --- | --- | --- |
| `src/engines/` (7 engines + testbed) | 452 | 131,600 |
| `src/common/` | 122 | 30,700 |
| `src/backends/`, `src/gui/`, `src/devtools/`, `base/main.cpp` | 18 | 7,250 |
| `tests/` | 72 | 24,600 |
| `tools/clue-re/` (reverse-engineering scratch: C++ and Python) | 46 | 1,400 |
| `third_party/` (stb, minimp3: never touched) | 3 | 14,900 |

### Already in line with the guide

- `nullptr` everywhere (one `NULL` left), `override` everywhere: clang-tidy
  found nothing for either.
- Four-space indents, attached braces, `case` at the `switch`'s indentation,
  `template<` with no space, and `kConstant` names for most constants.
- `#pragma once` in every header and an SPDX license header in every file.
  The guide doesn't require either (see "Gaps in the guide").

### Not in line with the guide

| Rule (guide section) | Today | Size of the change |
| --- | --- | --- |
| File names `UpperCamel.cpp` (File Names) | All 658 files are `lower_case.cpp` | Every file renamed; 2,560 `#include` lines, 226 links in docs, ~2,600 path mentions in comments and CMake |
| Members `_coolVariable` (Class Member Variables) | `rooms_` (trailing `_`) in every area except stgenerations, which already uses `_rooms` | 2,268 member declarations, ~23,000 uses |
| Enum values `kValue` (Constants) | `Ballroom`, `Rgba32` | 1,085 enum values (735 plain, 350 `enum class`) |
| Constants `kValue` (Constants) | Mostly right; some `constexpr int End`, `MAX_FOO` | 256 `constexpr`, 85 global, 81 static constants |
| Functions `camelCase` (Naming) | Mostly right | 103 functions |
| `char *p`, `int &r` (Pointers / References) | `char* p`, `int& r` | ~10,000 lines (clang-format) |
| One statement per line (Semicolons / Avoid one-liners) | `int get() const { return _x; }`, one-line lambdas | ~5,000 lines (clang-format) |
| Split declarations (Vertical Space) | `int a, b;` | 1,474 statements |
| Const member functions, `const &` params (Const-Correctness) | Mostly right | 16 + 19 places |
| Doxygen `/** */` on public APIs (Documentation) | Plain `//` comments: 17,800 comment lines, 4 Doxygen blocks | Rewriting by hand; see decision 7 |
| `// fall through` (Switch/Case) | Hardly any fall-throughs (one of each form) | Negligible |

All clang-format changes together touch about 640 files and ~22,600
lines, not counting generated files, which are now excluded (step 1.1).

### Found along the way

- **Clang can't compile `src/engines/awe/savegame.cpp`** (lines 189 and
  191: `std::optional<Placement>::emplace()`, where `Placement` is a nested
  struct with default member initializers). GCC and MSVC accept it, so CI
  passes, but the macOS build (Clang, CI job switched off) would fail, and
  clang-tidy skips that file. Fix it before the rename step
  (`emplace(Placement{})`, or move `Placement` out of `SaveGame`).
- `src/emulation/x86/runtime/msvcp60.cpp` defines `THISCALL` and `EXPORT`
  macros without the `RELUVA_` prefix, and its exported names must match
  MSVC's own: its names have to be left out of any rename.

## The tool configuration

`.clang-format` and `.clang-tidy` now implement the guide; each setting
and check names the guide section it comes from. What changed and why:

### .clang-format

- Comments tie every setting to a guide section. Settings the guide
  doesn't cover are marked "not in guide" and keep the current layout.
- Added `AllowShortLambdasOnASingleLine: Empty` and
  `AllowShortCaseLabelsOnASingleLine: false`: "Semicolons Must Be Followed
  By A New Line" applies to lambdas and `case` lines too.
- Added `SpacesInLineCommentPrefix` ("Comment operators followed by a
  space character") and `FixNamespaceComments` (closing `}` comment on
  namespaces; an existing comment such as `// End of namespace Reluva` is
  kept).
- Added `DerivePointerAlignment: false`, so `char *p` is applied
  everywhere and not guessed from each file.
- Added `Standard: c++20` (the project's standard), `InsertNewlineAtEOF`,
  `KeepEmptyLinesAtTheStartOfBlocks: false`.
- Code that isn't Reluva's is never formatted:
  - `third_party/.clang-format` (`DisableFormat: true`) covers stb and
    minimp3.
  - SDL3 and Dear ImGui aren't in the repository: CMake downloads them
    into the build folder's `_deps/`, and an IDE often puts its build
    folder inside the checkout (`out/`, `cmake-build-*/`). So
    `CMakeLists.txt` writes a `.clang-format` that turns formatting off,
    and a `.clang-format-ignore` matching everything, into the build
    folder when it configures. SDL brings its own `.clang-format`, which
    would otherwise win for its files. Checked on SDL's and ImGui's
    sources in `build/` and `out/build/x64-Release/`: nothing changes.
    `src/backends/sdl3/` is Reluva's own code and is formatted.
  - clang-tidy: SDL also brings its own `.clang-tidy`, and the compile
    database lists its files and ImGui's (243 of 596). So clang-tidy is
    run only on Reluva's files, by pattern:
    `run-clang-tidy -p build '^(?!.*/_deps/).*/(src|tests)/'` (at the top
    of `.clang-tidy`, and what `tools/tidy.sh` will do).
- New `.clang-format-ignore`: the generated files (the x86 DLL blob,
  shader blob, QVM and test-DLL byte arrays, Indeo, Bink and stgenerations
  opcode tables). Without it, those files alone were 43,000 of the 60,000
  changed lines. In clang-format 18, `*` and `**` in this file match one
  folder level only, which is why whole folders are switched off with a
  `.clang-format` file instead.

### .clang-tidy

| Change | Why |
| --- | --- |
| `ConstantCase`/`ConstantPrefix` replaced by `GlobalConstant`, `StaticConstant`, `ClassConstant`, `ScopedEnumConstant` | `Constant` matches *every* `const` variable, so `const int cx = ...` in a function was flagged as needing to be `kCx`: 96 false hits in `clue/board.cpp` alone. The guide's "Constants" means named constants. Local `const` variables and parameters are now camelCase. |
| `MemberPrefix: _` replaced by `PrivateMember`/`ProtectedMember` with `_`, `PublicMember` without | `Member` covers plain structs' fields (`target.path`, `game.engineId`), which would all have become `_path`. The guide says "*Class* Member Variables". See decision 3. |
| Removed `readability-braces-around-statements` | It required braces on every `if`/loop body, but the guide's "Avoid composite one-liners" shows `if (condition)` + an indented statement with no braces as GOOD. |
| Added `bugprone-suspicious-semicolon` and `clang-diagnostic-empty-body` | They catch `while (...);`, which the braces check was there for ("Mandatory `{}` for empty loops"), without requiring braces everywhere. |
| `ExtraArgs: -Wempty-body -Wimplicit-fallthrough` | A `clang-diagnostic-*` check only reports a warning that the compile enables. Before this, the fall-through check never fired. |
| Removed `modernize-avoid-c-arrays` and `readability-else-after-return` | Neither is a rule in the guide. `avoid-c-arrays` would flag every constant table (`static const char *const kNames[]`). Add them back along with a guide rule if wanted. |
| `HeaderFilterRegex: '(^|/)(src|tests)/'` | `.*` also reported on SDL's and ImGui's headers under `build/_deps/`. |
| Macro allow-list `^(RELUVA_\|USE_\|CHECK\|STB\|MINIMP3_\|WIN32_LEAN_AND_MEAN$\|NOMINMAX$)` | It allowed `CRAZY`, which is just the guide's example. These are the macros the code really needs. |
| Added `UnionCase`, `ScopedEnumConstant*`, `MacroDefinitionCase: UPPER_CASE`, `IgnoreMainLikeFunctions` | Cases the guide covers but the config didn't. |
| `modernize-use-override.IgnoreDestructors: true` | The guide asks for `override` on overriding *functions*. Change this if destructors should carry it too. |

Both files need version 18 or later (`.clang-format-ignore` and
`SpacesInParens` are new in 18 and 17). clang-format output changes between
major versions, so CI and everyone's editor should use the same major
version. Visual Studio's bundled clang-format may be older; point it at a
version 18 install.

## Gaps in the guide

### Contradictions within the guide

1. **"Commas followed by a white space"** shows `int d, e;`, but "Split
   variable declarations" forbids that. Suggest changing the example to a
   function call only.
2. **"Template definitions"** shows `template<typename chaos>` (lower
   case); "Type Names" says template types are `CamelCase`.
3. **"Avoid composite one-liners"** calls `be_crazy()`, a snake_case
   function.
4. **"Indentation level is not increased after namespace clause"** has
   `byte Actor::kRandomActor = 0;`: a non-`const` static member with a
   constant's `k`. `byte` isn't a type in this project.
5. **"Switch/Case"** says the `// fall through` comment lets "the compiler
   see it". That's true for GCC only: Clang and MSVC need `[[fallthrough]];`
   (C++17). Suggest requiring `[[fallthrough]];`.
6. **"File Names"** wants `UpperCamel.cpp`, but the examples elsewhere use
   `#include "common/system.h"`. Note too that ScummVM itself uses
   lower-case file names (`engines/scumm/actor.cpp`). If following ScummVM is
   the goal, lower case (the current names) may be the better rule.
7. Examples use ScummVM's APIs (`Common::String`, `warning()`, `Dialog`,
   `ReluvaLauncher`), which don't exist here. Reluva has `std::string`,
   `reluva::log`, and so on.

### Things the guide doesn't say (recommended additions)

- **Namespace names.** The example uses `namespace Reluva`; the code uses
  `reluva::clue`. Not stated as a rule (decision 2).
- **Fields of plain structs** (decision 3).
- **Line length.** The code is wrapped at 120 columns by hand
  (decision 6).
- **Include order and style.** Today most files have their own header
  first, then project headers, then `<system>` headers; stgenerations puts
  system headers first. Suggest: own header, a blank line, project
  headers, a blank line, system headers. Once agreed, clang-format can sort
  them (`IncludeBlocks: Regroup` with `IncludeCategories`).
- **File header:** the SPDX and copyright lines every file already has.
- **Header guard:** `#pragma once`, which every header already uses.
- **Acronyms in names:** `Md5`/`MD5`, `BspFile`/`BSPFile`, `kRgba32`/`kRGBA32`.
  Needed before the file renames and the enum renames.
- **Casts.** The guide's own example is a C-style cast; the code has
  ~1,000 C-style and ~280 `static_cast`. Say which is preferred, or that
  either is fine.
- **Macro names:** `UPPER_CASE` with a `RELUVA_` prefix (the code does
  this already; now checked).
- **`enum` vs `enum class`.** The code has both (122 and 58).
- **Exceptions:** names that must match something outside Reluva (Win32
  and MSVC runtime exports, QVM system calls, original file format
  fields). Mark them `// NOLINT(readability-identifier-naming)` or list
  them in the tidy config.

## Decisions needed

Each changes how big the work is. My recommendation comes first.

1. **File names.** (a) Keep `lower_case.cpp` and change the guide (no
   work; matches ScummVM), or (b) rename to `UpperCamel.cpp` as the guide
   says (largest step: every file, every include). About 50 names have no
   underscores to split on (`combatdata`, `rgbimage`, `optionsmenu`), so
   they need a hand-written mapping.
2. **Namespaces.** (a) Keep `reluva::engine` lower case, or (b) change to
   `Reluva::Clue` like the example (touches every namespace line and every
   qualified name).
3. **Public struct fields.** (a) No prefix, `struct GameTarget { std::string path; }`
   (current code, and what the config now does), or (b) `_` on every data
   member.
4. **Braces on single statements.** (a) Optional, as the guide shows
   (config now does this), or (b) always required (change the guide and
   turn `readability-braces-around-statements` back on).
5. **One-line functions.** (a) Not allowed, per the semicolon rule (now
   configured; ~5,000 lines), or (b) allow short accessors
   (`AllowShortFunctionsOnASingleLine: InlineOnly`).
6. **Line length.** (a) `ColumnLimit: 120` (adds only ~1,200 lines to the
   formatting change and keeps lines from growing), or (b) `0`, no limit
   (now configured, as the guide says nothing).
7. **Doxygen.** (a) Turn `//` comments above declarations in *headers*
   into `/** */` as each file is next worked on, and require it for new
   code, or (b) convert everything now (thousands of comments by hand), or
   (c) let `///` count.
8. **`tools/clue-re/`.** (a) Leave it out of the standard (research
   scratch), or (b) bring it in line.

## The plan

Each phase is its own pull request, or one per area for the large ones,
and each is merged only when CI is green. CI already builds on Linux and
Windows, runs every test, checks the renderer against reference frames,
builds each engine alone, builds a new engine from the template, and
checks the docs. That is the safety net, and every phase keeps all of it
passing.

Mechanical changes and hand changes never share a commit, so a reviewer
can check a mechanical commit by re-running its script.

### Phase 1: Agree on the rules (no code changes)

1. Tool configuration (this branch): `.clang-format`, `.clang-tidy`,
   `.clang-format-ignore`.
2. Make the decisions above and fix the guide: the contradictions, the
   additions, and the final configs to match.
3. Fix `awe/savegame.cpp` so Clang compiles it.

### Phase 2: Tooling and CI

1. Pin clang-format and clang-tidy (version 18, as Ubuntu 24.04 ships) in a
   new CI job, `style`.
2. Add `tools/format.sh` (format all, or only changed files) and
   `tools/tidy.sh` (configure with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` and
   *every* engine enabled, newworlds included, then run `run-clang-tidy`).
3. The `style` job starts as a report only, not failing the build.
4. Add `.git-blame-ignore-revs`. Each mechanical commit's hash goes in it, so
   `git blame` (and GitHub's blame view) skips them. Once per clone:
   `git config blame.ignoreRevsFile .git-blame-ignore-revs`.
5. Bring the new-engine template (`cmake/engine-template/*.in`) in line
   now, so every new engine starts out right.

### Phase 3: Formatting (clang-format, whitespace only)

1. Run clang-format one area per commit: `src/common`, then backends +
   launcher + tools + main, then each engine, then `tests/`.
2. **Proof that nothing changed:** for every file, the old and new text
   with all whitespace removed must be identical, apart from added
   `// namespace x` comments. A short script checks this, so the review
   comes down to "the script passed". Then CI as usual.
3. Do it at a quiet time and announce it. For an open branch, run
   `tools/format.sh` on the branch before rebasing. The rebase is then
   nearly conflict-free, because both sides have the same formatting.
4. Add the commit hashes to `.git-blame-ignore-revs`.
5. Make the `style` job fail on unformatted code from here on.

### Phase 4: Mechanical clang-tidy fixes (small)

`readability-isolate-declaration` (1,474), `performance-unnecessary-value-param`
(19), `readability-make-member-function-const` (16), each applied with
`run-clang-tidy -fix -checks=-*,<one check>`, one check per commit.
Review `isolate-declaration` by eye: `int a, *b;` must become
`int a;` + `int *b;`. Build and test after each.

### Phase 5: Identifier renames (the risky one)

clang-tidy's `readability-identifier-naming` can rename a declaration and
every use it sees. The risks, and how each is handled:

| Risk | Handling |
| --- | --- |
| Code the compile doesn't see: disabled engines, `#ifdef _WIN32`/`__APPLE__` blocks (19 blocks in 7 files) | Enable every engine for the run; search those 7 files by hand; Windows CI catches the rest. |
| One header fixed from many `.cpp` files | `run-clang-tidy -fix` collects all fixes and applies each once (`clang-apply-replacements`). |
| A new name that clashes (`rooms_` becomes `_rooms` while another `_rooms` exists, or `End` becomes `kEnd` where a `kEnd` exists) | The compile fails, so it can't pass unnoticed. Fix by hand in the same PR. |
| Names that must match outside code: MSVC/Win32 exports (`msvcp60.cpp`), QVM system calls, the idtech3 game-side `.c` test files | Exclude these files, or `NOLINT` them, before running. |
| Names that appear in strings (save files, config keys, the debug pages' labels) | Renaming code identifiers doesn't change strings. Save and config compatibility stays as it is. The engine tests with game files (`ctest -L gamedata`) are the check, run by whoever owns each game. |
| Macros that paste names (`RELUVA_BY_OP`, `CHECK`) | Search for `##` and `#x` uses by hand. |

Order: one engine per PR, smallest first so the process gets tested
cheaply: testbed, netimmerse, stharbinger, stgenerations, clue, awe,
newworlds, idtech3. Then `src/common` + backends + launcher + tools, then
`tests/`. Within a PR, one commit per kind: members, enum values,
constants, functions. Each PR has to pass the full CI, including the
"engine alone" builds. Each commit goes in `.git-blame-ignore-revs`.

Then turn naming warnings into errors in CI (`WarningsAsErrors:
'readability-identifier-naming'`), checking changed files only.

### Phase 6: File and folder moves (if chosen)

Do the moves of the chosen layout and any file renames (decision 1)
together, so each file moves only once.

1. A mapping file `old/path.cpp -> new/Path.cpp`, generated and then
   corrected by hand, reviewed first as its own PR.
2. A script that, from the mapping, `git mv`s each file, rewrites
   `#include`s, the `SOURCES` lists in each `CMakeLists.txt`,
   `ReluvaEngines.cmake` and `NewEngine.cmake`, the CI workflow, the paths in
   comments, and the links in `docs/`. `tools/check-docs.py` then confirms
   every link resolves.
3. Pure moves in one commit, path fixes in the next, so Git sees each
   file as renamed and `git log --follow` keeps its history.
4. Renames that only change case (`board.cpp` to `Board.cpp`) on
   Windows and macOS, which ignore case: Git handles them on pull, but a
   branch made before the rename may need a fresh checkout. Say so in the
   announcement.

### Phase 7: Hand work and enforcement

- Doxygen comments, per decision 7.
- The 8 remaining non-`const` globals: give them the `g_` prefix, or
  remove them.
- `docs/ADDING_AN_ENGINE.md` and `CONTRIBUTING.md` point to the guide and
  the tools; the `style` job blocks merges.

## Reorganizing the files

The current layout:

```text
src/
  base/main.cpp
  common/            utilities, fs, config, detection, images, fonts, drawing,
    audio/ video/    3D, net, and the whole x86/Win32/DirectX emulator
    soft3d/ x86/
  backends/sdl3/ backends/sockets/
  launcher/
  tools/             reluva-tool and the render check
  engines/<engine>/  flat (awe: 68 files, newworlds: 51, clue: 35), or
                     split into formats/ game/ pages/ tools/ (stgenerations,
                     stharbinger, idtech3)
tests/engines/<engine>/   tests, apart from the engine
docs/engines/<engine>/    docs, apart from the engine
tools/clue-re/            research scratch
```

Several options follow. Options A to C are alternatives (pick one); D and E
can be added to any of them.

### Option A: Same top level, standard layout inside each engine

Keep `src/`, `tests/` and `docs/` as they are. Give every engine the same
inside:

```text
src/engines/<engine>/
  CMakeLists.txt
  MetaEngine.cpp      the engine's registration, options, import
  Detection.cpp/.h    recognizing its games
  <Engine>.cpp/.h     the Engine class: the main loop
  formats/            readers for the game's files
  game/               game logic and state
  render/  audio/     the engine's own drawing and sound, where it has any
  ui/                 menus, HUD, dialogs
  pages/              the launcher's viewers and debug pages (*_viewer.cpp today)
  tools/              reluva-tool commands
  <game>/             code for one game of a multi-game engine
                      (idtech3/eliteforce, netimmerse/alphateam)
```

- **Pros:** the least churn (only engine files move); anyone who knows one
  engine can find their way in another; the template can create the
  skeleton.
- **Cons:** leaves `src/common` as a grab-bag of 120 files.

### Option B: ScummVM's layout

The `src/` level goes, and the shared code is split by subject, as
ScummVM's `common/`, `audio/`, `video/`, `graphics/`, `image/`, `gui/`,
`base/`:

```text
base/            base/main.cpp, engine.h, registry (what ScummVM calls base/)
common/          strings, fs, streams, config, settings, log, hashes,
                 archives (zip, wise, dcl, inflate), serializer
audio/           mixer, decoders, mp3, wav
video/           bink, smacker, cinepak, indeo, avi
graphics/        draw, palette, font, truetype, cursor, gfx3d, soft3d
image/           bmp, image (stb)
emulation/x86/   cpu, jit, fpu, disasm, pe, win32/, directx/, runtime/
                 (the Windows emulator, now under emulation/x86)
net/             net.h (and backends/networking for sockets)
backends/        platform/sdl3/, graphics/gpu/, networking/sockets/
gui/             launcher
engines/<engine>/    with Option A inside
devtools/        reluva-tool, render-check, clue-re
test/            tests
doc/             docs
dists/           packaging, when there is some
```

- **Pros:** familiar to anyone from ScummVM; includes say what they are
  (`#include "audio/mixer.h"`); the Windows emulator, a large subsystem of
  its own, is no longer hidden inside `common/`.
- **Cons:** the most churn: every include in the project changes; the
  root folder gets crowded; the include root becomes the repository root.

### Option C: ScummVM's split, kept under src/

Option B's subject folders, but inside `src/`, with `tests/`, `docs/` and
`tools/` staying where they are:

```text
src/
  base/  common/  audio/  video/  graphics/  image/
  emulation/x86/   backends/   gui/   engines/   devtools/
tests/   docs/   tools/
```

- **Pros:** most of B's clarity with a tidy root; includes still start at
  `src/`, so `engines/...` includes don't change.
- **Cons:** every include of `common/...` changes (the ones moving out of
  it); differs a little from ScummVM's root.

### Option D: Self-contained engines (add to A, B or C)

Move each engine's tests and docs into the engine's own folder:

```text
engines/<engine>/
  ...code...
  tests/     from tests/engines/<engine>/ (test data in tests/data/)
  docs/      from docs/engines/<engine>/
```

- **Pros:** an engine is one folder: adding, reviewing, disabling or
  removing one touches one place; the "engine alone" CI job already
  thinks this way.
- **Cons:** ScummVM doesn't do this; `tools/check-docs.py`,
  `.markdownlint-cli2.jsonc` and `ReluvaEngines.cmake` need path updates;
  docs are less easy to browse in one place.

### Option E: A CMakeLists.txt per folder (add to any)

Today the root `CMakeLists.txt` lists every `src/common` file. Give each
library folder its own `CMakeLists.txt`, as the engines already have (and
as ScummVM's `module.mk` per folder):

```text
src/common/CMakeLists.txt     reluva_common
src/audio/CMakeLists.txt      reluva_audio (if split out)
src/backends/CMakeLists.txt
```

- **Pros:** moving a file only touches the list next to it; dependency rules
  ("common must not use SDL", "engines talk only to common") can be
  enforced by which library links which.
- **Cons:** a small CMake refactor; no change for users.

### Suggested combination

**C + A + E**, done in Phase 6 alongside the file renames. It gives the
ScummVM-style split of the shared code without moving the root, and an
engine layout the template can stamp out. D is worth it if engines will be
added or dropped often, or by different people.
