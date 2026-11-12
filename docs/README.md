# Reluva's documentation

The [main README](../README.md) covers Reluva in general: what it is,
requirements, building, running and contributing. The rest is here.

| Folder | What's in it |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Notes for working on Reluva: build options, tests, the render check, shaders, IDEs, troubleshooting |
| [ADDING_AN_ENGINE.md](ADDING_AN_ENGINE.md) | Starting a new engine from the template, and taking it to a game that runs |
| [common/](common/) | Formats and components shared by engines, matching `src/common/` |
| [engines/](engines/) | One folder per engine, matching `src/engines/<engine>/` |

## Shared: `common/`

Laid out as `src/common/` is, a page per component:

- **[x86/](common/x86/README.md):** the x86 emulator that runs 32-bit
  Windows game code: [the processor](common/x86/cpu.md),
  [native code for it](common/x86/jit.md), the stand-ins for
  [Windows](common/x86/windows.md),
  [DirectX](common/x86/directx.md) and [Aureal A3D](common/x86/a3d.md),
  [MSVCP60](common/x86/msvcp60.md), and
  [a disassembler](common/x86/disasm.md).
- **[video/](common/video/):** [Bink](common/video/bink.md),
  [Smacker](common/video/smacker.md),
  [Cinepak](common/video/cinepak.md),
  [AVI files](common/video/avi.md) and
  [Intel Indeo 5](common/video/indeo5.md).
- **[dcl.md](common/dcl.md):** PKWARE DCL ("implode") compression.
- **[wise.md](common/wise.md):** Wise installers, taken apart without
  running them.

## Engines: `engines/<engine>/`

Everything about one engine lives in its own folder, laid out the same way
for every engine (see [Adding docs](#adding-docs)).

| Engine | Games | Docs |
|---|---|---|
| `idtech3` | Star Trek: Voyager - Elite Force (and, later, other Quake III engine games) | [engines/idtech3/](engines/idtech3/README.md) |
| `stharbinger` | Star Trek: Deep Space Nine - Harbinger | [engines/stharbinger/](engines/stharbinger/README.md) |
| `newworlds` | Star Trek: New Worlds | [engines/newworlds/](engines/newworlds/README.md) |
| `stgenerations` | Star Trek: Generations | [engines/stgenerations/](engines/stgenerations/README.md) |
| `awe` | Agatha Christie: Murder on the Orient Express (and, later, AWE's other Agatha Christie games) | [engines/awe/](engines/awe/README.md) |
| `clue` | Clue: Murder at Boddy Mansion (Cluedo: Murder at Blackwell Grange in the UK) | [engines/clue/](engines/clue/README.md) |
| `netimmerse` | LEGO Alpha Team (and, later, other NetImmerse games) | [engines/netimmerse/](engines/netimmerse/README.md) |
| `testbed` | A built-in test pattern, no game | — |

## Adding docs

**One engine's** docs go in `engines/<engine>/`, and only what is about
that engine and its games. Every engine's folder has the same layout:

| Page or folder | What goes in it |
|---|---|
| `README.md` | What it runs (games and releases), where it stands, how to use it, and a table of this folder |
| `KNOWN_DIFFERENCES.md` | Where Reluva knowingly differs from the original: by choice, not yet as the original, additions outside the game |
| `UNCONFIRMED.md` | What hasn't been checked against the running original: read from the code but not seen, inferred, guessed |
| `TESTING.md` | The automated checks, and what to check by hand in a playthrough |
| `REVERSE_ENGINEERING.md` | How the facts were found: the sources, the methods, the tools. An engine ported from another project may also have a `PORTING.md` |
| `formats/` | A page per file format: a file's layout, and how the game reads it |
| `systems/` | A page per part of how the game behaves at run time: scripts, dialogue, the interface, saves, AI, network play |
| `research/` | Working notes that the pages above are built from: address maps, notes read straight off the executable, notes from recordings |

- **The five pages** are made with the engine (`cmake/NewEngine.cmake`)
  and kept even while short.
- **The three folders** are made when they get their first page.
- **A format or a system** gets its own page once there's something to
  say about it; a page that grows too big is split by topic, not kept as
  one long notes file.
- **An engine for a family of games** (one known from the start to be
  meant for more than one game, as `idtech3`, `awe` and `netimmerse` are)
  keeps each game's docs in `games/<game id>/`, laid out as above: the
  five pages and the three folders. The engine's own folder then holds its
  `README.md` (the engine, and a table of its games) and only what holds
  for every game on it, such as the engine's or a middleware's own file
  formats in `formats/`. A page worked out from one game stays in that
  game's folder until another game is found to share it, then moves up.
- **An engine for one game** (`clue`, `stgenerations`, `stharbinger`)
  keeps everything in its own folder, with no `games/`. If a second game
  comes to it, it moves to the layout above.
- **Code in `src/common/`** that more than one engine can use is documented
  in `common/`, in the same folders as its code (`common/video/` for
  `src/common/video/`), a page per component. A common page holds only
  what's true of the component for any engine, under these headings:

  1. `## What it is`: what it is, and where its code is.
  2. `## What Reluva supports`: what it handles, and what it doesn't.
  3. `## The format` (a file format) or `## How it works` (a component),
     when there's a layout or a design to describe.
  4. `## Sources and checks`: where the facts came from, and how the code
     was checked. A game's files or program used as a check is named here.
  5. `## Used by`: each engine that uses it, with a link to the page in
     that engine's docs that says how its games use it.

  How one game uses a component (which of its files, its quirks) goes in
  that engine's docs, not here.
- Each fact gets its source: an address in a named binary, a file and its
  hash, or the published source it was read in. See
  [CONTRIBUTING.md](../CONTRIBUTING.md) for what may be used.
