// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>

namespace reluva::x86 {

// Native code for the decoded-instruction cache's blocks (Cpu::runCached),
// on x86-64 Windows and Linux. The commonest instructions that set no
// flags (moves, LEA, PUSH, POP) become the host's own instructions, on the
// emulated registers and memory where the Cpu keeps them; every other is
// a direct call to its handler, as the interpreter's loop would make it.
// Either way an instruction does what its handler does, step for step:
// the registers it writes and in what order, the memory check and the
// note of writes to code pages; so the two agree to the instruction, and a
// fault leaves the same state.
//
// The emulated eip and the instruction's start (for faults) are stored
// only where something reads them: before each handler called, at a
// fault, and at the block's end.
//
// The code lives in one buffer of executable memory, which says how to
// unwind through it (C++ exceptions, the faults, pass through it):
// RtlAddFunctionTable on Windows, __register_frame on Linux. Every block's
// code runs in the frame its entry made (rbx the Cpu, r12 the memory, r13
// the code pages' marks, 32 bytes of argument space) and ends in that
// frame's epilogue.
class Jit {
public:
    // Whether this build and system can make native code.
    static bool supported();

    // Where the Cpu keeps what the code reads and writes.
    struct Layout {
        size_t reg, eip, start, epoch, fsBase; // offsets into the Cpu
        const uint8_t* memory;                 // its memory
        const uint8_t* pages;                  // a byte per page, nonzero where code was cached
        uint32_t span;                         // the memory's size less the first page (Cpu::valid)
        // Throws the fault for an access outside memory: (cpu, address, size).
        void (*memoryFault)(void*, uint32_t, uint32_t);
    };
    explicit Jit(const Layout& layout);
    ~Jit();
    Jit(const Jit&) = delete;
    Jit& operator=(const Jit&) = delete;
    bool ready() const { return base_ != nullptr; }

    // An instruction, as decoded (Cpu::Cached), with what it becomes.
    enum class Kind : uint8_t {
        Call,      // its handler, called
        MovRegRm,  // reg <- r/m (size 1 or 4)
        MovRmReg,  // r/m <- reg
        MovRmImm,  // r/m <- imm
        MovRegImm, // rm <- imm (4)
        Lea,       // reg <- the address
        PushReg,   // push rm
        PushImm,
        PopReg,    // pop rm
    };
    struct Op {
        Kind kind = Kind::Call;
        uint8_t size = 4, length = 0;
        uint8_t reg = 0, rm = 0, scale = 0;
        int8_t base = -1, index = -1;
        bool mem = false, fs = false;
        uint32_t disp = 0, imm = 0;
        uint32_t start = 0;               // its address
        const void* handler = nullptr;    // for Call
        const void* entry = nullptr;      // its Cached, for Call
    };
    // Code for a block of `count` instructions, ending at `end` (where eip
    // goes on if the last doesn't say); nullptr if the buffer is full
    // (reset() makes room).
    const uint8_t* compile(const Op* ops, int count, uint32_t end);
    // Runs code compile() made, for the Cpu.
    void run(void* cpu, const uint8_t* code) const { entry_(cpu, code); }
    // Forgets all the code made (none may be running): the generation moves
    // on, so code kept from an older one is known to be gone.
    void reset();
    uint32_t generation() const { return generation_; }

private:
    Layout layout_;
    uint8_t* base_ = nullptr;
    size_t size_ = 0, used_ = 0, bodyStart_ = 0;
    uint32_t generation_ = 1;
    void (*entry_)(void*, const uint8_t*) = nullptr;
    void* unwind_ = nullptr; // what was registered, to take back
};

} // namespace reluva::x86
