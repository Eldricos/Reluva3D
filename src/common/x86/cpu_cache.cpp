// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The decoded-instruction cache. Each instruction's address has an entry,
// direct-mapped, made the first time it runs: its operands decoded once
// (the ModRM operand as base, index, scale and displacement; the
// immediate), and a handler that runs it. The instructions programs run
// most (moves, the ALU operations, pushes and pops, jumps, calls and
// returns, LEA, MOVZX/MOVSX, shifts, SETcc) have handlers of their own;
// the rest, and those with prefixes other than FS, run from their bytes
// as before (Cpu::execute). An entry keeps its instruction's bytes and is
// remade when they've changed, however they were written (a program's
// loader copying code with a native memcpy, say).

#include "common/x86/cpu.h"

#include <cmath>
#include <utility>

namespace reluva::x86 {

struct CachedOps {
    using Cached = Cpu::Cached;

    static uint32_t address(const Cpu& c, const Cached& e) {
        uint32_t a = e.disp;
        if (e.base >= 0)
            a += c.reg[size_t(e.base)];
        if (e.index >= 0)
            a += c.reg[size_t(e.index)] << e.scale;
        return e.fs ? a + c.fsBase : a;
    }
    template <int S>
    static uint32_t getReg(const Cpu& c, int r) {
        return S == 4 ? c.reg[size_t(r)] : c.readReg(S, r);
    }
    template <int S>
    static void setReg(Cpu& c, int r, uint32_t v) {
        if (S == 4)
            c.reg[size_t(r)] = v;
        else
            c.writeReg(S, r, v);
    }
    template <int S>
    static uint32_t getRm(Cpu& c, const Cached& e) {
        return e.mem ? c.load(address(c, e), S) : getReg<S>(c, e.rm);
    }
    template <int S>
    static void setRm(Cpu& c, const Cached& e, uint32_t v) {
        if (e.mem)
            c.store(address(c, e), v, S);
        else
            setReg<S>(c, e.rm, v);
    }
    static void next(Cpu& c, const Cached& e) { c.eip += e.length; }

    static void legacy(Cpu& c, const Cached&) { c.execute(); }

    // Cpu::alu and Cpu::condition, for one operation each.
    template <int Op, int S>
    static uint32_t alu(Cpu& c, uint32_t a, uint32_t b) {
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
    static uint32_t logicFlags(uint32_t r) { // ZF, SF and PF of a result
        constexpr uint32_t sign = S == 4 ? 0x80000000u : S == 2 ? 0x8000u : 0x80u;
        return (r ? 0 : Cpu::ZF) | (r & sign ? Cpu::SF : 0) | (__builtin_parity(r & 0xff) ? 0 : Cpu::PF);
    }
    template <int S>
    static void test(Cpu& c, uint32_t r) {
        constexpr uint32_t m = S == 4 ? 0xffffffffu : S == 2 ? 0xffffu : 0xffu;
        c.eflags = (c.eflags & ~(Cpu::CF | Cpu::OF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::PF)) | logicFlags<S>(r & m);
    }
    template <int Cc>
    static bool condition(const Cpu& c) {
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
    static double loadReal(Cpu& c, uint32_t a, bool wide) {
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
    static void storeReal(Cpu& c, uint32_t a, double v, bool wide) {
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
    static void arith(Cpu& c, double& dst, double x, double y) {
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
// goes to a native.
void Cpu::runCached(uint32_t returnAddress, uint32_t nativeStart) {
    if (cache_.empty())
        cache_.resize(kCacheEntries);
    const uint64_t end = limit_ ? limitAt_ : ~uint64_t(0);
    Cached* const cache = cache_.data();
    const uint8_t* const memory = memory_.data();
    do {
        instructionStart_ = eip;
        if (++executed_ > end)
            fault("instruction limit reached");
        if (stop_.load(std::memory_order_relaxed))
            fault("stopped");
        Cached& e = cache[(eip ^ (eip >> 17)) & (kCacheEntries - 1)];
        uint64_t now[2];
        std::memcpy(now, memory + eip, 16);
        if (e.tag != eip || ((now[0] ^ e.code[0]) & e.mask[0]) | ((now[1] ^ e.code[1]) & e.mask[1]))
            decode(e);
        e.run(*this, e);
    } while (eip != returnAddress && eip < nativeStart);
}

// Makes the entry for the instruction at eip: a handler of its own if it's
// one of those that have one, else one that runs it from its bytes.
void Cpu::decode(Cached& e) {
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
    e.tag = eip;
    e.run = &O::legacy;
    if (!valid(eip, 16))
        return;
    const uint8_t* p = memory_.data() + eip;
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
    if (!run || (o16 && !sized)) { // from its bytes, each time
        const uint32_t tag = e.tag;
        e = Cached{};
        e.tag = tag;
        e.run = &O::legacy;
        return;
    }
    e.run = run;
    e.length = uint8_t(i);
    uint8_t mask[16]{};
    std::memset(mask, 0xff, i);
    std::memcpy(e.code, p, 16);
    std::memcpy(e.mask, mask, 16);
    e.code[0] &= e.mask[0];
    e.code[1] &= e.mask[1];
}

} // namespace reluva::x86
