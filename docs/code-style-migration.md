# Code style migration plan

How every source file is being brought in line with
[CODING_GUIDE.md](CODING_GUIDE.md), and the new source layout, without
breaking the build, the tests or anyone's work in progress.

## Status

| Step | State |
| --- | --- |
| Coding guide: decisions made, contradictions fixed | Done |
| `.clang-format`, `.clang-tidy` follow the guide | Done |
| Source layout: shared code by subject, a CMakeLists.txt per library, the same layout inside each engine | Done (newworlds' inside waits; see below) |
| Phase 1: tooling and CI | Done |
| Phase 2: formatting | Done |
| Phase 3: small clang-tidy fixes | Done |
| Phase 4: identifier renames | Next |
| Phase 5: hand work and enforcement | To do |

## Decisions

| Question | Decision |
| --- | --- |
| File names | Lower case with underscores, as ScummVM (`actor.cpp`, `cool_game_function.h`). The code already is: no renames. |
| Namespaces | Lower case, `reluva::<engine>`, as most C++ code and the code already does. Closed with `} // namespace reluva::clue`. |
| Public struct fields | No prefix (`target.path`). `_` is for private and protected members. |
| Fall through | `[[fallthrough]];`. A `// fall through` comment works only with GCC. |
| Line length | 120 columns (`ColumnLimit: 120`). |
| Short functions | Allowed on one line when defined inside their class (`int health() const { return _health; }`), and for short lambdas. |
| Doxygen | New code uses it; existing `//` comments are converted as each file is worked on. |
| Layout | ScummVM's subject folders under `src/` (Option C), the same layout inside every engine (A), a CMakeLists.txt per library (E). |

Still open, as additions to the guide when wanted: include order (most
files have their own header, then the project's, then the system's;
stgenerations puts system headers first), acronyms in names (`kRgba32` or
`kRGBA32`), C-style casts or `static_cast` (the code has about 1,000 and
280), `enum` or `enum class` (122 and 58), and the names that must match
something outside Reluva (Win32 and MSVC exports in
`src/emulation/x86/runtime/msvcp60.cpp`, QVM system calls in the idtech3
test programs).

## The source layout

```text
src/
  base/            main.cpp
  common/          the core: files, streams, archives, config, settings, log, hashes,
                   the System interface; and stb_image's implementation
  audio/  video/  graphics/  image/
  emulation/x86/   the Windows emulator
  backends/        platform/sdl3/, graphics/sdl3/, networking/sockets/
  gui/             the launcher
  engines/<name>/  formats/ game/ render/ audio/ ui/ pages/ tools/ <game>/
  devtools/        reluva-tool, reluva-render-check
```

[ADDING_AN_ENGINE.md](ADDING_AN_ENGINE.md#3-make-it-run) has the engine
layout. Each library folder has a `CMakeLists.txt` and a target linking
only what it uses: `reluva_common`, then `reluva_audio`,
`reluva_graphics`, `reluva_image`, `reluva_video`, `reluva_emulation`;
engines link `reluva_shared` (all of those) and nothing else under `src/`.
`nm` over the built libraries confirms no library uses another it doesn't
declare. Two dependencies the single library had hidden came out of this
and are now explicit: `common/zip.cpp`'s inflate is stb_image's, whose
implementation was compiled in `image.cpp` (now `common/stb_image.cpp`,
same settings), and the SDL backend calls the sockets backend.

### Moving files: tools/relocate.py

Every move was made by `tools/relocate.py` from a list in
`tools/relocations/`. It `git mv`s what is still at an old path, then
rewrites every reference: `#include`s, CMake source lists (also those
relative to an engine's folder), CI, scripts, comments and doc links. It
can be run any number of times, on any branch.

**A branch made before the move** (the nfs, age and boggle engines, and
others) merges `main` and then runs, from the repository's top:

```text
python3 tools/relocate.py tools/relocations/2026-11-src-layout.txt
python3 tools/relocate.py tools/relocations/2026-11-engine-layout.txt
```

Git carries the branch's edits to moved files across in the merge; the
tool brings the branch's new code in line (its includes of
`common/x86/...`, `common/audio/...`), and moves any new file it added
inside a moved folder. Files a branch added in a new folder of
`src/common/` (`src/common/glide/`) stay where they are; move them by
hand if they belong to a subject (graphics, here). Then build.

**newworlds** keeps its flat layout for now: its work is outside the main
branch (as `tools/check-docs.py` notes). When it's merged, sort its files
into the folders above with a third list.

## Where the code is today

The code follows one consistent style, not yet the guide's. Measured
with clang-format and clang-tidy 18 on 665 source files; clang-tidy
didn't see newworlds, which is disabled (expect about 10% more).

| Rule (guide section) | Today | Size of the change |
| --- | --- | --- |
| Members `_coolVariable` | `rooms_` everywhere but stgenerations | 2,268 member declarations, about 23,000 uses |
| Enum values `kValue` | `Ballroom`, `Rgba32` | 1,085 enum values |
| Named constants `kValue` | Mostly right | 256 `constexpr`, 85 global, 81 static |
| Functions `camelCase` | Mostly right | 103 |
| Formatting: `char *p`, one statement per line, 120 columns | `char* p`, one-line functions outside classes | 24,300 lines in 641 files; pointer and reference alignment is about 10,000 of them |
| Split declarations | `int a, b;` | 1,474 |
| `const` member functions, `const &` parameters | Mostly right | 16 and 19 |
| Doxygen on public APIs | `//` comments | As files are worked on |

Already in line: `nullptr` and `override` everywhere, four-space indents,
attached braces, `case` at the `switch`'s indentation, `#pragma once`
and the license header in every file.

### Found along the way

- **Clang couldn't compile `src/engines/awe/formats/savegame.cpp`**:
  `std::optional<SaveGame::Placement>` was instantiated inside `SaveGame`,
  where Clang can't yet use `Placement`'s default member initializers, so
  `emplace()` failed. Fixed in Phase 1: the struct is `SavedPlacement`,
  defined before `SaveGame`, with `SaveGame::Placement` naming it as
  before. The whole project now builds with Clang (every engine,
  newworlds too) and all 99 tests pass, so the macOS job (Clang) has
  nothing known in its way.
- Clang warns about 8 unused things GCC doesn't: lambda captures
  (`clue/game/play.cpp:992`, `idtech3/eliteforce/sp_ui_host.cpp:574` and
  `:593`), constants (`idtech3/eliteforce/game_bots.cpp:166`,
  `idtech3/eliteforce/ui_host.cpp:88`, `idtech3/pages/play_viewer.cpp:44`,
  `newworlds/briefing.cpp:67`) and a field (`newworlds/economy.h:96`).
  The NFS III merge brought 10 more: unused `this` captures in
  `nfs/host/directx.cpp` (9) and a constant in
  `graphics/glide/gpu_renderer.cpp`.
- `src/emulation/x86/runtime/msvcp60.cpp` defines `THISCALL` and `EXPORT`
  without the `RELUVA_` prefix.
- **A test suite's cases** (`Suite suite("name", { {...}, });`, and a tool
  file's `Commands`) can't keep their layout under clang-format 18: the
  name moves to a line of its own and the list lines up under its first
  argument, each case still on one line. Accepted: the alternatives
  (`AlignAfterOpenBracket: BlockIndent`, `Cpp11BracedListStyle: false`)
  change far more code.

## The tool configuration

`.clang-format` and `.clang-tidy` name, for each setting and check, the
guide section it implements. Both need version 18 or later, and the same
major version everywhere: clang-format's output changes between versions,
and Visual Studio's bundled one may be older.

- **Not formatted:** `third_party/` (its own `.clang-format`), the build
  folder, where CMake downloads SDL3 and Dear ImGui (CMake writes a
  `.clang-format` and a `.clang-format-ignore` there), and generated files
  (`.clang-format-ignore`).
- **clang-tidy** runs on Reluva's files only, by pattern, as the comment at
  the top of `.clang-tidy` shows: the compile database lists SDL's and
  ImGui's files too.
- **Changes to clang-tidy's checks** from the original file: `const` local
  variables are no longer taken for named constants (`const int cx` was
  flagged as needing `kCx`); the `_` prefix applies to private and
  protected members only; braces aren't required on one-statement bodies
  (the guide allows them), while `while (...);` is still caught; the
  fall-through and empty-body warnings are switched on, so their checks
  fire; checks enforcing rules the guide doesn't have are off; namespaces
  are checked lower case.

## The plan

Each phase is its own pull request, or one per area for the large ones,
merged when CI is green. CI builds on Linux and Windows, runs every test,
checks the renderer, builds each engine alone, builds a new engine from
the template, and checks the docs. Every phase keeps all of it passing.
Mechanical changes and hand changes never share a commit, so a reviewer
can check a mechanical commit by running its command again.

### Phase 1: Tooling and CI (done)

- `tools/format.py` and `tools/tidy.py` (see
  [DEVELOPMENT.md](DEVELOPMENT.md#code-style)): all files, the ones named,
  or `--changed`; both insist on version 18. `tidy.py` makes its compile
  database with every engine enabled and reports only Reluva's files.
- CI's "Code style" job: formatting of every file, clang-tidy of the
  changed ones, counts in the run's summary; it reports without failing
  the run.
- `.git-blame-ignore-revs`, filled in Phase 2.
- The new-engine template in the guide's style: an engine made from it
  has nothing for either tool to report, builds without warnings, and
  passes its tests. Its classes have Doxygen comments, its registration
  objects are `kSuite` and `kCommands` (the other files' follow in Phase 4).
- `awe/formats/savegame.cpp` fixed for Clang.

### Phase 2: Formatting (done)

- clang-format over all 641 files that needed it, in 11 commits: the
  shared libraries, the backends and programs, each engine, the tests.
  Whitespace only: for each commit, `python3 tools/format.py --verify
  <commit>~1` (checked out at the commit) finds the same text once
  whitespace, line continuations and namespace closing comments are taken
  out. A dry run first found 14 files where clang-format did more (split
  long strings, re-sorted `using` declarations), so `.clang-format` now
  keeps both as written. Built, and all 99 tests pass, as before.
- The commits are in `.git-blame-ignore-revs`. **Merge this branch with a
  merge commit (or rebase), not a squash merge**: a squash makes new
  commits, whose hashes aren't the ones listed, and `git blame` would show
  the formatting again (it can be fixed afterwards by listing the squashed
  commit instead).
- CI's "Code style" job now fails the run on an unformatted file.

**A branch made before the formatting** (the nfs, age and boggle
engines among them) catches up in four steps, so the reformatting itself
causes no conflicts. The commits named are the last one before the
formatting, `fe53fda185`, and the last formatting commit, `53d91d0401`:

```text
git merge fe53fda185          # main up to the formatting: real conflicts only
python3 tools/relocate.py tools/relocations/2026-11-src-layout.txt
python3 tools/relocate.py tools/relocations/2026-11-engine-layout.txt
git add -A && git commit -m "Layout as main"
python3 tools/format.py               # the branch formatted as main was
git commit -am "Format: as main"
git merge -s ours 53d91d0401  # the formatting: already done, keep ours
git merge main                        # the rest, as usual
```

`-s ours` is right only for that merge: the formatting commits change
nothing but layout, which `tools/format.py` has just applied to the
branch. Tried on copies of the boggle and stharbinger branches: conflicts
only in the first merge (8 and 2, real ones), none after, and every file
formatted. Merging `main` in one go instead, or formatting before the
first merge, gives far more conflicts (47 and 118 on the nfs branch).

### Phase 3: Small clang-tidy fixes (done)

Each made by `python3 tools/tidy.py --fix --checks=-*,<check>` and
`tools/format.py`, a commit per check, with hand changes in commits of
their own. Built with GCC and Clang and all 99 tests pass.

- **One variable per declaration** (`readability-isolate-declaration`):
  1,678 statements in 281 files (newworlds included this time). Pointers,
  references and arrays split correctly; initializers run in the same
  order. By hand after: 11 comments that described a whole group, left on
  its last variable, moved above the group.
- **`const` methods** (`readability-make-member-function-const`): 9 of 16.
  The check only sees an object's own fields, so the other 7 would have
  been `const` while changing what the object manages through pointers
  (`Surface::setPixel` writes the surface's pixels); they stay non-`const`
  with a `NOLINTNEXTLINE` saying why.
- **`const &` parameters** (`performance-unnecessary-value-param`): 16
  now take a `const` reference, 3 that are stored are moved. Checked for
  aliasing (a caller passing what the function changes): none.
- **The NFS III merge**, written before phase 3, got the same fixes
  afterwards: 755 declarations split (13 comments placed by hand), 3
  methods made `const` and 3 left with a reason (`Pokes::apply`,
  `Win32Process::signalEventWhen` and `setHostEvent`), 9 parameters, and
  the `CHECK_EQ` macro in `tests/common/mmx.cpp` split by hand (clang-tidy
  can't fix inside a macro). The three checks now find nothing anywhere.
- **Not covered:** about 780 class and struct members still declared
  several to a line (`int x, y;`, mostly in headers); no clang-tidy check
  splits members. Left for Phase 5. Code compiled only on Windows or
  macOS (`#ifdef _WIN32`, 19 blocks) wasn't seen by the checks.

### Phase 4: Identifier renames

clang-tidy renames a declaration and every use it sees.

| Risk | Handling |
| --- | --- |
| Code the compile doesn't see: disabled engines, `#ifdef _WIN32`/`__APPLE__` (19 blocks in 7 files) | Every engine enabled for the run; those files checked by hand; Windows CI |
| A header fixed from many `.cpp` files | `run-clang-tidy -fix` applies each fix once |
| A new name that clashes (`End` becomes `kEnd` beside an existing `kEnd`) | It doesn't compile, so it can't pass unnoticed; fixed by hand in the same PR |
| Names that must match outside code: `msvcp60.cpp`'s exports, QVM system calls | Left out (`NOLINT`) before the run |
| Names in strings (save files, settings keys) | Not changed by renaming code; the game-files tests (`ctest -L gamedata`) check, run by whoever has each game |
| Macros pasting names (`RELUVA_BY_OP`, `CHECK`) | Search for `##` and `#x` by hand |

One engine per PR, smallest first: testbed, netimmerse, stharbinger,
stgenerations, clue, awe, newworlds, idtech3; then the shared libraries,
backends and programs, then `tests/`. Within a PR, a commit per kind
(members, enum values, constants, functions). Then naming warnings fail
CI for changed files.

### Phase 5: Hand work and enforcement

- Doxygen comments, as each file is worked on.
- The 8 non-`const` globals: `g_` prefix, or gone.
- Class and struct members declared several to a line (about 780):
  one per line, by a script reviewed like the others, or as files are
  worked on.
- The "Code style" job's clang-tidy step fails the run too;
  `CONTRIBUTING.md` points to the guide and the tools.
