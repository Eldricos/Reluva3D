# LEGO Alpha Team: plan

How the game came to be run: the ways weighed, the choice, and B1's order
of work. B1, the way chosen, runs all the game's own code, NetImmerse
included, in the x86 interpreter, with Windows and DirectX stood in for
([below](#b1-run-all-the-original-code-in-the-x86-interpreter-netimmerse-included-stand-in-for-windows-and-directx)). How the program is run now is in
[systems/running.md](systems/running.md), with what the game's code is and
calls, which the options below were weighed against.

## Goal

None written down yet.

## Phases

B1's order of work:

1. `.pac` reader and the viewer's images, fonts and puzzles
   ([data-viewer.md](systems/data-viewer.md)).
2. Load `LEGOATeam.exe` and every DLL in the interpreter, with stand-ins
   for the libraries listed in
   [running.md](systems/running.md#what-the-games-code-is-and-calls). Then let `DDPackedDirFileLib`'s loader pull a
   behaviour DLL out of `Bhvr.pac` (each puzzle part's own code is a DLL
   there, named after the part: `T0001.DLL`; the interface's is
   `PE0300.DLL`). `reluva-tool x86-run` shows how far it
   gets.
3. MSVCRT, MSVCP60, C++ exceptions and the extra thread, until the game
   reaches its first `DirectDrawCreateEx`.
4. DirectDraw/Direct3D 7: the menus first, then a puzzle. Done in
   software first (see [directx.md](../../../../common/x86/directx.md)); `gfx::Renderer3D` can come later, for
   speed.
5. DirectInput on `System` events, then A3D on the mixer.
6. Speed: an instruction cache, then native NiMain hot spots, measured
   against a puzzle's frame time.

## Decisions

### The options

#### A. Reimplement the game natively

Pros:
- Fastest at run time, and the most portable (no interpreter).
- Free to improve: widescreen, higher resolutions, modern input.
- The data side is needed anyway ([the data viewer](systems/data-viewer.md)), so it isn't wasted.

Cons:
- 5.5 MB of compiled C++ to rewrite: 74 puzzle parts' behaviours, plus
  the rules, physics and menus in the four `*Comp.dll`.
- A contraption puzzle only works if every part behaves exactly as the
  original's did (timings, momentum, electricity). Small differences
  break the shipped puzzles' solutions.
- Needs a complete native NetImmerse too: scene graph, animation,
  collision, NIF loading.
- By far the most work, with the least certain result.

#### B1. Run all the original code in the x86 interpreter, NetImmerse included; stand in for Windows and DirectX

Pros:
- Faithful by construction. Every behaviour DLL, NetImmerse's own physics,
  collision and animation run as shipped. The `_SOLVED.PUZ` files give a
  ready test: load one, run it, see it solve.
- The Windows surface is small and fully documented: about 170 plain
  functions plus three COM areas (DirectDraw/Direct3D 7, DirectInput,
  A3D). Elite Force's Win32 process and PE loader already cover the
  loading, relocation, imports and much of KERNEL32.
- Every interface it implements is public (Microsoft's API docs, the
  DirectX 7 SDK docs, Aureal's A3D SDK docs). No NetImmerse internals
  need to be guessed.
- The work carries over to any later LEGO game built the same way, and to
  other Win9x/DirectX 7 games.
- It can become native piece by piece. Any export of `NiMain.dll` (matrix
  maths, bounds, culling) can later be swapped for a native function where
  profiling shows the time goes.

Cons:
- Speed. The interpreter runs about 50 million instructions a second.
  The game asked for a Pentium II 233, roughly 4 to 6 times that, and
  here NetImmerse's per-frame scene-graph work runs interpreted too. It
  needs a decoded-instruction cache (already noted in [cpu.md](../../../../common/x86/cpu.md) as the next
  speed-up) or a simple JIT, plus native replacements for hot NiMain
  exports.
- MSVCP60 and MSVCRT aren't in the install. MSVCRT can be stood in for
  natively (C functions). MSVCP60 is C++ with object layouts
  (`std::string`, streams) that must match VC6's. That could be a native
  stand-in, or a small replacement DLL built for x86 with clang, like the
  tests' DLL, and run in the interpreter.
- New interpreter features:
  - C++ exceptions, i.e. structured exception handling dispatch, which is
    listed as a known limit today;
  - one extra thread (`_beginthread`), run cooperatively;
  - COM objects (vtables of native functions);
  - `GetVolumeInformationA` answering the CD check.
- Direct3D 7's fixed-function pipeline (render states, texture stages,
  vertex formats) must be mapped onto `gfx::Renderer3D`, which may need a
  few additions.

#### B2. Run only the game's code in the interpreter, with a native NetImmerse

Pros:
- NetImmerse's heavy per-frame work runs natively, so speed isn't a worry.
- Little of Windows or DirectX is needed: just what the game's own
  modules call.

Cons:
- The boundary is C++, not C. The game creates 42 NetImmerse classes
  itself, subclasses them through their vtables and reads 95 of their
  static data items. Its compiled code also reads and writes their fields
  directly through inlined accessors. A native NetImmerse would need every
  class's binary layout and vtable order to match the original exactly,
  with nothing to check against but the binaries.
- 792 functions to reimplement with matching behaviour, including NIF
  streaming (the game registers its own loaders with `NiStream`) and
  NetImmerse's collision and animation, which the puzzles' physics depend
  on.
- It amounts to rewriting NetImmerse to be binary-compatible: more work
  than B1 and less faithful, while still needing the interpreter.

### The decision

**Decision: B1.** It's the only option whose hard parts are all documented
public interfaces, it keeps the puzzles' physics exact, and the
`_SOLVED.PUZ` files give an automatic check. Its main risk, speed, has
known remedies that can be measured as the work goes. The data viewer's native
readers ([data-viewer.md](systems/data-viewer.md)) (`.pac`, NIF, TGA, fonts) are still worth having, for tools and
the viewer, and as the start of native replacements later.
