# Need for Speed III: the interpreter's speed before the merge (working note)

A working note from October and November 2026: how the x86 interpreter of the NFS III branch, before it was merged into main's emulator (with its decoded-instruction cache and JIT, docs/common/x86/cpu.md), was made fast enough for the game. Its numbers are that interpreter's; after the merge the game runs on main's, at about the same speed on the same machine.

## Speed of the branch's interpreter

About 70 million instructions a second in a Release build, measured on
Need for Speed III's start-up and in a race. A Debug build is much
slower.

October 2026, profiled with callgrind and by sampling stacks with gdb.
The first measurement was 33 million a second; each change below was
checked against that (CPU seconds for the same 400 million instructions,
12.0 at the start, 5.5 after):

- **Memory accesses and fetches inline** (`cpu.h`). The bounds check is
  one comparison against the arena's size less its first page. The rare
  cases are out of line: low memory read as 0, or a fault, which builds
  a message. Before this, each access was a call whose fault path forced
  a heavy stack frame. Most of the gain came from this.
- **One test per instruction** for what's due every so often: the next
  thread slice and the instruction limit share one counter.
- **`step()` inlined into `runUntil()`**, its one caller. The large
  function's frame is then set up once a run, not once an instruction.
- **Decoded ModRM operands cached** (`decodeModrm`; replaced since, below). Entries are keyed by
  the ModRM byte's address and hold what its bytes say: registers, scale
  and displacement. Each entry also keeps the bytes it was decoded from,
  and is used only while they're unchanged. So code written over, by the
  program or by native code, is never run stale, and nothing has to
  watch for writes.

October 2026, a second round, checked with `reluva-tool nfs host -deterministic`
(PLAN.md): the same end state after 5 billion instructions
from the menus into a race, and the best of several runs' CPU seconds.

- **A change of thread wakes one host thread.** Each emulated thread's
  host thread waited on one condition variable, and every change of
  thread woke all of them (about seven) to find whose turn it was. Their
  wake-ups and the lock they fought over took about 7% of the time. Now
  each thread has its own, and only the next is woken: 18% less CPU time.
- **`step()` inlined with Visual Studio too** (`__forceinline`), which
  ignores the GNU attribute.
- **One `switch` for the opcode.** The runs of opcodes (the ALU group,
  INC and DEC, PUSH and POP, Jcc, MOV with an immediate, x87) were tested
  one by one before the `switch`; they're case labels in it now, so every
  opcode goes through one jump. Prefixes are found with a table rather
  than a chain of tests: 7% less CPU time.
- **Tried, no difference:** ModRM cache entries of 32 bytes with their
  mask (each in one cache line), parity from a table, `decodeModrm`
  inlined.
- **Tried, not kept: whole instructions decoded in blocks.** Each
  instruction's prefixes, opcode, ModRM operand and immediates recorded
  on its first run, in blocks of instructions that follow one another,
  and replayed after that; a block's bytes compared with memory on
  entering it, or not, when nothing could have written code since. It
  gave the same end state, at the same speed: the decoding saved was
  spent waiting for the blocks' records, which miss the processor's
  caches (the race's code made about 330,000 records, against 1 MB of
  L2 per core). A smaller record (20 bytes rather than 48) was slower
  still, from unpacking it. Reading the code's own bytes, as now, keeps
  far less in the caches.

On Windows, one report from a Core i7-6700K with Visual Studio's x64-Release
(RelWithDebInfo, `/Ob1`): 37 million instructions a second in a race, half of
this machine's. Hometown with every opponent and every detail at its highest
took 1.4 to 3.1 million instructions a frame, against about 1.2 million with
the defaults. `CMakeLists.txt` now asks Visual Studio for full inlining
(`/Ob3`) outside Debug, and `CMakePresets.json` gives a true Release and a
clang-cl one. On the same machine and race: 41-47 million a second with
the Release preset, 55-63 with clang-cl (23 to 46 frames a second). Clang (Release) ran the deterministic
race about 5% faster than GCC here.

- **ModRM operands decoded without a cache** (`decodeModrm`, since a
  profile from Windows). The cache above was 1.5 MB, which fit this
  machine's 2 MB L2 but not the 256 KB of a Core i7-6700K, where
  `decodeModrm` took 13% of the time. The operand is now decoded from the
  instruction's next 8 bytes, read at once: the same end state, the same
  speed here (1.6% faster, within noise), and no table to miss in the
  caches. The slow way, a byte at a time, is left for the end of memory.

- **The operand and flag helpers inlined** (`decodeModrm`, `readRm`,
  `writeRm`, `readReg`, `writeReg`, `alu`, `setLogicFlags`,
  `condition`). A profile of Need for Speed III's start-up (callgrind,
  150 million instructions) put about 175 host instructions to each
  emulated one. Nearly all of that was the interpreter, a fifth of it in
  `decodeModrm` and another fifth in `alu` and `writeRm`, each called
  out of line from `step()`. Inlined (with out-of-line copies of
  `decodeModrm` and `writeReg` for `fpu.cpp`), the deterministic race
  start (2.2 billion instructions) ends in the same state 8% sooner:
  59.1-60.0 s became 54.5-55.0 s. A builtin for the parity flag made no
  difference and was left out.

Not taken: translating blocks of code to the host's instructions.
Profiles of the game's loading (`reluva-tool nfs host -hotspots`) are flat. In
start-up, no 16-byte block of code takes more than 6.5% of the
instructions, and that one is the game's timer service, polled. In a
track's load none takes more than 2%. A translator would speed
everything up several times over, but it means machine code for each
host processor, and handling faults, self-modifying code and thread
switches inside translated blocks. That is much larger than the loading
times call for.

Ideas not taken yet: lazy flags (`eflags` is read and written in about 70
places, so a large change for the few percent its computing takes).
