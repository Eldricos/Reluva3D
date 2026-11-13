# The x86 processor

## What it is

`src/emulation/x86/cpu.{h,cpp}`, `fpu.cpp` and `cpu_cache.cpp`: the
emulated processor, its x87 unit, the native functions programs call into,
and the cache of decoded instructions that runs them quickly. Native code
for the cache's blocks is [jit.md](jit.md).

## What Reluva supports

- **`cpu.h`, `cpu.cpp`: the processor.** The 32-bit integer instructions
  (with 16-bit operands), string instructions with `rep`, the bit
  instructions, `CPUID` (it reports a Pentium; see below) and the FS segment
  (Windows' thread information block). The segment registers hold their
  selectors and are pushed, popped and moved, but the model is flat. Its memory is one flat arena from
  address 0 whose first page faults, so null pointers are caught. Faults
  (bad memory, division by zero, an instruction not implemented) end the
  call with an error naming the address.
- **`fpu.cpp`: the x87 floating point unit.** Its registers are doubles:
  Windows runs the x87 at 53-bit precision, at which its results are a
  double's. The 80-bit format is converted where it's loaded or stored.
  24-bit precision is followed if a program sets it (or Direct3D 7 or 8
  does for it): add, subtract, multiply, divide and square root are
  rounded to 24 bits of mantissa, the transcendental instructions aren't,
  as on the processor (Intel SDM vol. 1, 8.1.5.2).
  - A 64-bit integer loaded with FILD is kept exactly beside its double,
    and FISTP stores it back unchanged. Programs of the time copy memory
    8 bytes at a time that way (Need for Speed III's `memcpy` does), and
    a double can't hold every 64-bit integer. FSAVE and FRSTOR keep such
    integers exact too; FLD, FST and FXCH of a register move it with the
    register. In the cache's native code (jit.md) those three are the
    host's own instructions, which don't: a program that needs it sets
    `Cpu::exactX87`, and they're left to their handlers.
- **`mmx.cpp`: the MMX instructions**, those of the Pentium MMX, from
  Intel's reference:
  - MOVD and MOVQ in all their forms;
  - PADD and PSUB (bytes, words, dwords; signed and unsigned
    saturating for bytes and words);
  - PMULLW, PMULHW, PMADDWD;
  - PCMPEQ and PCMPGT; PAND, PANDN, POR, PXOR;
  - PSLL, PSRL, PSRA by a register, memory or an immediate; a count past
    the lane's width gives 0, or the sign for PSRA;
  - PACKSSWB, PACKSSDW, PACKUSWB; PUNPCKL and PUNPCKH (BW, WD, DQ);
    the low unpacks read 4 bytes of memory, the others 8;
  - EMMS.

  The SSE forms (a 66, F3 or F2 prefix) fault, as do the opcodes in
  those ranges the Pentium MMX lacks. As on the hardware, MMi is the
  64-bit mantissa of x87 register i (physical, not stack-relative).
  Every MMX instruction but EMMS sets the stack top to 0 and tags all
  eight registers valid; EMMS tags them all empty. A register an MMX
  instruction writes holds a NaN for the x87 (its exponent and sign all
  ones, which FSTP m80 and FSAVE store), and its 64 bits are kept beside
  the double (`Fpu::mm`). They hold while the double is unchanged; once
  the x87 writes the register, MMX reads the mantissa of its value (an
  exact FILD integer's whole). FLD m80 and FRSTOR keep the whole
  mantissa too; FXCH moves it with its register. The MMX registers are
  part of the thread's context. They're decoded in `twoByte` (0F 60-7F
  and D0-FF go to `mmx()`); the cache runs them from their bytes.
  Checked on a program too: `reluva-tool nfs mmx-check` runs Need for
  Speed III's MMX sound mixer beside its float one on the same voices.
  Every difference was one its code predicts: the 127/128 gain, rounding
  down, even samples, the bus saturating. One of them rests on PUNPCKLBW
  keeping the destination's low bytes: the game's 8-bit mixer takes its
  words' low bytes from there.
- **CPUID** reports a Pentium (family 5, model 4) with an FPU, a time
  stamp counter and CMPXCHG8B. It reports MMX (EDX bit 23) only when
  `Cpu::reportMmx` is set (off by default), so programs keep their
  non-MMX paths unless asked.
- **The first page** faults, but a program that reads low memory to tell
  Windows 95 from NT can be let do so (`Cpu::readableFirstPage`), as
  Windows 95 and 98 allow: its reads give 0, its writes still fault. A
  read there in the cache's native code is run again by the interpreter.
- **Threads switched at any instruction** (`Cpu::setSlice`,
  `saveContext`, `loadContext`): for host threads (windows.md), every
  `slice` instructions the run calls back between two instructions,
  where another thread may take the processor; the context saved holds
  the registers, the x87 and MMX state, the segment selectors and where
  the runs are (their depth, limit and the run loop's own state). After
  such a change the cache finds the block at eip again, as another
  thread may have changed the table.
- **For debugging**: stores into a range watched (`watchStart`,
  `watchEnd`, `onWatchedStore`; instructions then run one at a time), a
  callback before each native function (`nativeTrace`), and how often
  each native was called (`nativeCallCounts`).
- **Native functions.** The engine's functions, and stand-ins for the
  system's, live at reserved addresses at the top of memory; a call there
  runs a C++ function, which reads its arguments off the stack and sets
  the result. They may call back into emulated code.

## How it works

### The decoded-instruction cache

`src/emulation/x86/cpu_cache.cpp`. Instructions run in blocks: from an
address, the instructions up to the first that may go elsewhere (a jump,
call or return, or one run from its bytes that might), at most 16 of
them. Blocks are kept in a direct-mapped table of 32768, by their first
instruction's address. Each instruction in a block has its operands
decoded once (a register, or memory as base, index, scale and
displacement; the immediate) and a handler of its own. Each handler is
specialised by operand size, ALU operation or condition, so flags are
worked out without the general `alu()` switch. Handlers exist for:

- the moves (88–8B, C6/C7, B0–BF, A0–A3), LEA, MOVZX/MOVSX;
- the ALU operations in all their forms (00–3D, 80/81/83), TEST (84/85,
  A8/A9, F6/F7 /0), INC/DEC, IMUL (0F AF, 69, 6B);
- PUSH/POP, LEAVE;
- Jcc, JMP, CALL and RET (and indirect through FF);
- the shifts and rotates, SETcc, CDQ, NOP;
- the commonest x87 ones: FLD/FST/FSTP of floats and doubles, FILD of
  32 bits, the arithmetic on memory and registers, FXCH, FCHS, FABS,
  FLD1, FLDZ, FNSTSW AX.

They also cover the FS and operand-size (66) prefixes. Everything else
runs from its bytes as before (`Cpu::execute`). Those that can't go
elsewhere and whose length is plain (the other x87 instructions, NOT,
NEG, MUL, DIV and IDIV, REP string operations) do so without ending their
block. RDTSC ends its block, as it reads the instruction count (below).

The processor's memory is one block from the system, `Cpu::Memory`: a
`std::vector` with `PageAllocator`, which takes it from `mmap` or
`VirtualAlloc`. Those give it zeroed a page at a time as it's first
touched, and the allocator leaves the zeros unwritten, so a program given
1 GB costs only what it uses.

A block keeps its instructions' bytes and is made again when they're not
what they were. That's checked when it runs, a few 64-bit compares, but
only if code may have changed since its last check (`Cpu::_codeEpoch`
moved on): an instruction wrote to a page holding a cached block (pages
are marked as blocks are made); a native ran that may write memory
directly; or the interpreter was entered again, as the host may have
written memory between runs. Natives' writes through `Cpu::write8/16/32`
and `writeString` are noted as instructions' are, and so are the heap's
(`Arena` zeroing what it gives out); memcpy, memmove and memset note
what they write. The natives called most that write only that way, or
nothing at all, are marked so (`Cpu::setWritesTracked`): those three,
malloc, free, operator new and delete, `_EH_prolog`, and Direct3D's and
A3D's per-frame setters and draws (a draw fills its surface later on the
drawing threads, which never hold code). On Visual Studio's build, the
checks had taken about a sixth of the game's thread (a profile of LEGO
Alpha Team). Then one block run in four still checked, nearly all after
operator new, free and Direct3D's calls; now hardly any do, and the
game's thread takes a tenth less time on GCC's build too.

Whether an address is in memory (`Cpu::valid`, asked by every load and
store) is one compare with the size less the first page kept by itself:
an address below 4096 wraps round to past it.
So code that changes is always run as it now is, however it was written:
by the program, or by a native such as a loader's `memcpy`.

A block's instructions are counted together before it runs (one by one
when the instruction limit is near). Nothing in a block reads the count
before its last instruction, and a fault part way through takes back the
count of those not run (`Cpu::uncountBlock`), so counts are exact and a
run on an instruction clock repeats exactly whatever the blocks. The
ALU's parity flag comes from a 256-entry table.

Natives, and runs with a `trace` callback, go one instruction at a time
as before (`Cpu::step`). `Cpu::cacheOn = false` runs everything from the
bytes. The tests run the instruction tests and the test DLL both ways.
They also run 3000 random sequences of 40 instructions, with operands in
registers and memory, both ways, and compare registers, flags, the x87
unit and memory afterwards.

A program that changes the instructions just after the one changing them
must jump between, as the Pentium's manual asks: a block's bytes are
checked when it starts, so a block that writes into itself runs on as it
was decoded (the test of it in `common.unit.x86_more` has the jump).

## Sources and checks

- **Where the facts come from:** Intel's published instruction set
  reference (the Software Developer's Manual).

- `tests/common/unit.cpp` (`common.unit.x86`) and `tests/common/jit.cpp`
  (`common.unit.jit`, the cache and the JIT on and off) run hand-encoded
  instructions.
- It also runs a DLL built from `tests/common/x86/testdll.c` by
  `tests/common/x86/build_testdll.sh`: clang for the Pentium, x87 only, linked
  by lld-link. Its 20 tests, each run on 12 inputs, must give what the
  same C gives built natively (integer and 64-bit arithmetic, division,
  shifts, sorting, CRC, recursion, function pointers, float arithmetic and
  conversions).
  - The script writes the DLL and the native results to
    `tests/common/data/x86_testdll.h`.
  - The DLL is also loaded a second time, so that it is relocated.
- **Confidence:** published unless marked.

- `tests/common/x86_more.cpp` (`common.unit.x86_more`) checks the exact
  64-bit copies, the segment registers, code written over, the readable
  first page (native code included) and pokes; `tests/common/mmx.cpp`
  (`common.unit.mmx`) each MMX instruction against values worked out
  from Intel's reference: the saturation edges, shifts past the width,
  packs and unpacks, the memory forms, the aliasing with the x87, the
  thread context, and CPUID's MMX bit.

## Used by

- **`idtech3`**: Star Trek: Voyager - Elite Force's single-player DLLs
  ([single-player.md](../../engines/idtech3/games/stvef/systems/single-player.md)).
- **`netimmerse`**: LEGO Alpha Team's whole program
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
- **`awe`**: Murder on the Orient Express's `granny2.dll` and `binkw32.dll`,
  run by its tools to check Reluva's own decoders against them.
- **`nfs`**: Need for Speed III's whole program, with MMX if asked, its
  `memcpy` through the x87 unit, and its threads switched at any
  instruction ([the game's threads](../../engines/nfs/games/nfs3/systems/threads.md)).
