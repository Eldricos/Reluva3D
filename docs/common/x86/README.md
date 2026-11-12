# The x86 emulator

## What it is

Some games' own 32-bit Windows code, DLLs or whole programs, is run as it
shipped rather than reimplemented: in an emulator of the x86 processor,
with stand-ins for the parts of Windows, DirectX and the libraries it
calls. The code stays in the user's copy, on any machine Reluva builds
for. It's new code, written from Intel's published instruction set
reference and Microsoft's published PE/COFF specification and API
documentation. It's all in `src/common/x86/`.

| Page | What it covers |
|---|---|
| [cpu.md](cpu.md) | The processor: the integer instructions, the x87 unit, native functions, and the decoded-instruction cache |
| [jit.md](jit.md) | Native code for the cache's blocks, on x86-64 |
| [windows.md](windows.md) | A Windows process: loading, KERNEL32, files, MSVCRT, USER32 and GDI32, the registry, COM, input, threads |
| [directx.md](directx.md) | DirectDraw, Direct3D 7 (in software) and DirectInput |
| [a3d.md](a3d.md) | Aureal A3D, on Reluva's mixer |
| [msvcp60.md](msvcp60.md) | Visual C++ 6's C++ library, rebuilt with its layouts |
| [disasm.md](disasm.md) | A disassembler, for studying a game's executable |

## What Reluva supports

- Structured exception handling isn't dispatched: `RaiseException` and
  `RtlUnwind` end the run.
- No MMX or SSE (the game DLLs predate their common use).
- The interpreter checks each memory access. Instructions are decoded
  once and kept: see [cpu.md](cpu.md#the-decoded-instruction-cache).

## Sources and checks

Each page says where its facts came from and how it's checked. The tests
are in `tests/common/` (`common.unit.x86`, `common.unit.jit`,
`common.unit.windows`).

- `reluva-tool x86-run <dll> [-call Export args...]` loads a DLL and runs
  its start-up and, if asked, an export. An argument `table` passes a
  table of functions that print each call made to them. It's for seeing
  how far a game's DLL gets and what it asks of the engine. A failure
  prints the addresses of the last instructions run.

## Used by

- **`idtech3`**: Star Trek: Voyager - Elite Force's single-player DLLs
  ([single-player.md](../../engines/idtech3/games/stvef/systems/single-player.md)).
- **`netimmerse`**: LEGO Alpha Team's whole program
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
- **`stharbinger`**: the disassembler, for Harbinger's `DEEP.EXE`
  ([disasm.md](disasm.md)).
- **`awe`**: Murder on the Orient Express's `granny2.dll` and `binkw32.dll`,
  run by its tools to check Reluva's own decoders against them.
