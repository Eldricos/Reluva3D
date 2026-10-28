// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::x86 {

// A 32-bit x86 processor, interpreted (written from Intel's published
// instruction set reference), for running native game code (Windows DLLs
// built for the Pentium of their day) on any machine: the integer
// instructions, the x87 floating point unit, and the few system ones such
// code uses. Its memory is one flat arena from address 0; the first page
// is left unmapped so that null pointers fault.
//
// Native code (the engine's functions, stand-ins for the system's) runs at
// reserved addresses: a call there runs a C++ function instead, which
// reads its arguments from the stack and sets the return value.
class Cpu {
public:
    enum Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
    // EFLAGS bits.
    static constexpr uint32_t CF = 1u << 0, PF = 1u << 2, AF = 1u << 4, ZF = 1u << 6, SF = 1u << 7, TF = 1u << 8,
                              IF = 1u << 9, DF = 1u << 10, OF = 1u << 11;
    static constexpr uint32_t kPageSize = 4096;

    // `memorySize` bytes of memory (a multiple of 4096).
    explicit Cpu(uint32_t memorySize);

    // --- Memory --------------------------------------------------------------
    std::vector<uint8_t>& memory() { return memory_; } // not to be resized
    uint32_t memorySize() const { return uint32_t(memory_.size()); }
    bool valid(uint32_t address, uint32_t size) const {
        return address >= kPageSize && size <= memory_.size() && address <= memory_.size() - size;
    }
    // Reads and writes for native code (no fault: out of range reads 0,
    // writes are dropped; check valid() first where it matters).
    uint8_t read8(uint32_t a) const { return valid(a, 1) ? memory_[a] : 0; }
    uint16_t read16(uint32_t a) const;
    uint32_t read32(uint32_t a) const;
    void write8(uint32_t a, uint8_t v) {
        if (valid(a, 1))
            memory_[a] = v;
    }
    void write16(uint32_t a, uint16_t v);
    void write32(uint32_t a, uint32_t v);
    std::string string(uint32_t address, size_t max = 4096) const;
    // Copies `text` and a NUL into memory at `address` (at most `size`
    // bytes in all); false if it isn't all in memory.
    bool writeString(uint32_t address, std::string_view text, uint32_t size);

    // --- State ---------------------------------------------------------------
    std::array<uint32_t, 8> reg{};
    uint32_t eip = 0;
    uint32_t eflags = 0x202;
    uint32_t fsBase = 0; // the FS segment's base (Windows' thread information block)
    // The segment registers' selectors, as Windows sets them, in the
    // instruction set's order: ES, CS, SS, DS, FS, GS. The model is flat, so
    // loading one changes nothing but the selector held (FS keeps its base).
    std::array<uint16_t, 6> sreg{0x23, 0x1b, 0x23, 0x23, 0x3b, 0};
    // The x87 unit: its eight registers as a stack (st(i) is regs[(top + i) & 7]),
    // kept as doubles: Windows programs run it at 53-bit precision.
    struct Fpu {
        std::array<double, 8> regs{};
        uint8_t top = 0;
        uint8_t tags = 0xff; // a bit set for each empty register
        uint16_t control = 0x027f, status = 0;
        // A 64-bit integer loaded with FILD, kept exactly alongside its
        // double (which can't hold every one), so that FISTP stores it back
        // unchanged: programs copy memory 8 bytes at a time that way. A
        // bit set in exactValid for each register holding one.
        std::array<int64_t, 8> exact{};
        uint8_t exactValid = 0;
    } fpu;

    // --- Native code ---------------------------------------------------------
    // A C++ function run when the emulated code calls `address()` of it:
    // it reads arguments with arg(), sets reg[EAX] (and EDX, or pushes onto
    // the FPU stack) for its result, and returns how many bytes of
    // arguments to pop on return (stdcall), 0 for cdecl.
    using Native = std::function<uint32_t(Cpu& cpu)>;
    // The address a native function is called at (in a reserved range at
    // the top of memory, which holds HLT instructions).
    uint32_t addNative(std::string name, Native fn);
    // How many times each native function has been called, by name.
    std::vector<std::pair<std::string, uint64_t>> nativeCallCounts() const;
    // The name of the native function at `address`, if it is one.
    const std::string* nativeName(uint32_t address) const;
    // Where the reserved range begins: memory below it is the program's.
    uint32_t nativeBase() const { return nativeBase_ - kNativeStride; }
    // From a native function: its n'th argument (from 0) on the stack.
    uint32_t arg(int n) const { return read32(reg[ESP] + 4 + 4 * uint32_t(n)); }
    // Pushes onto and pops from the FPU stack (float results).
    void fpuPush(double v);
    double fpuPop();

    // --- Running -------------------------------------------------------------
    // Calls the emulated function at `address` with `args` pushed (cdecl
    // or stdcall), returning EAX in `result`; fails on a fault (bad memory,
    // division by zero, an instruction not implemented...), naming where.
    // Native functions may call() back in.
    Error call(uint32_t address, const std::vector<uint32_t>& args, uint32_t& result);
    // Stops a run with an error after this many more instructions (0: never).
    void setInstructionLimit(uint64_t limit) {
        limit_ = limit;
        updateNextEvent();
    }
    uint64_t executed() const { return executed_; }
    // Ends the current run with an error once the native function running
    // returns (a fatal error of the program's, say).
    void abort(std::string message) {
        if (abort_.empty())
            abort_ = std::move(message);
    }
    // Reads in the first page give 0 rather than faulting, as Windows 95
    // and 98 let programs read low memory (writes still fault).
    bool readableFirstPage = false;
    // What RDTSC reads, if set (else the instructions run so far): a host
    // can report a processor of the speed a program expects.
    std::function<uint64_t()> timestampCounter;
    // Called before each instruction, with its address; for tracing.
    std::function<void(Cpu& cpu)> trace;
    // Called when an instruction stores into [watchStart, watchEnd); for
    // finding what changes memory (a debugging aid).
    uint32_t watchStart = 0, watchEnd = 0;
    std::function<void(Cpu& cpu, uint32_t address, uint32_t value, int size)> onWatchedStore;
    // Called before each native function runs, with its name; for logs.
    std::function<void(Cpu& cpu, const std::string& name)> nativeTrace;

    // The registers as they were when the last call() failed (before they
    // were put back), for reports.
    std::array<uint32_t, 8> faultReg{};
    uint32_t faultEip = 0;

    // The stack: the emulated code's, which call() uses; set before the
    // first call (it grows down from `top`).
    void setStack(uint32_t top) { reg[ESP] = top; }

    // --- Threads -------------------------------------------------------------
    // Everything a thread of the program has of its own, for running
    // several on the one processor: each on a host thread of its own, one
    // at a time, the context swapped when they change over (Win32Process).
    struct Context {
        std::array<uint32_t, 8> reg{};
        uint32_t eip = 0, eflags = 0x202, fsBase = 0;
        std::array<uint16_t, 6> sreg{};
        Fpu fpu;
        int depth = 0;
        uint64_t limitAt = 0;
        uint32_t instructionStart = 0;
        std::string abort;
    };
    Context saveContext() const;
    void loadContext(const Context& c);
    // Called every `length` instructions (between two), for switching
    // threads; returning false ends the run with `stopMessage`.
    void setSlice(uint32_t length, std::function<bool(Cpu& cpu)> fn) {
        sliceLength_ = length;
        nextSlice_ = executed_ + length;
        slice_ = std::move(fn);
        updateNextEvent();
    }
    std::string stopMessage = "the run was stopped";

private:
    struct Fault {
        std::string message;
    };
    [[noreturn]] void fault(const std::string& message);
    // One instruction; inlined into runUntil, its one caller, so that its
    // frame is set up once a run rather than once an instruction.
    inline void step();
    // What's due at nextEvent_ instructions: a slice, the limit.
    void events();
    void updateNextEvent() {
        nextEvent_ = UINT64_MAX;
        if (sliceLength_)
            nextEvent_ = nextSlice_;
        if (limit_ && limitAt_ + 1 < nextEvent_)
            nextEvent_ = limitAt_ + 1;
    }
    void runUntil(uint32_t returnAddress);
    void callNative(uint32_t address);

    // Memory as the instructions see it (faulting). Inline: every
    // instruction comes this way several times.
    // valid() for the few bytes an instruction touches (at most 16): one
    // comparison.
    bool validSmall(uint32_t a, uint32_t size) const { return a - kPageSize <= memoryLimit_ - size; }
    uint8_t* at(uint32_t a, uint32_t size) {
        if (!validSmall(a, size)) [[unlikely]]
            outside(a, size);
        return memory_.data() + a;
    }
    uint32_t load(uint32_t a, int size) {
        if (!validSmall(a, uint32_t(size))) [[unlikely]]
            return loadOutside(a, size);
        const uint8_t* p = memory_.data() + a;
        if (size == 1)
            return p[0];
        if (size == 2)
            return uint32_t(p[0] | p[1] << 8);
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
    void store(uint32_t a, uint32_t v, int size) {
        uint8_t* p = at(a, uint32_t(size));
        if (a < watchEnd && a + uint32_t(size) > watchStart && onWatchedStore) [[unlikely]]
            onWatchedStore(*this, a, v, size);
        if (size == 1)
            p[0] = uint8_t(v);
        else if (size == 2) {
            p[0] = uint8_t(v);
            p[1] = uint8_t(v >> 8);
        } else
            std::memcpy(p, &v, 4);
    }
    // The rare cases, out of line: low memory read as 0, or a fault.
    [[noreturn]] void outside(uint32_t a, uint32_t size);
    uint32_t loadOutside(uint32_t a, int size);
    void push(uint32_t v) {
        reg[ESP] -= 4;
        store(reg[ESP], v, 4);
    }
    uint32_t pop() {
        const uint32_t v = load(reg[ESP], 4);
        reg[ESP] += 4;
        return v;
    }
    void pushSegment(int s);
    void popSegment(int s);
    uint32_t fetch8() { return load(eip++, 1); }
    uint32_t fetch16() {
        const uint32_t v = load(eip, 2);
        eip += 2;
        return v;
    }
    uint32_t fetch32() {
        const uint32_t v = load(eip, 4);
        eip += 4;
        return v;
    }

    // The instruction being run: prefixes, and its ModRM operand.
    struct Decoded {
        bool opSize16 = false, rep = false, repne = false;
        uint32_t segment = 0; // a segment override's base (FS)
        bool hasModrm = false;
        uint8_t mod = 0, regField = 0, rm = 0;
        uint32_t address = 0; // of a memory operand
        bool memoryOperand = false;
    } d_;
    void decodeModrm();
    // A ModRM operand's fields, decoded a byte at a time (the slow way, near
    // the end of memory).
    struct ModrmEntry {
        uint8_t mod = 0, regField = 0, rm = 0;
        uint8_t base = 8, index = 8, scale = 0; // 8: no register
        uint32_t displacement = 0;
    };
    void decodeModrmSlow(ModrmEntry& e);
    uint32_t readRm(int size);
    void writeRm(int size, uint32_t v);
    uint32_t readReg(int size, int r) const;
    void writeReg(int size, int r, uint32_t v);

    // Integer operations, setting the flags.
    uint32_t alu(int op, uint32_t a, uint32_t b, int size);
    uint32_t shift(int op, uint32_t v, uint32_t count, int size);
    void setLogicFlags(uint32_t result, int size);
    bool condition(int cc) const;
    void group3(int size);
    void stringOp(uint8_t op);
    void twoByte();
    void x87(uint8_t op);

    // x87 helpers.
    double& st(int i) { return fpu.regs[size_t((fpu.top + i) & 7)]; }
    // Copies st(from)'s exact integer, if it has one, to st(to).
    void copyExact(int to, int from);
    void fpuCompare(double a, double b, bool setEflags, bool unordered);
    double fpuRound(double v) const;

    std::vector<uint8_t> memory_;
    uint32_t memoryLimit_ = 0; // its size less the first page
    struct NativeEntry {
        std::string name;
        Native fn;
        uint64_t calls = 0;
    };
    std::vector<NativeEntry> natives_;
    uint32_t nativeBase_ = 0; // natives are at nativeBase_ + 16 * index
    static constexpr uint32_t kNativeStride = 16, kMaxNatives = 4096;
    uint32_t instructionStart_ = 0;
    uint64_t executed_ = 0, limit_ = 0, limitAt_ = 0;
    int depth_ = 0;
    std::string abort_;
    uint32_t sliceLength_ = 0;
    uint64_t nextSlice_ = 0;      // when the next slice is due (in instructions run)
    uint64_t nextEvent_ = UINT64_MAX; // the first of that and the limit
    std::function<bool(Cpu& cpu)> slice_;
};

} // namespace reluva::x86
