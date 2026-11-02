// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The decoded-instruction cache. Each instruction's address has an entry,
// direct-mapped, made the first time it runs: its operands decoded once
// (the ModRM operand as base, index, scale and displacement; the
// immediate), and a handler that runs it, in blocks that run straight
// through. The instructions programs run
// most (moves, the ALU operations, pushes and pops, jumps, calls and
// returns, LEA, MOVZX/MOVSX, shifts, SETcc) have handlers of their own;
// the rest, and those with prefixes other than FS, run from their bytes
// as before (Cpu::execute); those that can't go elsewhere (x87, NOT, NEG,
// MUL, DIV, REP string operations) without ending their block; RDTSC, which reads the count, ends its own. A block keeps its instructions' bytes and is
// remade when they've changed, however they were written (a program's
// loader copying code with a native memcpy, say).

#include "common/x86/cpu.h"
#include "common/x86/jit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#define RELUVA_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0)
#elif defined(__GNUC__)
#define RELUVA_PREFETCH(p) __builtin_prefetch(p)
#else
#define RELUVA_PREFETCH(p) ((void)(p))
#endif

namespace reluva::x86 {

// PF for each low byte of a result: set when its bits set are even.
struct ParityTable {
    uint8_t flag[256];
    constexpr ParityTable() : flag{} {
        for (int i = 0; i < 256; ++i) {
            int bits = 0;
            for (int v = i; v; v >>= 1)
                bits += v & 1;
            flag[i] = bits % 2 ? 0 : uint8_t(Cpu::PF);
        }
    }
};
constexpr ParityTable kParity;

struct CachedOps {
    using Cached = Cpu::Cached;

    RELUVA_FORCE_INLINE static uint32_t address(const Cpu& c, const Cached& e) {
        uint32_t a = e.disp;
        if (e.base >= 0)
            a += c.reg[size_t(e.base)];
        if (e.index >= 0)
            a += c.reg[size_t(e.index)] << e.scale;
        return e.fs ? a + c.fsBase : a;
    }
    template <int S>
    RELUVA_FORCE_INLINE static uint32_t getReg(const Cpu& c, int r) {
        return S == 4 ? c.reg[size_t(r)] : c.readReg(S, r);
    }
    template <int S>
    RELUVA_FORCE_INLINE static void setReg(Cpu& c, int r, uint32_t v) {
        if (S == 4)
            c.reg[size_t(r)] = v;
        else
            c.writeReg(S, r, v);
    }
    template <int S>
    RELUVA_FORCE_INLINE static uint32_t getRm(Cpu& c, const Cached& e) {
        return e.mem ? c.load(address(c, e), S) : getReg<S>(c, e.rm);
    }
    template <int S>
    RELUVA_FORCE_INLINE static void setRm(Cpu& c, const Cached& e, uint32_t v) {
        if (e.mem)
            c.store(address(c, e), v, S);
        else
            setReg<S>(c, e.rm, v);
    }
    RELUVA_FORCE_INLINE static void next(Cpu& c, const Cached& e) { c.eip += e.length; }

    static void legacy(Cpu& c, const Cached&) { c.execute(); }

    // Cpu::alu and Cpu::condition, for one operation each.
    template <int Op, int S>
    RELUVA_FORCE_INLINE static uint32_t alu(Cpu& c, uint32_t a, uint32_t b) {
        constexpr uint32_t m = S == 4 ? 0xffffffffu : S == 2 ? 0xffffu : 0xffu;
        constexpr uint32_t sign = S == 4 ? 0x80000000u : S == 2 ? 0x8000u : 0x80u;
        a &= m;
        b &= m;
        uint32_t r = 0;
        uint32_t flags = c.eflags & ~(Cpu::CF | Cpu::OF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::PF);
        if constexpr (Op == 0 || Op == 2) { // ADD, ADC
            const uint64_t wide = uint64_t(a) + b + (Op == 2 ? (c.eflags & Cpu::CF) : 0);
            r = uint32_t(wide) & m;
            if (wide > m)
                flags |= Cpu::CF;
            if ((a ^ r) & (b ^ r) & sign)
                flags |= Cpu::OF;
            flags |= (a ^ b ^ r) & Cpu::AF;
        } else if constexpr (Op == 3 || Op == 5 || Op == 7) { // SBB, SUB, CMP
            const uint64_t subtrahend = uint64_t(b) + (Op == 3 ? (c.eflags & Cpu::CF) : 0);
            r = uint32_t(uint64_t(a) - subtrahend) & m;
            if (uint64_t(a) < subtrahend)
                flags |= Cpu::CF;
            if ((a ^ b) & (a ^ r) & sign)
                flags |= Cpu::OF;
            flags |= (a ^ b ^ r) & Cpu::AF;
        } else if constexpr (Op == 1) {
            r = a | b;
        } else if constexpr (Op == 4) {
            r = a & b;
        } else {
            r = a ^ b;
        }
        c.eflags = flags | logicFlags<S>(r);
        return r;
    }
    template <int S>
    RELUVA_FORCE_INLINE static uint32_t logicFlags(uint32_t r) { // ZF, SF and PF of a result
        constexpr uint32_t sign = S == 4 ? 0x80000000u : S == 2 ? 0x8000u : 0x80u;
        return (r ? 0 : Cpu::ZF) | (r & sign ? Cpu::SF : 0) | kParity.flag[r & 0xff];
    }
    template <int S>
    RELUVA_FORCE_INLINE static void test(Cpu& c, uint32_t r) {
        constexpr uint32_t m = S == 4 ? 0xffffffffu : S == 2 ? 0xffffu : 0xffu;
        c.eflags = (c.eflags & ~(Cpu::CF | Cpu::OF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::PF)) | logicFlags<S>(r & m);
    }
    template <int Cc>
    RELUVA_FORCE_INLINE static bool condition(const Cpu& c) {
        const uint32_t f = c.eflags;
        bool r;
        if constexpr (Cc >> 1 == 0)
            r = f & Cpu::OF;
        else if constexpr (Cc >> 1 == 1)
            r = f & Cpu::CF;
        else if constexpr (Cc >> 1 == 2)
            r = f & Cpu::ZF;
        else if constexpr (Cc >> 1 == 3)
            r = f & (Cpu::CF | Cpu::ZF);
        else if constexpr (Cc >> 1 == 4)
            r = f & Cpu::SF;
        else if constexpr (Cc >> 1 == 5)
            r = f & Cpu::PF;
        else if constexpr (Cc >> 1 == 6)
            r = bool(f & Cpu::SF) != bool(f & Cpu::OF);
        else
            r = (f & Cpu::ZF) || bool(f & Cpu::SF) != bool(f & Cpu::OF);
        return (Cc & 1) ? !r : r;
    }

    // The ALU operations (Op: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP).
    template <int Op, int S>
    static void aluRmReg(Cpu& c, const Cached& e) {
        next(c, e);
        if (e.mem) {
            const uint32_t a = address(c, e);
            const uint32_t r = alu<Op, S>(c, c.load(a, S), getReg<S>(c, e.reg));
            if (Op != 7)
                c.store(a, r, S);
        } else {
            const uint32_t r = alu<Op, S>(c, getReg<S>(c, e.rm), getReg<S>(c, e.reg));
            if (Op != 7)
                setReg<S>(c, e.rm, r);
        }
    }
    template <int Op, int S>
    static void aluRegRm(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t r = alu<Op, S>(c, getReg<S>(c, e.reg), getRm<S>(c, e));
        if (Op != 7)
            setReg<S>(c, e.reg, r);
    }
    template <int Op, int S>
    static void aluAccImm(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t r = alu<Op, S>(c, getReg<S>(c, Cpu::EAX), e.imm);
        if (Op != 7)
            setReg<S>(c, Cpu::EAX, r);
    }
    template <int Op, int S>
    static void aluRmImm(Cpu& c, const Cached& e) {
        next(c, e);
        if (e.mem) {
            const uint32_t a = address(c, e);
            const uint32_t r = alu<Op, S>(c, c.load(a, S), e.imm);
            if (Op != 7)
                c.store(a, r, S);
        } else {
            const uint32_t r = alu<Op, S>(c, getReg<S>(c, e.rm), e.imm);
            if (Op != 7)
                setReg<S>(c, e.rm, r);
        }
    }
    template <int S>
    static void testRmReg(Cpu& c, const Cached& e) {
        next(c, e);
        test<S>(c, getRm<S>(c, e) & getReg<S>(c, e.reg));
    }
    template <int S>
    static void testAccImm(Cpu& c, const Cached& e) {
        next(c, e);
        test<S>(c, getReg<S>(c, Cpu::EAX) & e.imm);
    }
    // INC (Op 0) and DEC (Op 5), which leave CF.
    template <int Op>
    static void incDecReg(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t cf = c.eflags & Cpu::CF;
        c.reg[e.rm] = alu<Op, 4>(c, c.reg[e.rm], 1);
        c.eflags = (c.eflags & ~Cpu::CF) | cf;
    }
    template <int Op, int S>
    static void incDecRm(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t cf = c.eflags & Cpu::CF;
        if (e.mem) {
            const uint32_t a = address(c, e);
            c.store(a, alu<Op, S>(c, c.load(a, S), 1), S);
        } else {
            setReg<S>(c, e.rm, alu<Op, S>(c, getReg<S>(c, e.rm), 1));
        }
        c.eflags = (c.eflags & ~Cpu::CF) | cf;
    }

    // Moves.
    template <int S>
    static void movRmReg(Cpu& c, const Cached& e) {
        next(c, e);
        setRm<S>(c, e, getReg<S>(c, e.reg));
    }
    template <int S>
    static void movRegRm(Cpu& c, const Cached& e) {
        next(c, e);
        setReg<S>(c, e.reg, getRm<S>(c, e));
    }
    template <int S>
    static void movRmImm(Cpu& c, const Cached& e) {
        next(c, e);
        setRm<S>(c, e, e.imm);
    }
    template <int S>
    static void movRegImm(Cpu& c, const Cached& e) {
        next(c, e);
        setReg<S>(c, e.rm, e.imm);
    }
    // MOVZX and MOVSX, from S bytes to D.
    template <int S, int D, bool Signed>
    static void movExtend(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t v = getRm<S>(c, e);
        setReg<D>(c, e.reg, !Signed ? v : S == 1 ? uint32_t(int32_t(int8_t(v))) : uint32_t(int32_t(int16_t(v))));
    }
    // LEA: the offset, without a segment's base.
    static void lea(Cpu& c, const Cached& e) {
        next(c, e);
        uint32_t a = e.disp;
        if (e.base >= 0)
            a += c.reg[size_t(e.base)];
        if (e.index >= 0)
            a += c.reg[size_t(e.index)] << e.scale;
        c.reg[e.reg] = a;
    }

    // The stack.
    static void pushReg(Cpu& c, const Cached& e) {
        next(c, e);
        c.push(c.reg[e.rm]);
    }
    static void popReg(Cpu& c, const Cached& e) {
        next(c, e);
        c.reg[e.rm] = c.pop();
    }
    static void pushImm(Cpu& c, const Cached& e) {
        next(c, e);
        c.push(e.imm);
    }
    static void pushRm(Cpu& c, const Cached& e) {
        next(c, e);
        c.push(getRm<4>(c, e));
    }
    static void leave(Cpu& c, const Cached& e) {
        next(c, e);
        c.reg[Cpu::ESP] = c.reg[Cpu::EBP];
        c.reg[Cpu::EBP] = c.pop();
    }

    // Jumps, calls and returns (relative ones: e.imm from the next instruction).
    template <int Cc>
    static void jcc(Cpu& c, const Cached& e) {
        next(c, e);
        if (condition<Cc>(c))
            c.eip += e.imm;
    }
    static void jmp(Cpu& c, const Cached& e) { c.eip += e.length + e.imm; }
    static void jmpRm(Cpu& c, const Cached& e) {
        next(c, e);
        c.eip = getRm<4>(c, e);
    }
    static void call(Cpu& c, const Cached& e) {
        next(c, e);
        c.push(c.eip);
        c.eip += e.imm;
    }
    static void callRm(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t target = getRm<4>(c, e);
        c.push(c.eip);
        c.eip = target;
    }
    static void ret(Cpu& c, const Cached& e) {
        c.eip = c.pop();
        c.reg[Cpu::ESP] += e.imm;
    }

    // Shifts and rotates (e.sub the operation): by e.imm, or by CL.
    template <int S>
    static void shiftImm(Cpu& c, const Cached& e) {
        next(c, e);
        if (e.mem) {
            const uint32_t a = address(c, e);
            c.store(a, c.shift(e.sub, c.load(a, S), e.imm, S), S);
        } else {
            setReg<S>(c, e.rm, c.shift(e.sub, getReg<S>(c, e.rm), e.imm, S));
        }
    }
    template <int S>
    static void shiftCl(Cpu& c, const Cached& e) {
        next(c, e);
        const uint32_t count = c.reg[Cpu::ECX] & 0xff;
        if (e.mem) {
            const uint32_t a = address(c, e);
            c.store(a, c.shift(e.sub, c.load(a, S), count, S), S);
        } else {
            setReg<S>(c, e.rm, c.shift(e.sub, getReg<S>(c, e.rm), count, S));
        }
    }
    static void setcc(Cpu& c, const Cached& e) {
        next(c, e);
        setRm<1>(c, e, c.condition(e.sub) ? 1 : 0);
    }
    static void cdq(Cpu& c, const Cached& e) {
        next(c, e);
        c.reg[Cpu::EDX] = (c.reg[Cpu::EAX] & 0x80000000u) ? 0xffffffffu : 0;
    }
    static void nop(Cpu& c, const Cached& e) { next(c, e); }

    template <int S>
    static void testRmImm(Cpu& c, const Cached& e) {
        next(c, e);
        test<S>(c, getRm<S>(c, e) & e.imm);
    }
    // IMUL r, r/m (e.mem or e.rm), times e.imm when Imm.
    template <int S, bool Imm>
    static void imul(Cpu& c, const Cached& e) {
        next(c, e);
        const auto extend = [](uint32_t v) { return S == 2 ? int64_t(int16_t(v)) : int64_t(int32_t(v)); };
        const int64_t p = extend(getRm<S>(c, e)) * (Imm ? int64_t(int32_t(e.imm)) : extend(getReg<S>(c, e.reg)));
        setReg<S>(c, e.reg, uint32_t(p));
        c.eflags &= ~(Cpu::CF | Cpu::OF);
        if (p != extend(uint32_t(p)))
            c.eflags |= Cpu::CF | Cpu::OF;
    }

    // The x87 unit's commonest: loads, stores and arithmetic on memory and
    // registers, FXCH, FNSTSW AX (as Cpu::x87 does them).
    RELUVA_FORCE_INLINE static double loadReal(Cpu& c, uint32_t a, bool wide) {
        if (wide) {
            const uint64_t bits = uint64_t(c.load(a, 4)) | uint64_t(c.load(a + 4, 4)) << 32;
            double v;
            std::memcpy(&v, &bits, 8);
            return v;
        }
        const uint32_t bits = c.load(a, 4);
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    }
    RELUVA_FORCE_INLINE static void storeReal(Cpu& c, uint32_t a, double v, bool wide) {
        if (wide) {
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            c.store(a, uint32_t(bits), 4);
            c.store(a + 4, uint32_t(bits >> 32), 4);
        } else {
            const float f = float(v);
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            c.store(a, bits, 4);
        }
    }
    // dst = x op y, for the eight forms (2 and 3 compare x with y).
    template <int R>
    RELUVA_FORCE_INLINE static void arith(Cpu& c, double& dst, double x, double y) {
        if constexpr (R == 0)
            dst = c.fpuRound(x + y);
        else if constexpr (R == 1)
            dst = c.fpuRound(x * y);
        else if constexpr (R == 2 || R == 3)
            c.fpuCompare(x, y, false, false);
        else if constexpr (R == 4)
            dst = c.fpuRound(x - y);
        else if constexpr (R == 5)
            dst = c.fpuRound(y - x);
        else if constexpr (R == 6)
            dst = c.fpuRound(x / y);
        else
            dst = c.fpuRound(y / x);
    }
    template <int R, bool Wide>
    static void fArithMem(Cpu& c, const Cached& e) { // D8 /r, DC /r
        next(c, e);
        arith<R>(c, c.st(0), c.st(0), loadReal(c, address(c, e), Wide));
        if (R == 3)
            c.fpuPop();
    }
    template <int R>
    static void fArithSt0(Cpu& c, const Cached& e) { // D8 C0+r*8+i: st(0) = st(0) op st(i)
        next(c, e);
        arith<R>(c, c.st(0), c.st(0), c.st(e.rm));
        if (R == 3)
            c.fpuPop();
    }
    template <int R, bool Pop>
    static void fArithSti(Cpu& c, const Cached& e) { // DC and DE (R 0, 1, 4-7): st(i) = st(i) op st(0)
        next(c, e);
        constexpr int kSwap[8] = {0, 1, 2, 3, 5, 4, 7, 6};
        arith<kSwap[R]>(c, c.st(e.rm), c.st(e.rm), c.st(0));
        if (Pop)
            c.fpuPop();
    }
    template <bool Wide>
    static void fLoadMem(Cpu& c, const Cached& e) {
        next(c, e);
        c.fpuPush(loadReal(c, address(c, e), Wide));
    }
    template <bool Wide, bool Pop>
    static void fStoreMem(Cpu& c, const Cached& e) {
        next(c, e);
        storeReal(c, address(c, e), Pop ? c.fpuPop() : c.st(0), Wide);
    }
    static void fLoadInt(Cpu& c, const Cached& e) { // DB /0
        next(c, e);
        c.fpuPush(double(int32_t(c.load(address(c, e), 4))));
    }
    static void fLoadSt(Cpu& c, const Cached& e) {
        next(c, e);
        c.fpuPush(c.st(e.rm));
    }
    template <bool Pop>
    static void fStoreSt(Cpu& c, const Cached& e) { // DD D0+i, D8+i
        next(c, e);
        c.st(e.rm) = c.st(0);
        if (Pop)
            c.fpuPop();
    }
    static void fxch(Cpu& c, const Cached& e) {
        next(c, e);
        std::swap(c.st(0), c.st(e.rm));
    }
    static void fchs(Cpu& c, const Cached& e) {
        next(c, e);
        c.st(0) = -c.st(0);
    }
    static void fabs(Cpu& c, const Cached& e) {
        next(c, e);
        c.st(0) = std::fabs(c.st(0));
    }
    template <int V>
    static void fLoadConstant(Cpu& c, const Cached& e) { // FLDZ, FLD1
        next(c, e);
        c.fpuPush(double(V));
    }
    static void fnstswAx(Cpu& c, const Cached& e) {
        next(c, e);
        c.writeReg(2, Cpu::EAX, uint16_t((c.fpu.status & ~(7u << 11)) | (c.fpu.top << 11)));
    }
};

// Runs instructions from the cache until one returns to `returnAddress` or
// goes to a native. A block's instructions run one after another: only its
// last can go elsewhere.
void Cpu::runCached(uint32_t returnAddress, uint32_t nativeStart) {
    if (blocks_.empty()) {
        blocks_.resize(kBlockEntries);
        if (Jit::supported()) {
            const char* self = reinterpret_cast<const char*>(this);
            auto at = [&](const void* member) { return size_t(static_cast<const char*>(member) - self); };
            Jit::Layout l{};
            l.reg = at(reg.data()), l.eip = at(&eip), l.start = at(&instructionStart_), l.epoch = at(&codeEpoch_);
            l.eflags = at(&eflags);
            l.fpuRegs = at(fpu.regs.data()), l.fpuTop = at(&fpu.top), l.fpuTags = at(&fpu.tags);
            l.fpuControl = at(&fpu.control), l.fpuStatus = at(&fpu.status);
            l.fsBase = at(&fsBase), l.memory = memory_.data(), l.pages = codePages_.data(), l.span = span_;
            l.memoryFault = &Cpu::jitMemoryFault;
            l.executed = at(&executed_), l.countingOps = at(&counting_.ops), l.countingCount = at(&counting_.count);
            l.countingStart = at(&counting_.start);
            static_assert(sizeof(stop_) == 1, "the stop flag a byte, as the Jit reads it");
            l.stop = at(&stop_), l.runReturn = at(&run_.returnAddress), l.runNative = at(&run_.nativeStart);
            l.runEnd = at(&run_.end);
            l.blocks = reinterpret_cast<const uint8_t*>(blocks_.data());
            l.blockSize = uint32_t(sizeof(Block)), l.blockMask = kBlockEntries - 1;
            l.nativeBase = nativeBase_, l.callNative = &Cpu::jitCallNative;
            jit_ = std::make_unique<Jit>(l);
            if (!jit_->ready())
                jit_.reset();
        }
    }
    Jit* const jit = jitOn ? jit_.get() : nullptr;
    // This run's bounds, for the Jit's dispatch; the run this one is
    // inside of (a native's call back in) gets its own back after.
    struct Bounds {
        decltype(run_)& now;
        const decltype(run_) before;
        Bounds(decltype(run_)& r, uint32_t ret, uint32_t natives, uint64_t end) : now(r), before(r) {
            now = {ret, natives, end};
        }
        ~Bounds() { now = before; }
    } bounds{run_, returnAddress, nativeStart, limit_ ? limitAt_ : ~uint64_t(0)};
    const uint64_t end = limit_ ? limitAt_ : ~uint64_t(0);
    Block* const blocks = blocks_.data();
    const uint8_t* const memory = memory_.data();
    do {
        if (stop_.load(std::memory_order_relaxed)) { // counted, as one instruction at a time did
            instructionStart_ = eip;
            if (++executed_ > end)
                fault("instruction limit reached");
            fault("stopped");
        }
        Block& b = blocks[(eip ^ (eip >> 15)) & (kBlockEntries - 1)];
        bool fresh = b.start == eip;
        if (fresh && b.epoch != codeEpoch_) {
            uint64_t differ = 0;
            const int last = b.words - 1;
            for (int w = 0; w < last; ++w) {
                uint64_t now;
                std::memcpy(&now, memory + eip + 8 * uint32_t(w), 8);
                differ |= now ^ b.code[w];
            }
            uint64_t now;
            std::memcpy(&now, memory + eip + 8 * uint32_t(last), 8);
            differ |= (now ^ b.code[last]) & b.lastMask;
            fresh = differ == 0;
            b.epoch = codeEpoch_;
        }
        if (!fresh)
            rebuild(b, eip);
        // The dispatch's index, as the slot now is (whatever else ran it
        // last, it mustn't find code that isn't this block's).
        const auto index = [&] {
            const bool made = jit_ && b.jit && b.jitGeneration == jit_->generation();
            if (jit_)
                jit_->setEntry(uint32_t(&b - blocks), b.start, b.epoch, made ? b.jit : nullptr, b.count);
        };
        index();
        // The blocks it may go to next, into the cache while it runs: the
        // table is larger than the caches, and finding the next block was
        // much of this loop's time.
        for (uint32_t to : b.next)
            if (to != 0xffffffffu)
                RELUVA_PREFETCH(&blocks[(to ^ (to >> 15)) & (kBlockEntries - 1)]);
        const Cached* e = b.ops;
        const Cached* const last = e + b.count - 1;
        if (executed_ + b.count <= end) {
            // Counted as a whole; a fault part way through uncounts the
            // rest (uncountBlock). Only a block's last instruction reads
            // the count (RDTSC ends its block).
            executed_ += b.count;
            counting_.ops = b.ops; // stores of their own: as one, Visual Studio's build stalled on it
            counting_.count = b.count;
            counting_.start = eip;
            if (jit && b.start == eip) { // (not one rebuilt each time it runs)
                // Made on its second run: many blocks made run once, and
                // are made again for another address before the next.
                if (b.jitGeneration != jit->generation() && ++b.runs >= 2) {
                    compileBlock(b);
                    index();
                }
                if (b.jit && b.jitGeneration == jit->generation()) {
                    // Counted while it runs: native code a native's call back
                    // in made mustn't be forgotten under it (compileBlock).
                    struct Depth {
                        int& d;
                        explicit Depth(int& n) : d(n) { ++d; }
                        ~Depth() { --d; }
                    } depth{jitDepth_};
                    jit->run(this, b.jit);
                    counting_.ops = nullptr;
                    continue;
                }
            }
            for (;; ++e) {
                instructionStart_ = eip;
                e->run(*this, *e);
                if (e == last)
                    break;
            }
            counting_.ops = nullptr;
        } else {
            for (;; ++e) {
                instructionStart_ = eip;
                if (++executed_ > end)
                    fault("instruction limit reached");
                e->run(*this, *e);
                if (e == last)
                    break;
            }
        }
        // (A native the Jit's dispatch called may have asked the thread to
        // yield: runUntil sees to it.)
    } while (eip != returnAddress && eip < nativeStart && !yield_);
}

// The block's native code: each instruction's start stored, its handler
// called, as the loop in runCached does. With the buffer full, all the code
// is forgotten and made again as blocks run, unless some is running (a
// native called back in): then this block runs through the loop for now.
namespace {
// The handlers the Jit makes with flags, and what each is.
struct Made {
    Jit::Kind kind;
    uint8_t alu, form, size, cc;
    bool lahf = true; // needs Jit::flagsOps
};
struct MadeTable {
    std::unordered_map<const void*, Made> map;
    template <class F>
    Made& operator[](F* handler) {
        return map[reinterpret_cast<const void*>(handler)];
    }
};
// By the handler's address.
const std::unordered_map<const void*, Made>& madeWithFlags() {
    using O = CachedOps;
    using K = Jit::Kind;
    static const std::unordered_map<const void*, Made> made = [] {
        MadeTable m;
        auto sizes = [&]<int Op>(std::integral_constant<int, Op>) {
            m[&O::aluRmReg<Op, 1>] = {K::Alu, Op, 0, 1, 0}, m[&O::aluRmReg<Op, 4>] = {K::Alu, Op, 0, 4, 0};
            m[&O::aluRegRm<Op, 1>] = {K::Alu, Op, 1, 1, 0}, m[&O::aluRegRm<Op, 4>] = {K::Alu, Op, 1, 4, 0};
            m[&O::aluAccImm<Op, 1>] = {K::Alu, Op, 2, 1, 0}, m[&O::aluAccImm<Op, 4>] = {K::Alu, Op, 2, 4, 0};
            m[&O::aluRmImm<Op, 1>] = {K::Alu, Op, 3, 1, 0}, m[&O::aluRmImm<Op, 4>] = {K::Alu, Op, 3, 4, 0};
        };
        [&]<int... Op>(std::integer_sequence<int, Op...>) {
            (sizes(std::integral_constant<int, Op>{}), ...);
        }(std::make_integer_sequence<int, 8>{});
        m[&O::testRmReg<1>] = {K::Alu, 8, 0, 1, 0}, m[&O::testRmReg<4>] = {K::Alu, 8, 0, 4, 0};
        m[&O::testAccImm<1>] = {K::Alu, 8, 2, 1, 0}, m[&O::testAccImm<4>] = {K::Alu, 8, 2, 4, 0};
        m[&O::incDecReg<0>] = {K::Alu, 9, 3, 4, 0}, m[&O::incDecReg<5>] = {K::Alu, 10, 3, 4, 0};
        m[&O::incDecRm<0, 1>] = {K::Alu, 9, 3, 1, 0}, m[&O::incDecRm<0, 4>] = {K::Alu, 9, 3, 4, 0};
        m[&O::incDecRm<5, 1>] = {K::Alu, 10, 3, 1, 0}, m[&O::incDecRm<5, 4>] = {K::Alu, 10, 3, 4, 0};
        m[&O::testRmImm<1>] = {K::Alu, 8, 3, 1, 0}, m[&O::testRmImm<4>] = {K::Alu, 8, 3, 4, 0};
        m[&O::fArithMem<2, true>] = {K::FCom, 0, 0, 8, 0}, m[&O::fArithMem<2, false>] = {K::FCom, 0, 0, 4, 0};
        m[&O::fArithMem<3, true>] = {K::FCom, 0, 0, 8, 1}, m[&O::fArithMem<3, false>] = {K::FCom, 0, 0, 4, 1};
        m[&O::fArithSt0<2>] = {K::FCom, 0, 0, 4, 0}, m[&O::fArithSt0<3>] = {K::FCom, 0, 0, 4, 1}; // LAHF too
        m[&O::fnstswAx] = {K::FnstswAx, 0, 0, 4, 0, false};
        [&]<int... Cc>(std::integer_sequence<int, Cc...>) {
            ((m[&O::jcc<Cc>] = {K::Jcc, 0, 0, 4, uint8_t(Cc)}), ...);
        }(std::make_integer_sequence<int, 16>{});
        // The x87 unit (no flags).
        m[&O::fLoadMem<true>] = {K::FLoad, 0, 0, 8, 0, false}, m[&O::fLoadMem<false>] = {K::FLoad, 0, 0, 4, 0, false};
        m[&O::fStoreMem<true, false>] = {K::FStore, 0, 0, 8, 0, false};
        m[&O::fStoreMem<true, true>] = {K::FStore, 0, 0, 8, 1, false};
        m[&O::fStoreMem<false, false>] = {K::FStore, 0, 0, 4, 0, false};
        m[&O::fStoreMem<false, true>] = {K::FStore, 0, 0, 4, 1, false};
        m[&O::fLoadInt] = {K::FLoadInt, 0, 0, 4, 0, false};
        m[&O::fLoadSt] = {K::FLoadSt, 0, 0, 4, 0, false};
        m[&O::fStoreSt<false>] = {K::FStoreSt, 0, 0, 4, 0, false};
        m[&O::fStoreSt<true>] = {K::FStoreSt, 0, 0, 4, 1, false};
        m[&O::fxch] = {K::Fxch, 0, 0, 4, 0, false};
        auto arith = [&]<int R>(std::integral_constant<int, R>) { // not the compares (2, 3)
            m[&O::fArithMem<R, true>] = {K::FArith, R, 0, 8, 0, false};
            m[&O::fArithMem<R, false>] = {K::FArith, R, 0, 4, 0, false};
            m[&O::fArithSt0<R>] = {K::FArithSt0, R, 0, 4, 0, false};
            m[&O::fArithSti<R, false>] = {K::FArithSti, R, 0, 4, 0, false};
            m[&O::fArithSti<R, true>] = {K::FArithSti, R, 0, 4, 1, false};
        };
        [&]<int... R>(std::integer_sequence<int, R...>) {
            (arith(std::integral_constant<int, R>{}), ...);
        }(std::integer_sequence<int, 0, 1, 4, 5, 6, 7>{});
        return m.map;
    }();
    return made;
}
} // namespace

void Cpu::compileBlock(Block& b) {
    using O = CachedOps;
    using K = Jit::Kind;
    static const bool flags = Jit::flagsOps();
    Jit::Op ops[kBlockOps];
    uint32_t at = b.start;
    for (int i = 0; i < b.count; ++i) {
        const Cached& e = b.ops[i];
        Jit::Op& o = ops[i];
        o.start = at;
        o.length = e.length;
        o.reg = e.reg, o.rm = e.rm, o.scale = e.scale, o.base = e.base, o.index = e.index;
        o.mem = e.mem, o.fs = e.fs, o.disp = e.disp, o.imm = e.imm;
        o.size = 4;
        const auto run = e.run;
        if (run == &O::movRegRm<4> || run == &O::movRegRm<1>)
            o.kind = K::MovRegRm, o.size = run == &O::movRegRm<4> ? 4 : 1;
        else if (run == &O::movRmReg<4> || run == &O::movRmReg<1>)
            o.kind = K::MovRmReg, o.size = run == &O::movRmReg<4> ? 4 : 1;
        else if (run == &O::movRmImm<4> || run == &O::movRmImm<1>)
            o.kind = K::MovRmImm, o.size = run == &O::movRmImm<4> ? 4 : 1;
        else if (run == &O::movRegImm<4>)
            o.kind = K::MovRegImm;
        else if (run == &O::lea)
            o.kind = K::Lea;
        else if (run == &O::pushReg)
            o.kind = K::PushReg;
        else if (run == &O::pushImm)
            o.kind = K::PushImm;
        else if (run == &O::popReg)
            o.kind = K::PopReg;
        else if (run == &O::jmp)
            o.kind = K::Jmp;
        else if (run == &O::call)
            o.kind = K::CallRel;
        else if (run == &O::jmpRm)
            o.kind = K::JmpRm;
        else if (run == &O::callRm)
            o.kind = K::CallRm;
        else if (run == &O::ret)
            o.kind = K::Ret;
        else if (flags && run == &O::shiftImm<4> && e.sub >= 4 && (e.imm & 31) != 0) {
            // SHL (and SAL, 6), SHR, SAR by 1 to 31.
            o.kind = K::Alu, o.form = 3, o.size = 4, o.imm = e.imm & 31;
            o.alu = uint8_t(e.sub == 5 ? 12 : e.sub == 7 ? 13 : 11);
        }
        else if (auto it = madeWithFlags().find(reinterpret_cast<const void*>(run));
                 it != madeWithFlags().end() && (flags || !it->second.lahf)) {
            o.kind = it->second.kind, o.alu = it->second.alu, o.form = it->second.form;
            o.size = it->second.size, o.cc = it->second.cc;
            if (run == &O::incDecReg<0> || run == &O::incDecReg<5>)
                o.mem = false; // INC and DEC of a register: its number in rm
        } else
            o.kind = K::Call, o.handler = reinterpret_cast<const void*>(run), o.entry = &e;
        // One run from its bytes (length 0) can't be made here: it reads eip.
        if (!e.length)
            o.kind = K::Call, o.handler = reinterpret_cast<const void*>(run), o.entry = &e;
        at += e.length;
    }
    const uint8_t* code = jit_->compile(ops, b.count, at, b.ops, sizeof(Cached));
    if (!code && jitDepth_ == 0) {
        jit_->reset();
        code = jit_->compile(ops, b.count, at, b.ops, sizeof(Cached));
    }
    b.jit = code;
    b.jitGeneration = code ? jit_->generation() : 0;
}

void Cpu::jitMemoryFault(void* cpu, uint32_t address, uint32_t size) {
    static_cast<Cpu*>(cpu)->memoryFault(address, size);
}

// A native at eip, from the Jit's dispatch, as runUntil's step() calls it:
// out of any block's count, and with no made code under it but the
// dispatch's (which reset() keeps), so a run it calls back into may make
// the buffer afresh.
int Cpu::jitCallNative(void* cpu) {
    Cpu& c = *static_cast<Cpu*>(cpu);
    c.counting_.ops = nullptr;
    c.instructionStart_ = c.eip;
    struct Out {
        int& d;
        explicit Out(int& n) : d(n) { --d; }
        ~Out() { ++d; }
    } out{c.jitDepth_};
    c.callNative(c.eip);
    return c.yield_ ? 0 : 1;
}

void Cpu::uncountBlock() {
    const Cached* ops = counting_.ops;
    if (!ops)
        return;
    counting_.ops = nullptr;
    const int count = int(counting_.count);
    uint32_t at = counting_.start;
    int i = 0;
    while (i < count - 1 && at != instructionStart_)
        at += ops[i++].length;
    executed_ -= uint64_t(count - 1 - i);
}

// Makes the block that starts at `at`: its instructions up to the first
// that may go elsewhere (or runs from its bytes), at most kBlockOps of
// them and 8 * kBlockWords bytes.
void Cpu::build(Block& b, uint32_t at) {
    b.start = at;
    b.count = 0;
    b.jit = nullptr; // made again when it runs
    b.jitGeneration = 0;
    b.runs = 0;
    uint32_t bytes = 0;
    for (;;) {
        Cached& e = b.ops[b.count++];
        decode(e, at + bytes);
        bytes += e.length;
        if (e.ends || b.count == kBlockOps || bytes + 16 > 8 * kBlockWords)
            break;
    }
    // One that ends the block and is run from its bytes has length 0: the
    // block's bytes are those before it.
    b.bytes = uint8_t(bytes);
    // Where it goes next, when that's known.
    const Cached& last = b.ops[b.count - 1];
    b.next[0] = b.next[1] = 0xffffffffu;
    if (!last.ends) {
        b.next[0] = at + bytes; // full: straight on
    } else if (last.length) {
        using O = CachedOps;
        const uint32_t after = at + bytes, target = after + last.imm;
        if (last.run == &O::jmp || last.run == &O::call)
            b.next[0] = target;
        else if (last.run != &O::ret && last.run != &O::jmpRm && last.run != &O::callRm)
            b.next[0] = after, b.next[1] = target; // a conditional jump
    }
    b.words = uint8_t(std::max<uint32_t>(1, (bytes + 7) / 8));
    const uint32_t tail = bytes - 8 * (b.words - 1u);
    b.lastMask = tail >= 8 ? ~uint64_t(0) : (uint64_t(1) << (8 * tail)) - 1;
    if (!valid(at, 8u * b.words)) { // never checked again: rebuilt each time
        b.start = 0xffffffffu;
        return;
    }
    std::memcpy(b.code, memory_.data() + at, 8u * b.words);
    // Its pages, and the one before (a write starting there may reach in),
    // are code: writes to them make blocks check their bytes again.
    for (uint32_t page = (at >= 3 ? at - 3 : 0) >> 12; page <= (at + 8u * b.words - 1) >> 12; ++page)
        codePages_[page] = 1;
    b.epoch = codeEpoch_;
}

void Cpu::rebuild(Block& b, uint32_t at) {
    if (b.jit && b.start != 0xffffffffu && b.start != at) { // put by for when it comes round again
        if (spare_.size() >= kSpareBlocks)
            spare_.clear();
        auto& kept = spare_[b.start];
        if (!kept)
            kept = std::make_unique<Block>();
        *kept = b;
    }
    if (auto it = spare_.find(at); it != spare_.end()) {
        Block& k = *it->second;
        bool same = valid(at, 8u * k.words);
        for (int w = 0; same && w < k.words; ++w) {
            uint64_t now;
            std::memcpy(&now, memory_.data() + at + 8 * uint32_t(w), 8);
            same = ((now ^ k.code[w]) & (w == k.words - 1 ? k.lastMask : ~uint64_t(0))) == 0;
        }
        if (same) {
            b = k;
            b.epoch = codeEpoch_;
            spare_.erase(it);
            return;
        }
        spare_.erase(it);
    }
    build(b, at);
}

// Makes the entry for the instruction at `at`: a handler of its own if it's
// one of those that have one, else one that runs it from its bytes.
void Cpu::decode(Cached& e, uint32_t at) {
    using O = CachedOps;
    using Handler = Cached::Handler;
    // Handlers by operand size (1, 2, 4 bytes), operation and condition.
#define RELUVA_BY_OP(name, size)                                                                              \
    {&O::name<0, size>, &O::name<1, size>, &O::name<2, size>, &O::name<3, size>,                              \
     &O::name<4, size>, &O::name<5, size>, &O::name<6, size>, &O::name<7, size>}
#define RELUVA_BY_SIZE(name) {RELUVA_BY_OP(name, 1), RELUVA_BY_OP(name, 2), RELUVA_BY_OP(name, 4)}
    static constexpr Handler kAluRmReg[3][8] = RELUVA_BY_SIZE(aluRmReg);
    static constexpr Handler kAluRegRm[3][8] = RELUVA_BY_SIZE(aluRegRm);
    static constexpr Handler kAluAccImm[3][8] = RELUVA_BY_SIZE(aluAccImm);
    static constexpr Handler kAluRmImm[3][8] = RELUVA_BY_SIZE(aluRmImm);
#undef RELUVA_BY_SIZE
#undef RELUVA_BY_OP
#define RELUVA_BY_R(name, ...)                                                                                \
    {&O::name<0 __VA_ARGS__>, &O::name<1 __VA_ARGS__>, &O::name<2 __VA_ARGS__>, &O::name<3 __VA_ARGS__>,       \
     &O::name<4 __VA_ARGS__>, &O::name<5 __VA_ARGS__>, &O::name<6 __VA_ARGS__>, &O::name<7 __VA_ARGS__>}
    static constexpr Handler kJcc[16] = {&O::jcc<0>,  &O::jcc<1>,  &O::jcc<2>,  &O::jcc<3>,  &O::jcc<4>,  &O::jcc<5>,
                                         &O::jcc<6>,  &O::jcc<7>,  &O::jcc<8>,  &O::jcc<9>,  &O::jcc<10>, &O::jcc<11>,
                                         &O::jcc<12>, &O::jcc<13>, &O::jcc<14>, &O::jcc<15>};
    static constexpr Handler kFArithMem[2][8] = {RELUVA_BY_R(fArithMem, , false), RELUVA_BY_R(fArithMem, , true)};
    static constexpr Handler kFArithSt0[8] = RELUVA_BY_R(fArithSt0);
    static constexpr Handler kFArithSti[2][8] = {RELUVA_BY_R(fArithSti, , false), RELUVA_BY_R(fArithSti, , true)};
#undef RELUVA_BY_R

    e = Cached{};
    e.run = &O::legacy;
    e.ends = true;
    if (!valid(at, 16))
        return;
    const uint8_t* p = memory_.data() + at;
    size_t i = 0;
    bool o16 = false; // an operand-size prefix
    for (;; ++i) {
        if (p[i] == 0x64 && !e.fs)
            e.fs = true;
        else if (p[i] == 0x66 && !o16)
            o16 = true;
        else
            break;
    }
    uint8_t op = p[i++];
    // From its bytes, each time, but its block goes on after it: it doesn't
    // go elsewhere, and its length is known.
    auto straight = [&] {
        e = Cached{};
        e.run = &O::legacy;
        e.length = uint8_t(i);
    };
    if ((op == 0xf3 || op == 0xf2) && p[i] >= 0xa4 && p[i] <= 0xaf) { // REP string operations
        ++i;
        straight();
        return;
    }
    switch (op) { // other prefixes: from the bytes
    case 0x26: case 0x2e: case 0x36: case 0x3e: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0xf0: case 0xf2: case 0xf3:
        return;
    default: break;
    }
    const bool twoByte = op == 0x0f;
    if (twoByte)
        op = p[i++];
    const int wideIndex = o16 ? 1 : 2; // the handlers for a full-sized operand
    bool sized = false; // the handler chosen heeds the operand size
    // The ModRM operand: a register (e.rm) or memory (base, index, scale,
    // displacement).
    auto modrm = [&] {
        const uint8_t m = p[i++];
        const uint8_t mod = m >> 6, rm = m & 7;
        e.reg = (m >> 3) & 7;
        if (mod == 3) {
            e.rm = rm;
            return;
        }
        e.mem = true;
        if (rm == 4) {
            const uint8_t sib = p[i++];
            const uint8_t scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
            if (index != 4) {
                e.index = int8_t(index);
                e.scale = scale;
            }
            if (base == 5 && mod == 0) {
                std::memcpy(&e.disp, p + i, 4);
                i += 4;
            } else {
                e.base = int8_t(base);
            }
        } else if (rm == 5 && mod == 0) {
            std::memcpy(&e.disp, p + i, 4);
            i += 4;
        } else {
            e.base = int8_t(rm);
        }
        if (mod == 1) {
            e.disp += uint32_t(int32_t(int8_t(p[i++])));
        } else if (mod == 2) {
            uint32_t d;
            std::memcpy(&d, p + i, 4);
            e.disp += d;
            i += 4;
        }
    };
    auto imm8 = [&] { return uint32_t(p[i++]); };
    auto simm8 = [&] { return uint32_t(int32_t(int8_t(p[i++]))); };
    auto imm16 = [&] {
        const uint32_t v = uint32_t(p[i] | p[i + 1] << 8);
        i += 2;
        return v;
    };
    auto imm32 = [&] {
        uint32_t v;
        std::memcpy(&v, p + i, 4);
        i += 4;
        return v;
    };
    auto immOsz = [&] { return o16 ? imm16() : imm32(); };
    auto bySize = [&](Handler h2, Handler h4) {
        sized = true;
        return o16 ? h2 : h4;
    };
    Handler run = nullptr;
    bool plain = false; // with no handler of its own, still straight on (length i)
    if (twoByte) {
        if (op >= 0x80 && op <= 0x8f) { // Jcc rel32
            e.imm = imm32();
            run = kJcc[op & 15];
        } else if (op >= 0x90 && op <= 0x9f) { // SETcc
            modrm();
            e.sub = op & 15;
            run = &O::setcc;
        } else if (op == 0xb6 || op == 0xbe) { // MOVZX, MOVSX from 8 bits
            modrm();
            run = op == 0xb6 ? bySize(&O::movExtend<1, 2, false>, &O::movExtend<1, 4, false>)
                             : bySize(&O::movExtend<1, 2, true>, &O::movExtend<1, 4, true>);
        } else if (op == 0xb7 || op == 0xbf) {
            modrm();
            run = op == 0xb7 ? &O::movExtend<2, 4, false> : &O::movExtend<2, 4, true>;
        } else if (op == 0xaf) {
            modrm();
            run = bySize(&O::imul<2, false>, &O::imul<4, false>);
        }
    } else if (op < 0x40 && (op & 7) < 6) { // the ALU operations
        const int sub = op >> 3;
        const int size = (op & 1) ? wideIndex : 0;
        sized = true;
        switch (op & 7) {
        case 0:
        case 1: modrm(); run = kAluRmReg[size][sub]; break;
        case 2:
        case 3: modrm(); run = kAluRegRm[size][sub]; break;
        default:
            e.imm = (op & 1) ? immOsz() : imm8();
            run = kAluAccImm[size][sub];
            break;
        }
    } else if (op >= 0x40 && op <= 0x4f) { // INC/DEC r
        e.rm = op & 7;
        run = op < 0x48 ? &O::incDecReg<0> : &O::incDecReg<5>;
    } else if (op >= 0x50 && op <= 0x57) {
        e.rm = op & 7;
        run = &O::pushReg;
    } else if (op >= 0x58 && op <= 0x5f) {
        e.rm = op & 7;
        run = &O::popReg;
    } else if (op >= 0x70 && op <= 0x7f) { // Jcc rel8
        e.imm = simm8();
        run = kJcc[op & 15];
    } else if (op >= 0xb0 && op <= 0xb7) {
        e.rm = op & 7;
        e.imm = imm8();
        run = &O::movRegImm<1>;
    } else if (op >= 0xb8 && op <= 0xbf) {
        e.rm = op & 7;
        e.imm = immOsz();
        run = bySize(&O::movRegImm<2>, &O::movRegImm<4>);
    } else if (op >= 0xd8 && op <= 0xdf) { // x87
        modrm();
        plain = true; // none goes elsewhere
        const int r = e.reg;
        if (e.mem) {
            if (op == 0xd8 || op == 0xdc)
                run = kFArithMem[op == 0xdc][r];
            else if ((op == 0xd9 || op == 0xdd) && r == 0)
                run = op == 0xdd ? &O::fLoadMem<true> : &O::fLoadMem<false>;
            else if ((op == 0xd9 || op == 0xdd) && (r == 2 || r == 3))
                run = op == 0xdd ? (r == 3 ? &O::fStoreMem<true, true> : &O::fStoreMem<true, false>)
                                 : (r == 3 ? &O::fStoreMem<false, true> : &O::fStoreMem<false, false>);
            else if (op == 0xdb && r == 0)
                run = &O::fLoadInt;
        } else {
            const uint8_t m = uint8_t(0xc0 | r << 3 | e.rm);
            if (op == 0xd8)
                run = kFArithSt0[r];
            else if ((op == 0xdc || op == 0xde) && r != 2 && r != 3)
                run = kFArithSti[op == 0xde][r];
            else if (op == 0xd9 && r == 0)
                run = &O::fLoadSt;
            else if (op == 0xd9 && r == 1)
                run = &O::fxch;
            else if (op == 0xd9 && m == 0xe0)
                run = &O::fchs;
            else if (op == 0xd9 && m == 0xe1)
                run = &O::fabs;
            else if (op == 0xd9 && m == 0xe8)
                run = &O::fLoadConstant<1>;
            else if (op == 0xd9 && m == 0xee)
                run = &O::fLoadConstant<0>;
            else if (op == 0xdd && (r == 2 || r == 3))
                run = r == 3 ? &O::fStoreSt<true> : &O::fStoreSt<false>;
            else if (op == 0xdf && m == 0xe0)
                run = &O::fnstswAx;
        }
    } else {
        switch (op) {
        case 0x68: e.imm = imm32(); run = &O::pushImm; break;
        case 0x6a: e.imm = simm8(); run = &O::pushImm; break;
        case 0x69:
        case 0x6b:
            modrm();
            e.imm = op == 0x6b ? simm8() : o16 ? uint32_t(int32_t(int16_t(imm16()))) : imm32();
            run = bySize(&O::imul<2, true>, &O::imul<4, true>);
            break;
        case 0x80:
        case 0x81:
        case 0x83:
            modrm();
            e.imm = op == 0x80 ? imm8() : op == 0x81 ? immOsz() : simm8();
            run = kAluRmImm[op == 0x80 ? 0 : wideIndex][e.reg];
            sized = true;
            break;
        case 0x84: modrm(); run = &O::testRmReg<1>; break;
        case 0x85: modrm(); run = bySize(&O::testRmReg<2>, &O::testRmReg<4>); break;
        case 0x88: modrm(); run = &O::movRmReg<1>; break;
        case 0x89: modrm(); run = bySize(&O::movRmReg<2>, &O::movRmReg<4>); break;
        case 0x8a: modrm(); run = &O::movRegRm<1>; break;
        case 0x8b: modrm(); run = bySize(&O::movRegRm<2>, &O::movRegRm<4>); break;
        case 0x8d:
            modrm();
            if (e.mem)
                run = &O::lea;
            break;
        case 0x90: run = &O::nop; break;
        case 0x99: run = &O::cdq; break;
        case 0xa0: // MOV AL/EAX, moffs; MOV moffs, AL/EAX
        case 0xa1:
        case 0xa2:
        case 0xa3:
            e.mem = true;
            e.reg = EAX;
            e.disp = imm32();
            run = op == 0xa0   ? &O::movRegRm<1>
                  : op == 0xa1 ? bySize(&O::movRegRm<2>, &O::movRegRm<4>)
                  : op == 0xa2 ? &O::movRmReg<1>
                               : bySize(&O::movRmReg<2>, &O::movRmReg<4>);
            break;
        case 0xa8: e.imm = imm8(); run = &O::testAccImm<1>; break;
        case 0xa9: e.imm = immOsz(); run = bySize(&O::testAccImm<2>, &O::testAccImm<4>); break;
        case 0xc0:
        case 0xc1:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3: {
            modrm();
            e.sub = e.reg;
            const bool wide = op & 1;
            if (op <= 0xd1) {
                e.imm = op <= 0xc1 ? imm8() : 1;
                run = wide ? bySize(&O::shiftImm<2>, &O::shiftImm<4>) : &O::shiftImm<1>;
            } else {
                run = wide ? bySize(&O::shiftCl<2>, &O::shiftCl<4>) : &O::shiftCl<1>;
            }
            break;
        }
        case 0xc2: e.imm = imm16(); run = &O::ret; break;
        case 0xc3: run = &O::ret; break;
        case 0xc6: modrm(); e.imm = imm8(); run = &O::movRmImm<1>; break;
        case 0xc7: modrm(); e.imm = immOsz(); run = bySize(&O::movRmImm<2>, &O::movRmImm<4>); break;
        case 0xc9: run = &O::leave; break;
        case 0xe8: e.imm = imm32(); run = &O::call; break;
        case 0xe9: e.imm = imm32(); run = &O::jmp; break;
        case 0xeb: e.imm = simm8(); run = &O::jmp; break;
        case 0xf6:
        case 0xf7:
            modrm();
            if (e.reg <= 1) { // TEST r/m, imm
                e.imm = op == 0xf6 ? imm8() : immOsz();
                run = op == 0xf6 ? &O::testRmImm<1> : bySize(&O::testRmImm<2>, &O::testRmImm<4>);
            } else {
                plain = true; // NOT, NEG, MUL, IMUL, DIV, IDIV
            }
            break;
        case 0xfe:
            modrm();
            if (e.reg <= 1)
                run = e.reg ? &O::incDecRm<5, 1> : &O::incDecRm<0, 1>;
            break;
        case 0xff:
            modrm();
            switch (e.reg) {
            case 0:
            case 1: run = bySize(e.reg ? &O::incDecRm<5, 2> : &O::incDecRm<0, 2>,
                                 e.reg ? &O::incDecRm<5, 4> : &O::incDecRm<0, 4>); break;
            case 2: run = &O::callRm; break;
            case 4: run = &O::jmpRm; break;
            case 6: run = &O::pushRm; break;
            default: break;
            }
            break;
        default: break;
        }
    }
    if (!run && plain) {
        straight();
        return;
    }
    if (!run || (o16 && !sized)) { // from its bytes, each time, and the block ends with it
        e = Cached{};
        e.run = &O::legacy;
        e.ends = true;
        return;
    }
    e.run = run;
    e.length = uint8_t(i);
    e.ends = run == &O::call || run == &O::callRm || run == &O::jmp || run == &O::jmpRm || run == &O::ret;
    for (Handler j : kJcc)
        e.ends = e.ends || run == j;
}

} // namespace reluva::x86
