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

- [x86.md](common/x86.md): the x86 emulator (an interpreter, and native
  code on x86-64) that runs 32-bit Windows game code (Elite Force's
  single-player DLLs, LEGO Alpha Team's whole program), and its stand-ins
  for Windows, DirectX and Aureal A3D. Two of them have pages of their
  own: [x86/directx.md](common/x86/directx.md), DirectDraw and Direct3D 7
  in software, and [x86/msvcp60.md](common/x86/msvcp60.md), the C++
  library rebuilt with Visual C++ 6's layouts.
- [bink.md](common/bink.md): Bink videos (Elite Force's cinematics, Murder
  on the Orient Express's movies and video loops).
- [smacker.md](common/smacker.md): Smacker videos.
- [cinepak.md](common/cinepak.md): Cinepak videos, in AVI files.
- [video/avi.md](common/video/avi.md) and
  [video/indeo5.md](common/video/indeo5.md): AVI files, and Intel Indeo
  Video 5 (LEGO Alpha Team's intro movie).
- [dcl.md](common/dcl.md): PKWARE DCL ("implode") compression.
- [wise.md](common/wise.md): Wise installers, taken apart without running
  them (Elite Force's patch).

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
- **Code in `src/common/`** that more than one engine can use is documented
  in `common/`.
- Each fact gets its source: an address in a named binary, a file and its
  hash, or the published source it was read in. See
  [CONTRIBUTING.md](../CONTRIBUTING.md) for what may be used.
