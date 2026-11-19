// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace reluva::x86 {

// One instruction of 32-bit x86 code, in Intel syntax as capstone writes
// it: a lower-case mnemonic ("rep movsd", "lock add") and its operands
// ("dword ptr [ebp - 8], eax", "0x10").
struct Instruction {
    uint32_t address = 0;
    int size = 0;       // in bytes; 1 for one that isn't valid
    bool valid = false; // false: not an instruction (shown as "db")
    std::string mnemonic;
    std::string operands;
    bool hasTarget = false; // a direct call, jmp, jcc, loop or jecxz:
    uint32_t target = 0;    // where it goes
};

// The instruction at `code` (`size` bytes available), as if at `address`:
// 32-bit protected-mode code (operand and address size 32, unless prefixed
// 0x66 or 0x67). Covers the one-byte opcodes, the 0x0F map's general
// instructions (jcc, setcc, cmovcc, movzx/movsx, imul, bt*, shld/shrd,
// bsf/bsr, the system ones), the x87 FPU and the string instructions with
// their prefixes; not MMX, SSE or later extensions (those are invalid).
Instruction disassemble(const uint8_t *code, size_t size, uint32_t address);

} // namespace reluva::x86
