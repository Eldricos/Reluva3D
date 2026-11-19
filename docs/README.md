# Reluva's documentation

The [main README](../README.md) covers Reluva in general: what it is,
requirements, building, running and contributing. The rest is here.

| Folder | What's in it |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Notes for working on Reluva: build options, tests, the render check, shaders, IDEs, troubleshooting |
| [ADDING_AN_ENGINE.md](ADDING_AN_ENGINE.md) | Starting a new engine from the template, and taking it to a game that runs |
| [CODING_GUIDE.md](CODING_GUIDE.md) | How Reluva's code is written: names, braces, spacing, comments |
| [DOCUMENTATION_STANDARD.md](DOCUMENTATION_STANDARD.md) | How the docs are laid out, named and written |
| [COMMIT_STANDARD.md](COMMIT_STANDARD.md) | How commits, branches and pull requests are written |
| [common/](common/) | Formats and components shared by engines, matching `src/common/` |
| [engines/](engines/) | One folder per engine, matching `src/engines/<engine>/` |
| [templates/](templates/README.md) | The templates every page starts from |

## Shared: `common/`

Laid out as `src/common/` is, a page per component:

- **[x86/](common/x86/README.md):** the x86 emulator that runs 32-bit
  Windows game code: [the processor](common/x86/cpu.md),
  [native code for it](common/x86/jit.md), the stand-ins for
  [Windows](common/x86/windows.md),
  [DirectX](common/x86/directx.md) and [Aureal A3D](common/x86/a3d.md),
  [MSVCP60](common/x86/msvcp60.md), and
  [a disassembler](common/x86/disasm.md).
- **[graphics/](common/graphics/):** [the Glide stand-in](common/graphics/glide.md),
  3dfx's Glide 2 and 3 in software and on the GPU, for a game's own Voodoo
  renderer.
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
for every engine ([DOCUMENTATION_STANDARD.md](DOCUMENTATION_STANDARD.md)).

Which engine runs which game, and how far each has got, is in the main
README's [games table](../README.md#games).

## Adding docs

Every page follows [DOCUMENTATION_STANDARD.md](DOCUMENTATION_STANDARD.md):
where docs go, an engine's folder, engines for one game or a family,
shared code, naming, and how sources and confidence are recorded. Every
page starts from a template in [templates/](templates/README.md).
