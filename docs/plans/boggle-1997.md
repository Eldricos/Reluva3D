# Boggle (1997): engine implementation plan

Initial plan: 2 November 2026. This document preserves the original milestones
and historical access limits; [current support](../engines/boggle/README.md)
now includes playable Classic variants, user dictionaries and presentation.

The subsequent [stage 1 static inspection](boggle-1997/inspection.md)
records successful Drive access, resource findings, detection candidates and
remaining access/behavioral limits. The initial access failure below is historical.

Target supplied by the owner: Hasbro Interactive's Boggle (1997), developed
by PCA, Inc. and Third-i Productions. Confirm the precise edition, language
and credits from the supplied media before publishing a compatibility claim.

## Review and branch boundary

Work branch: `engine/boggle-1997`, created from
`ecb655dea9402339e95ea159343edfcfd5e71aab`. GitHub's `main` resolved to this
same commit during review. The checkout initially had a clean working tree
on `work`; its ref must remain unchanged. All subsequent changes belong on
the Boggle branch. Do not commit, merge, rebase, reset, force-push or otherwise
move `main` or any other existing branch. Check the current branch before
each write or commit; use explicit branch names for any eventual push.

Repository guidance reviewed:

- [CONTRIBUTING.md](../../CONTRIBUTING.md): permitted evidence, forbidden
  sources, provenance and no retail assets.
- [ADDING_AN_ENGINE.md](../ADDING_AN_ENGINE.md), the generator and all its
  templates in `cmake/engine-template/`: registration, tools and initial tests.
- [DEVELOPMENT.md](../DEVELOPMENT.md), `cmake/ReluvaEngines.cmake`,
  `cmake/ReluvaGamedata.cmake` and `.github/workflows/ci.yml`: suite names,
  synthetic records, optional local game-data tests and engine isolation.
- `src/engines/engine.h`, `src/common/system.h`, detection, filesystem,
  image/audio interfaces and the shared developer commands.
- [Elite Force's research method](../engines/idtech3/REVERSE_ENGINEERING.md)
  and [the x86 interpreter](../common/x86.md): useful precedents, not evidence
  about Boggle's architecture.

This is a review of the engine integration path and relevant shared facilities,
not an audit of every existing engine implementation.

## Evidence currently available

The owner supplied a [Drive folder](https://drive.google.com/drive/folders/1PQlLY6Lnxcpv5dPefUHmk4ZupoaWwSjS?usp=sharing)
described as a fresh install and CD contents. The folder could not be opened
through the available web tool. The executor's network policy excludes Drive;
its Git HTTPS connection also failed because the configured proxy was
unreachable. GitHub metadata was readable through its connector.
No game files were downloaded, opened or added to Git. There are no observed
Boggle filenames, signatures, formats, binary addresses or behavior results yet.
The original game has not been run. Current ScummVM policy could not be fetched;
the operational rules below come from this repository and the owner's explicit
permission to use Ghidra, not a claim of independent ScummVM policy verification.

The first prerequisite is access to the two supplied file sets through an
approved connection or an external local directory. Do not infer their contents
from the title or replace inspection with another release found online.

## Research and data handling rules

All engine code will be written independently. Permitted references provide
facts; do not copy or mechanically translate proprietary disassembly or
decompiler output into the implementation. Never use leaked, stolen or
unlicensed source, or knowledge derived from it. Record the URL, version,
license and factual purpose of every external reference actually consulted.
The project's code uses GPL-3.0-or-later notices; this task does not authorize
adding third-party implementation code or new dependencies.

Ghidra, disassembly, debugging and observation may be used to understand the
owner's binaries for interoperability under the repository's rules. This is
the project's documented independent reimplementation workflow; it does not
by itself establish a formal two-team clean-room process. Research produces
behavioral and format specifications, followed by original implementations
and tests. Do not claim researcher/implementer separation unless actually used.

Keep originals, extracted assets, dictionaries, screenshots, recordings,
Ghidra projects, decompiled listings, memory dumps and raw research logs outside
the repository and every Git worktree. Suggested layout:

```
/workspace/work/boggle-1997/
  originals/cd/
  originals/install/
  extracted/
  analysis/
  captures/
```

Preserve originals read-only and do analysis on copies. A directory being
ignored is not sufficient: game files must never enter the repository at all.
Existing ignore rules cover some executable extensions, not all asset types.
Point tools, runtime library paths, save paths and temporary outputs at external
directories. Never use a broad `git add .`; stage reviewed source/docs paths
explicitly. Before each commit and push inspect the complete changed-file list
and staged diff for retail content. CI must never download the Drive folder or
publish game-derived captures/logs as artifacts.

Commit only original code, independently authored specs, reviewed metadata
(paths, sizes, hashes, counts and binary addresses), and synthetic fixtures.
Never commit the retail word list or sampled word-list contents. Each confirmed
fact needs a source file SHA-256, file offset or RVA/image base as appropriate,
analysis method and reproduction steps. Separate hypotheses from confirmed
facts and explain conflicting evidence.

## Milestones and acceptance criteria

### 1. Inventory the install and CD; choose the runtime boundary

Create separate recursive manifests with relative paths, sizes and full-file
SHA-256 hashes; compare identical, changed and missing files. Inspect headers,
executable type/architecture, imports, resources, archive signatures and any
runtime dependencies. Identify which files are installer-only and which are
needed at runtime, including dictionaries, fonts, animations and media if present.
Look for registry/CD dependencies and resources embedded in executables.

Use the existing `reluva-tool pe-info <file>` on PE files, and `hexdump`,
`words` and `table` for data inspection. The shared `chunks` command tests a
particular chunk layout; its output is a hypothesis, not a universal parser.
Use Ghidra to follow file-open calls, resource lookup, dictionary lookup,
scoring, board creation and timing as needed. Do not assume an installer
format or select Wise extraction simply because Reluva already supports it.

Run the original in a separate suitable Windows reference environment once
available. Record screens, actual modes/options, controls, round transitions,
timing, scoring, dictionary behavior and any saves/network features. Keep these
captures private and external. If it cannot run, document that validation gap.

Deliverable: `docs/engines/boggle/REVERSE_ENGINEERING.md`, an inventory summary
without payloads, a source ledger and `UNCONFIRMED.md`. Decide whether the engine
is native C++ logic over retail resources or an interpreter for a discovered
script/bytecode format. Prefer original native logic if behavior is in the EXE.
The existing x86 host is limited to DLL-style calls and lacks general Windows
window/file/thread services; it is not evidence that Boggle's EXE can run there.
Using retail machine code would need an explicit architecture review against
the goal of replacing the executable with our own code. Gate: identify the
minimum runtime file set and justify the chosen boundary before implementation.

### 2. Generate the engine and establish detection

Use provisional engine ID `boggle` and game ID `boggle1997`; the engine ID does
not assert a shared engine family with other PCA/Third-i games. Run:

```sh
cmake -DENGINE=boggle -DTITLE="Boggle (1997)" -P cmake/NewEngine.cmake
```

The generator refuses an existing `docs/engines/boggle/` directory. If research
docs already exist, temporarily move that new directory outside the worktree,
generate, then reconcile the generated README with the research docs. Keep this
plan under `docs/plans/` until generation to avoid that collision.

The generator creates engine/tool/test files and adds `boggle=enabled` to
`engines.cfg`; registration and CI discovery are automatic. No hand-written
launcher registration or new top-level CMake entry is needed. Start with an
empty detection table; never invent hashes or enable filename-only recognition.
Add measured variants using `reluva --hash <files...>`: MD5 of the first 5000
bytes plus exact size, distinct from full-file research SHA-256. Pair an EXE
signature with a required data file where appropriate to avoid detecting an
incomplete install. Do not assume English or Windows variant metadata until
confirmed. Preserve unsupported/corrupt-install diagnostics.

Document the actual status in the generated README and add entries to both
repository documentation tables. Gate: the template builds, its initial suites
pass and detection matches only observed complete variants. A blank template
screen is scaffolding, not playable Boggle.

### 3. Read resources and establish Add Game / Import Game

Implement bounded readers for observed formats, with explicit byte order,
offset/length validation, overflow checks and malformed-input errors. Add one
format document and synthetic unit-test suite per format. Survey every supplied
file of a supported type, not just the first successful sample. Keep proprietary
formats in `src/engines/boggle/`; add genuinely reusable work to `src/common/`
only when justified. Never include SDL headers in engine code.

Reuse case-insensitive filesystem/archive helpers, image decoding, audio decoding
and the mixer where formats match. Existing support includes BMP/PNG/JPEG/TGA,
WAV/MP3 and Bink; this does not establish that Boggle uses any of them. Inspect
and prioritize the actual formats before adding decoders. If assets are embedded
in the EXE, read them as resources without executing it.

Implement `MetaEngine::planImport` only after required files and disc/install
layouts are known. Copy only the supported runtime set into the user's external
library; never install bundled system components or run the original installer.
If runtime resources require retaining a retail EXE, document that it supplies
data and remains in the user's copy. Gate: Add Game on the install and Import
Game from each supported source resolve equivalent runtime content; missing
media and unsupported packaging produce clear errors.

### 4. Reimplement one complete round, then the remaining observed features

Separate resource readers, game rules/state and UI/audio. Record and implement
the actual edition's board layouts, dice/faces, randomization, adjacency,
tile reuse, multi-letter tiles, input normalization, minimum word length,
duplicates, dictionary membership, scoring, timer/focus/pause behavior and
results. These are investigation topics, not assumed rules from tabletop Boggle.
Keep board generation, dictionary lookup and scoring independently testable.

Use the installed dictionary through an original reader. Use a tiny invented
dictionary for repository tests. Provide injectable clocks/seeds and deterministic
input replays for validation; use ordinary advancing clocks/seeds during play.
Separate matching a fixed board from reproducing the original random generator;
only promise seed-identical generation when its algorithm has been established.

First playable gate: supported install -> menu -> round -> word entry and
validation -> timeout -> scoring/results -> another round or launcher, with
working input and required feedback. Compare identical boards/actions with the
original, including boundary cases. Then add every remaining mode, media,
settings, persistence and multiplayer feature actually found. Track deferred
features and deliberate deviations in `KNOWN_DIFFERENCES.md` and `TESTING.md`;
do not label the edition fully supported while material features remain missing.

### 5. Regression coverage and release readiness

Register `boggle.unit.<topic>` and `boggle.engine` using
`reluva_add_engine_tests`, with all suites listed in `TOPICS`. Expand the
synthetic engine test from detection into resource loading, input, state
transitions and deterministic output records. Review record updates and prove
an intentional behavior change makes the relevant check fail before reverting it.

Register optional `boggle.gamedata.<step>` tests with
`reluva_add_gamedata_test`, `GAME boggle1997`, `ENV RELUVA_BOGGLE1997` and the
shared helpers. Suggested steps, once implemented: release, resources,
dictionary and round. Missing local data must skip cleanly. Commit only reviewed
counts/checksums/summary records, with raw outputs outside the repository.
The shared helper defaults raw output to the build tree: for this task adapt
the Boggle registration/script to direct it to an external analysis directory.

Run ordinary CTest (including `tests.coverage`), the Boggle label and a
Boggle-only engine configuration with other optional engines disabled. Check
the disabled-engine build too. Existing CI discovers engines from `engines.cfg`
and tests Linux, Windows and macOS; confirm those jobs pass rather than claiming
cross-platform support from a Linux build. Verify high-DPI scaling, text/mouse
input, audio shutdown, return-to-launcher, read-only install and external saves.
Use the render check if backend/rendering code changes justify it.

Final gate: the supplied release completes its observed feature set, repeatable
tests pass, provenance is recorded, remaining compatibility limits are accurate,
and a source-only diff contains no retail content. Changes remain exclusively
on the Boggle branch; this plan never authorizes merging into another branch.

## Immediate next action

Stage 1 static inspection is recorded in [the inspection report](boggle-1997/inspection.md).
Drive access and dictionary research are resolved. Classic Original/Master,
30-second/untimed options and isolated user-word persistence are implemented,
with owner VM rule checks and Windows smoke/workflow tests documented in the
engine pages. Classic lid clips and sound effects are now integrated; their
presentation has owner Windows audio and VM cue corroboration. Import Game now
copies a tested 39-file English Classic set from the install or expanded CD; see
[import scope](../engines/boggle/import.md). Current gaps include original
frontend menus, remaining media formats, other modes and AI/networking. See [current support](../engines/boggle/README.md) and
[remaining evidence gaps](boggle-1997/unconfirmed.md) before choosing the next slice.
