// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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
// A block's code ends in a jump to the dispatch, which does what
// runCached's loop does between blocks where all is ready: unless the run
// is over (eip at its return address or a native), or asked to stop, it
// finds the next block, and if that's there with its bytes unchanged
// since checked (the code epoch), made in this generation and within the
// instruction limit, counts it and jumps to its code. A native function
// at eip is called from there too (Layout::callNative), and the dispatch
// goes on after it. Anything else goes back to runCached, which sees to it.
//
// The code lives in one buffer of executable memory, which says how to
// unwind through it (C++ exceptions, the faults, pass through it):
// RtlAddFunctionTable on Windows, __register_frame on Linux. Every block's
// code runs in the frame its entry made (rbx the Cpu, r12 the memory, r13
// the code pages' marks, 32 bytes of argument space); the dispatch leaves
// it through that frame's epilogue.
class Jit {
public:
    // Whether this build and system can make native code.
    static bool supported();
    // Whether instructions with flags can be made (Kind::Alu): the host's
    // LAHF in 64-bit mode, which a few of the first x86-64 processors lack.
    static bool flagsOps();

    // Where the Cpu keeps what the code reads and writes.
    struct Layout {
        size_t reg, eip, start, epoch, fsBase, eflags; // offsets into the Cpu
        size_t fpuRegs, fpuTop, fpuTags, fpuControl;   // Cpu::fpu's
        const uint8_t* memory;                 // its memory
        const uint8_t* pages;                  // a byte per page, nonzero where code was cached
        uint32_t span;                         // the memory's size less the first page (Cpu::valid)
        // Throws the fault for an access outside memory: (cpu, address, size).
        void (*memoryFault)(void*, uint32_t, uint32_t);
        // For going from block to block (Jit::compile's dispatch): offsets
        // into the Cpu of the count run, the block counted and its start,
        // the stop flag (a byte), and the run's return address, where
        // natives start and the count's limit (runCached's);
        size_t executed, countingBlock, countingStart, stop, runReturn, runNative, runEnd;
        // Where the natives are (from here up), and what runs one at eip
        // (Cpu::callNative's way), giving whether to go on (0: the run
        // stops for the thread to yield).
        uint32_t nativeBase;
        int (*callNative)(void*);
        // the blocks (Cpu::Block, `blockSize` bytes each, `blockMask` + 1 of
        // them, by (eip ^ eip >> 15) & blockMask).
        const uint8_t* blocks;
        uint32_t blockSize, blockMask;
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
        // With flags (Jit::flagsOps: where the host has LAHF). `alu` is
        // the operation: 0-7 ADD OR ADC SBB AND SUB XOR CMP, 8 TEST, 9
        // INC, 10 DEC; `form` the operands: 0 r/m, reg; 1 reg, r/m; 2 the
        // accumulator, imm; 3 r/m, imm (INC and DEC: r/m).
        Alu,
        // Going elsewhere (a block's last): Jcc on `cc`, and the relative
        // JMP and CALL (to imm past the end), JMP and CALL to r/m, RET imm.
        Jcc,
        Jmp,
        CallRel,
        JmpRm,
        CallRm,
        Ret,
        // The x87 unit on Cpu::fpu's doubles, as the handlers have them
        // (SSE2's double arithmetic, which theirs compiles to; precision
        // control's 24 bits by way of float). `size` 4 or 8 for a memory
        // operand's; `alu` the operation (0 ADD, 1 MUL, 4 SUB, 5 SUBR, 6
        // DIV, 7 DIVR); `cc` 1 to pop after; `rm` st(i)'s i.
        FLoad,     // push the memory operand
        FLoadInt,  // push the memory operand's 32-bit integer
        FLoadSt,   // push st(i)
        FStore,    // the memory operand <- st(0)
        FStoreSt,  // st(i) <- st(0)
        FArith,    // st(0) <- st(0) op the memory operand
        FArithSt0, // st(0) <- st(0) op st(i)
        FArithSti, // st(i) <- st(i) op st(0)
        Fxch,
    };
    struct Op {
        Kind kind = Kind::Call;
        uint8_t size = 4, length = 0;
        uint8_t reg = 0, rm = 0, scale = 0;
        uint8_t alu = 0, form = 0, cc = 0;
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
    // What the dispatch knows of the block in table slot `slot`: where it
    // starts, the code epoch its bytes were last checked in, its code
    // (nullptr: none to run) and its count. The Cpu sets it as it checks,
    // makes and runs blocks; reset() takes all the code away.
    void setEntry(uint32_t slot, uint32_t start, uint32_t epoch, const uint8_t* code, uint32_t count);

private:
    Layout layout_;
    uint8_t* base_ = nullptr;
    size_t size_ = 0, used_ = 0, bodyStart_ = 0;
    uint32_t generation_ = 1;
    const uint8_t* dispatch_ = nullptr; // where each block's code goes at its end
    // The dispatch's index of the table, 16 bytes a slot: a 32nd of the
    // blocks' size, so it stays in the processor's caches where the table
    // doesn't (its first read had been half the dispatch's time).
    struct Entry {
        uint32_t start = 0xffffffffu, epoch = 0, code = 0, count = 0; // code: an offset into the buffer
    };
    std::vector<Entry> index_;
    void (*entry_)(void*, const uint8_t*) = nullptr;
    void* unwind_ = nullptr; // what was registered, to take back
};

} // namespace reluva::x86
