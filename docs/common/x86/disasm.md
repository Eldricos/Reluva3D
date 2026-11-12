# The x86 disassembler

## What it is

`src/common/x86/disasm.h` (`reluva::x86::disassemble`) turns 32-bit x86
code back into text, in Intel syntax as capstone writes it, for studying a
game's executable.

## What Reluva supports

It covers the one-byte opcodes with their ModRM/SIB forms and prefixes,
the 0x0F map's general and system instructions, the x87 FPU and the
string instructions; MMX, SSE and later extensions come out as `db`.

Where its text may differ from capstone's: small immediates and
displacements are decimal (up to 9), a sign-extended byte immediate is
negative for arithmetic and a full mask for and/or/xor, a segment register
moved to memory says `dword ptr`, and register-form FPU instructions name
both operands.

## Sources and checks

- **Where the facts come from:** Intel's published instruction set
  reference.
- **How it's checked:** `common.unit.x86` checks it against hand-checked
  byte sequences.

## Used by

- **`stharbinger`**: `reluva-tool stharbinger exe-disasm`, for Harbinger's
  `DEEP.EXE` ([REVERSE_ENGINEERING.md](../../engines/stharbinger/REVERSE_ENGINEERING.md#the-tools)).
