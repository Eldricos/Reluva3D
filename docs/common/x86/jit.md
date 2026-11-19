# Native code for the blocks (JIT)

## What it is

On x86-64 Windows and Linux a block runs as native code made the first
time it runs (`Cpu::compileBlock`, `Jit::compile`), in place of the
loop over its instructions. The rest of the cache is as it was: finding
the block, checking its bytes, counting it, and the loop for blocks run
near the instruction limit.

## What Reluva supports

- Moves of 8 and 32 bits (register, memory, immediate), LEA, PUSH and
  POP become host instructions on the emulated registers (`Cpu::reg`,
  addressed from rbx) and memory (from r12). Each does what its handler
  does in the same order: the address, `Cpu::valid`'s one compare and a
  jump to the fault, the mark of a write to a page holding cached code
  (`_codeEpoch` on), and for PUSH and POP ESP's change where the
  handler makes it.
- ADD, OR, ADC, SBB, AND, SUB, XOR, CMP, TEST, INC and DEC (8 and 32
  bits, every form), and SHL, SHR and SAR of 32 bits by 1 to 31 (OF as
  `Cpu::shift` has it: SF ^ CF, the value's top bit, clear; AF clear), are the host's own instruction on the operands
  loaded as the handler loads them, ADC and SBB taking CF in with BT.
  The flags come from the host's (LAHF and SETO) into EFLAGS: as the
  handlers work them out, which is the processor's definition, but for
  AF, which the logic operations leave undefined and the handlers clear,
  so the made code clears it too. They're worked out only if read: by a
  later instruction of the block before one that sets them all (INC
  and DEC keep CF, so read it, as ADC and SBB do; a handler may read
  any), or after the block. Without LAHF in 64-bit mode (a few of the
  first x86-64 processors) these stay calls.
- Jcc, JMP and CALL (relative, and to a register or memory) and RET are
  made too: the condition tested on EFLAGS as `CachedOps::condition`
  does, eip set one way or the other.
- The x87 unit's loads and stores (float, double, int32, st(i)), FXCH,
  and its arithmetic (ADD, MUL, SUB, SUBR, DIV, DIVR on memory and
  st(i), popping or not) work on `Cpu::fpu`'s doubles with SSE2's
  double arithmetic, which is what the handlers compile to; with
  precision control's 24 bits through float, as `Cpu::fpuRound` does.
  Neither changes the host's rounding (MXCSR), so the results are the
  same to the bit. FCOM and FCOMP (memory and st(i)) set C0, C2 and C3
  from UCOMISD's CF, PF and ZF, as `Cpu::fpuCompare` does (all three
  unordered), and FNSTSW AX is made too; FCOMI and the like stay calls.
- Every other instruction is a direct call to its handler, after
  storing eip and the instruction's start, which the handler and a fault
  read. The made instructions store neither: the block's end stores
  them if its last didn't, and a fault's path stores them as the
  handler would have had them before it throws.
- Faults are C++ exceptions thrown through the native code, so the
  buffer says how to unwind its one frame (rbx, r12, r13 saved, 32 bytes
  below): `RtlAddFunctionTable` on Windows, an `.eh_frame` given to
  `__register_frame` on Linux. Every block's code ends in that frame's
  epilogue.
- The buffer (32 MB, near the program's code so calls take 32-bit
  offsets) is filled in order. When full, all its code is forgotten
  (`Jit::reset`, a new generation, which blocks compare with theirs) and
  made again as blocks run, but only while none is running: a native
  calling back in finds the old still under it (`Cpu::_jitDepth`), and
  runs its blocks through the loop until then.

## How it works

A block's code goes on to the next itself (`Jit` dispatch, in the
buffer): unless the run is over (eip at the run's return address) or
asked to stop, it finds the block for eip, and if that's there with its
bytes checked in this code epoch, made and within the instruction
limit, counts it and jumps to its code. It reads the table's slot from
an index of its own (`Jit::setEntry`: start, epoch, code, count; 16
bytes a slot, 512 KB), which `runCached` keeps as each slot is checked,
built or made, and `Jit::reset` empties: the 512-byte blocks themselves
are a 16 MB table the processor's caches don't hold, and its first read
had been half the dispatch's time, a tenth of the game's thread, in a
profile on Visual Studio's build. A native at eip is called from the
dispatch too (`Cpu::jitCallNative`, as `step` calls it), which goes on
after it unless the native asked the thread to yield; the run that
native may call back into can make the buffer afresh, as the only made
code under it is the dispatch's, which `reset` keeps. Anything else
returns to `runCached`, which checks, builds, makes or counts it as
before.

Where a block goes is mostly known when it's made: straight on, a
jump's or call's target, either way of a Jcc. Each of those exits has a
link (in the Jit's data, apart from the code): the code the dispatch
last found there, and the code epoch then. While the epoch hasn't moved
(no write to a page of cached code since, nor a native that may have
written), the exit checks the stop flag and the instruction limit,
counts the block and jumps straight to its code; else it goes to the
dispatch, which fills the link when it finds the code. For that a
block's code depends on nothing in the table, which another block may
take the place of: it has its own copy of its decoded instructions,
which the handlers it calls are given and the count's record
(`Cpu::_counting`, for `uncountBlock`) points at, in a header before
the code. A profile on Visual Studio's build had the index's lookup a
fourteenth of the game's thread; the links took 9% off the attract
sequence's thread here. A block's code is made on its second run: many
blocks run once before another address takes their place in the table.
The table is two-way: a block is in either slot of a pair, found by
Fibonacci hashing (`Cpu::slotOf`), the one used less lately replaced.
Direct-mapped, a few pairs of hot blocks of LEGO Alpha Team's attract
sequence shared a slot and took each other's place four and a half
million times in 1500 frames, whatever the hash.
A block with code that another takes the place of is kept aside by its
address (`Cpu::_spare`, up to 32768, then all forgotten) and put back,
decoded and made, when its address comes round with the same bytes
(`Cpu::rebuild`): making blocks again had been a fifteenth of the
game's thread in a profile.

## Sources and checks

Elsewhere (and with `Cpu::jitOn = false`, or the environment variable
`RELUVA_NO_JIT` set) the loop runs them. Both run the same handlers or
their exact equivalents, so counts, registers and memory agree to the
instruction. The tests run with and without it, including faults from
made instructions and from handlers, thrown through the made code: they
pass on Linux (GCC, `__register_frame`) and on Windows (Visual Studio
2022, `RtlAddFunctionTable`). On LEGO Alpha Team's M1010 route
the program's thread took 12% less time with moves, LEA, PUSH and POP
made, 26% less than the loop with the ALU, the jumps and the
dispatch too, and 35% less (35 s from 54 s) with the blocks kept aside
and the x87 unit made, with the same instructions and frames at both
resolutions. Calling every instruction's handler from
native code, without making any, had been 5% slower than the loop: the
code is several times the size of the loop's, and the calls' gain was
less than the instruction cache's misses. Next: shifts, the x87
compares, and jumping straight to a block known in advance (a relative
jump's or call's) without the dispatch's lookup.

- **Confidence:** Reluva's own design.

## Used by

- **`idtech3`**: Star Trek: Voyager - Elite Force's single-player DLLs
  ([single-player.md](../../engines/idtech3/games/stvef/systems/single-player.md)).
- **`netimmerse`**: LEGO Alpha Team's whole program
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
- **`awe`**: Murder on the Orient Express's `granny2.dll` and `binkw32.dll`,
  run by its tools to check Reluva's own decoders against them.
