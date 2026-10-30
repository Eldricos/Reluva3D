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

- [x86.md](common/x86.md): the x86 interpreter that runs 32-bit Windows game
  code (Elite Force's single-player DLLs), and its Windows stand-ins.
- [bink.md](common/bink.md): Bink videos.
- [wise.md](common/wise.md): Wise installers, taken apart without running
  them (Elite Force's patch).

## Engines: `engines/<engine>/`

Everything about one engine lives in its own folder: its README (what it
runs, where it stands, how to use it), the formats of its games' files and
interfaces, and any notes on testing and differences from the original.

| Engine | Games | Docs |
|---|---|---|
| `idtech3` | Star Trek: Voyager - Elite Force (and, later, other Quake III engine games) | [engines/idtech3/](engines/idtech3/README.md) |
| `newworlds` | Star Trek: New Worlds | [engines/newworlds/](engines/newworlds/README.md) |
| `testbed` | A built-in test pattern, no game | — |

## Adding docs

- **One engine's** docs go in `engines/<engine>/`, its file formats in
  `engines/<engine>/formats/` if there are many.
- **Code in `src/common/`** that more than one engine can use is documented
  in `common/`.
- Each fact gets its source: an address in a named binary, a file and its
  hash, or the published source it was read in. See
  [CONTRIBUTING.md](../CONTRIBUTING.md) for what may be used.
