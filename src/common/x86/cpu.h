// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::x86 {

// Memory straight from the system (mmap, VirtualAlloc), which gives it
// zeroed, a page at a time as it's first touched.
void* allocatePages(size_t bytes);
void freePages(void* p, size_t bytes);

// An allocator of such memory: elements made without a value are left as
// the zeros they already are, so a large memory costs only what's used.
template <typename T>
struct PageAllocator {
    using value_type = T;
    PageAllocator() = default;
    template <typename U>
    PageAllocator(const PageAllocator<U>&) {}
    T* allocate(size_t n) {
        if (void* p = allocatePages(n * sizeof(T)))
            return static_cast<T*>(p);
        throw std::bad_alloc();
    }
    void deallocate(T* p, size_t n) { freePages(p, n * sizeof(T)); }
    template <typename U>
    void construct(U*) noexcept {} // zero already
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
    template <typename U>
    bool operator==(const PageAllocator<U>&) const { return true; }
};
using Memory = std::vector<uint8_t, PageAllocator<uint8_t>>;

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
    Memory& memory() { return memory_; }
    uint32_t memorySize() const { return uint32_t(memory_.size()); }
    RELUVA_FORCE_INLINE bool valid(uint32_t address, uint32_t size) const {
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
    // The x87 unit: its eight registers as a stack (st(i) is regs[(top + i) & 7]),
    // kept as doubles: Windows programs run it at 53-bit precision.
    struct Fpu {
        std::array<double, 8> regs{};
        uint8_t top = 0;
        uint8_t tags = 0xff; // a bit set for each empty register
        uint16_t control = 0x027f, status = 0;
    } fpu;
    // A thread's registers, flags, x87 unit and FS base.
    struct Context {
        std::array<uint32_t, 8> reg{};
        uint32_t eip = 0, eflags = 0x202, fsBase = 0;
        Fpu fpu;
    };

    // --- Native code ---------------------------------------------------------
    // A C++ function run when the emulated code calls `address()` of it:
    // it reads arguments with arg(), sets reg[EAX] (and EDX, or pushes onto
    // the FPU stack) for its result, and returns how many bytes of
    // arguments to pop on return (stdcall), 0 for cdecl.
    using Native = std::function<uint32_t(Cpu& cpu)>;
    // The address a native function is called at (in a reserved range at
    // the top of memory, which holds HLT instructions).
    uint32_t addNative(std::string name, Native fn);
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
    // Asks a run on another thread to stop: it ends with an error ("stopped")
    // at the next instruction. Safe to call from any thread.
    void requestStop() { stop_.store(true, std::memory_order_relaxed); }
    // Whether instructions run through the decoded-instruction cache (on by
    // default; off runs each from its bytes, as a check).
    bool cacheOn = true;
    // Stops a run with an error after this many more instructions (0: never).
    void setInstructionLimit(uint64_t limit) { limit_ = limit; }
    uint64_t executed() const { return executed_; }
    // The thread running, for switching between the program's threads.
    Context context() const;
    void setContext(const Context& c);
    // Runs from the current state (a thread switched in) until a native
    // function yields (yield()), or the run fails.
    Error resume();
    // Ends the run resume() began once the native function running returns,
    // with eip and esp as if it had returned.
    void yield() { yield_ = true; }
    // Ends the current run with an error once the native function running
    // returns (a fatal error of the program's, say).
    void abort(std::string message) {
        if (abort_.empty())
            abort_ = std::move(message);
    }
    // Called before each instruction, and before each native function is
    // entered (eip is then its address); for tracing.
    std::function<void(Cpu& cpu)> trace;
    // What RDTSC reads; without it, the instructions run so far.
    std::function<uint64_t()> timeStamp;

    // The registers as they were when the last call() failed (before they
    // were put back), for reports.
    std::array<uint32_t, 8> faultReg{};
    uint32_t faultEip = 0;

    // The stack: the emulated code's, which call() uses; set before the
    // first call (it grows down from `top`).
    void setStack(uint32_t top) { reg[ESP] = top; }

private:
    struct Fault {
        std::string message;
    };
    [[noreturn]] void fault(const std::string& message);
    [[noreturn]] void memoryFault(uint32_t address, uint32_t size);
    void step();
    void execute();
    void runUntil(uint32_t returnAddress);
    void runCached(uint32_t returnAddress, uint32_t nativeStart);
    void callNative(uint32_t address);

    // Memory as the instructions see it (faulting).
    uint8_t* at(uint32_t a, uint32_t size);
    uint32_t load(uint32_t a, int size);
    void store(uint32_t a, uint32_t v, int size);
    void push(uint32_t v);
    uint32_t pop();
    uint32_t fetch8();
    uint32_t fetch16();
    uint32_t fetch32();

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
    RELUVA_FORCE_INLINE double& st(int i) { return fpu.regs[size_t((fpu.top + i) & 7)]; }
    void fpuCompare(double a, double b, bool setEflags, bool unordered);
    double fpuRound(double v) const;

    Memory memory_;
    struct NativeEntry {
        std::string name;
        Native fn;
    };
    std::vector<NativeEntry> natives_;
    uint32_t nativeBase_ = 0; // natives are at nativeBase_ + 16 * index
    static constexpr uint32_t kNativeStride = 16, kMaxNatives = 4096;
    uint32_t instructionStart_ = 0;
    uint64_t executed_ = 0, limit_ = 0, limitAt_ = 0;
    std::atomic<bool> stop_{false};
    // The decoded-instruction cache (cpu_cache.cpp): blocks of
    // instructions, direct-mapped by their first's address.
    friend struct CachedOps;
    struct Cached {
        using Handler = void (*)(Cpu&, const Cached&);
        Handler run = nullptr;
        uint32_t disp = 0, imm = 0; // the memory operand's displacement; the immediate
        uint8_t length = 0;         // its bytes' count (0: run from them each time)
        uint8_t reg = 0, rm = 0, sub = 0; // ModRM's reg field, a register operand, the operation
        int8_t base = -1, index = -1;     // the memory operand's registers (-1: none)
        uint8_t scale = 0;
        bool mem = false, fs = false; // a memory operand; FS's base added
        bool ends = false;            // it may go elsewhere (or runs from its bytes): its block's last
    };
    // Instructions run one after another: those up to the first that may go
    // elsewhere. Valid while its bytes are still there (code, the first
    // `words` 8-byte words from start, the last masked to `bytes`).
    static constexpr int kBlockOps = 16, kBlockWords = 8;
    struct Block {
        uint32_t start = 0xffffffffu;
        uint8_t count = 0, bytes = 0, words = 0;
        uint64_t lastMask = 0;
        uint64_t code[kBlockWords]{};
        Cached ops[kBlockOps];
    };
    static constexpr uint32_t kBlockEntries = 1u << 15;
    std::vector<Block> blocks_;
    void decode(Cached& e, uint32_t at);
    void build(Block& b, uint32_t at);
    // The block running, counted whole before it ran, and where it began.
    struct {
        const Block* block = nullptr;
        uint32_t start = 0;
    } counting_;
    void uncountBlock(); // takes back the count of its instructions not run
    int depth_ = 0;
    std::string abort_;
    bool yield_ = false;
    struct Yield {};
};

// The hot paths of running instructions, inline.

RELUVA_FORCE_INLINE uint8_t* Cpu::at(uint32_t a, uint32_t size) {
    if (!valid(a, size)) [[unlikely]]
        memoryFault(a, size);
    return memory_.data() + a;
}

RELUVA_FORCE_INLINE uint32_t Cpu::load(uint32_t a, int size) {
    const uint8_t* p = at(a, uint32_t(size));
    if (size == 1)
        return p[0];
    if (size == 2)
        return uint32_t(p[0] | p[1] << 8);
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

RELUVA_FORCE_INLINE void Cpu::store(uint32_t a, uint32_t v, int size) {
    uint8_t* p = at(a, uint32_t(size));
    if (size == 1)
        p[0] = uint8_t(v);
    else if (size == 2) {
        p[0] = uint8_t(v);
        p[1] = uint8_t(v >> 8);
    } else
        std::memcpy(p, &v, 4);
}

RELUVA_FORCE_INLINE void Cpu::push(uint32_t v) {
    reg[ESP] -= 4;
    store(reg[ESP], v, 4);
}

RELUVA_FORCE_INLINE uint32_t Cpu::pop() {
    const uint32_t v = load(reg[ESP], 4);
    reg[ESP] += 4;
    return v;
}

// The x87 stack (fpu.cpp has the rest of the unit).
RELUVA_FORCE_INLINE void Cpu::fpuPush(double v) {
    fpu.top = uint8_t((fpu.top - 1) & 7);
    fpu.regs[fpu.top] = v;
    fpu.tags &= uint8_t(~(1u << fpu.top));
}

RELUVA_FORCE_INLINE double Cpu::fpuPop() {
    const double v = fpu.regs[fpu.top];
    fpu.tags |= uint8_t(1u << fpu.top);
    fpu.top = uint8_t((fpu.top + 1) & 7);
    return v;
}

RELUVA_FORCE_INLINE double Cpu::fpuRound(double v) const {
    // Precision control 0: 24 bits (a game may set it for speed).
    return (fpu.control & 0x300) == 0 ? double(float(v)) : v;
}

inline uint32_t Cpu::fetch8() { return load(eip++, 1); }
inline uint32_t Cpu::fetch16() {
    const uint32_t v = load(eip, 2);
    eip += 2;
    return v;
}
inline uint32_t Cpu::fetch32() {
    const uint32_t v = load(eip, 4);
    eip += 4;
    return v;
}

RELUVA_FORCE_INLINE uint32_t Cpu::readReg(int size, int r) const {
    if (size == 4)
        return reg[size_t(r)];
    if (size == 2)
        return reg[size_t(r)] & 0xffff;
    return r < 4 ? reg[size_t(r)] & 0xff : (reg[size_t(r - 4)] >> 8) & 0xff;
}

RELUVA_FORCE_INLINE void Cpu::writeReg(int size, int r, uint32_t v) {
    if (size == 4)
        reg[size_t(r)] = v;
    else if (size == 2)
        reg[size_t(r)] = (reg[size_t(r)] & 0xffff0000u) | (v & 0xffff);
    else if (r < 4)
        reg[size_t(r)] = (reg[size_t(r)] & 0xffffff00u) | (v & 0xff);
    else
        reg[size_t(r - 4)] = (reg[size_t(r - 4)] & 0xffff00ffu) | (v & 0xff) << 8;
}

inline void Cpu::decodeModrm() {
    const uint32_t m = fetch8();
    d_.hasModrm = true;
    d_.mod = uint8_t(m >> 6);
    d_.regField = uint8_t((m >> 3) & 7);
    d_.rm = uint8_t(m & 7);
    d_.memoryOperand = d_.mod != 3;
    if (!d_.memoryOperand)
        return;
    uint32_t a = 0;
    if (d_.rm == 4) {
        const uint32_t sib = fetch8();
        const uint32_t scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
        if (index != 4)
            a += reg[index] << scale;
        if (base == 5 && d_.mod == 0)
            a += fetch32();
        else
            a += reg[base];
    } else if (d_.rm == 5 && d_.mod == 0) {
        a = fetch32();
    } else {
        a = reg[d_.rm];
    }
    if (d_.mod == 1)
        a += uint32_t(int32_t(int8_t(fetch8())));
    else if (d_.mod == 2)
        a += fetch32();
    d_.address = a + d_.segment;
}

inline uint32_t Cpu::readRm(int size) {
    return d_.memoryOperand ? load(d_.address, size) : readReg(size, d_.rm);
}

inline void Cpu::writeRm(int size, uint32_t v) {
    if (d_.memoryOperand)
        store(d_.address, v, size);
    else
        writeReg(size, d_.rm, v);
}


} // namespace reluva::x86
