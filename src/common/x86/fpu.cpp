// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The x87 floating point unit, from Intel's instruction set reference. Its
// registers are kept as doubles: Windows sets it to 53-bit precision, at
// which its results are a double's (the 80-bit format is converted on the
// few instructions that load or store it).

#include "common/x86/cpu.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace reluva::x86 {

namespace {

constexpr uint16_t C0 = 1u << 8, C1 = 1u << 9, C2 = 1u << 10, C3 = 1u << 14;
constexpr uint16_t kTopMask = 7u << 11;

double fromExtended(const uint8_t* p) {
    uint64_t mantissa;
    std::memcpy(&mantissa, p, 8);
    const uint16_t se = uint16_t(p[8] | p[9] << 8);
    const bool negative = se & 0x8000;
    const int exponent = se & 0x7fff;
    double v;
    if (exponent == 0x7fff)
        v = (mantissa << 1) ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
    else if (!exponent && !mantissa)
        v = 0.0;
    else
        v = std::ldexp(double(mantissa), exponent - 16383 - 63);
    return negative ? -v : v;
}

void toExtended(double v, uint8_t* p) {
    uint64_t mantissa = 0;
    uint16_t se = std::signbit(v) ? 0x8000 : 0;
    if (std::isnan(v)) {
        se |= 0x7fff;
        mantissa = 0xc000000000000000ull;
    } else if (std::isinf(v)) {
        se |= 0x7fff;
        mantissa = 0x8000000000000000ull;
    } else if (v != 0.0) {
        int e;
        const double f = std::frexp(std::fabs(v), &e); // [0.5, 1)
        mantissa = uint64_t(std::ldexp(f, 64));
        se |= uint16_t(e - 1 + 16383);
    }
    std::memcpy(p, &mantissa, 8);
    p[8] = uint8_t(se);
    p[9] = uint8_t(se >> 8);
}

} // namespace

void Cpu::fpuPush(double v) {
    fpu.top = uint8_t((fpu.top - 1) & 7);
    fpu.regs[fpu.top] = v;
    fpu.tags &= uint8_t(~(1u << fpu.top));
}

double Cpu::fpuPop() {
    const double v = fpu.regs[fpu.top];
    fpu.tags |= uint8_t(1u << fpu.top);
    fpu.top = uint8_t((fpu.top + 1) & 7);
    return v;
}

double Cpu::fpuRound(double v) const {
    // Precision control 0: 24 bits (a game may set it for speed, or
    // Direct3D for it). It rounds only FADD, FSUB, FMUL, FDIV (and their
    // forms) and FSQRT; the transcendental instructions' results don't
    // follow it (Intel SDM vol. 1, 8.1.5.2).
    return (fpu.control & 0x300) == 0 ? double(float(v)) : v;
}

void Cpu::fpuCompare(double a, double b, bool setEflags, bool) {
    uint16_t c = 0;
    if (std::isnan(a) || std::isnan(b))
        c = C3 | C2 | C0;
    else if (a < b)
        c = C0;
    else if (a == b)
        c = C3;
    if (setEflags) {
        eflags &= ~(ZF | PF | CF | OF | SF | AF);
        if (c & C3)
            eflags |= ZF;
        if (c & C2)
            eflags |= PF;
        if (c & C0)
            eflags |= CF;
    } else {
        fpu.status = uint16_t((fpu.status & ~(C3 | C2 | C1 | C0)) | c);
    }
}

namespace {

// To an integer by the rounding control's mode.
double roundByMode(double v, uint16_t control) {
    switch ((control >> 10) & 3) {
    case 0: return std::nearbyint(v); // the default mode: nearest, ties to even
    case 1: return std::floor(v);
    case 2: return std::ceil(v);
    default: return std::trunc(v);
    }
}

// An integer of `bits` bits, or the "integer indefinite" (its lowest value)
// if it doesn't fit, as the processor stores.
int64_t toInteger(double v, int bits) {
    const double limit = std::ldexp(1.0, bits - 1);
    if (std::isnan(v) || v >= limit || v < -limit)
        return bits == 64 ? INT64_MIN : -(int64_t(1) << (bits - 1));
    return int64_t(v);
}

} // namespace

void Cpu::x87(uint8_t op) {
    decodeModrm();
    const int r = d_.regField;
    const uint32_t a = d_.address;
    auto arith = [&](int kind, double& dst, double x, double y) {
        // dst = x op y for the eight arithmetic forms (2, 3 compare x, y)
        switch (kind) {
        case 0: dst = fpuRound(x + y); break;
        case 1: dst = fpuRound(x * y); break;
        case 2:
        case 3: fpuCompare(x, y, false, false); break;
        case 4: dst = fpuRound(x - y); break;
        case 5: dst = fpuRound(y - x); break;
        case 6: dst = fpuRound(x / y); break;
        case 7: dst = fpuRound(y / x); break;
        }
    };
    auto store16 = [&](int64_t v) { store(a, uint32_t(v), 2); };
    auto store64 = [&](uint64_t v) {
        store(a, uint32_t(v), 4);
        store(a + 4, uint32_t(v >> 32), 4);
    };
    auto load64 = [&]() { return uint64_t(load(a, 4)) | uint64_t(load(a + 4, 4)) << 32; };
    auto loadFloat = [&]() {
        const uint32_t bits = load(a, 4);
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    };
    auto loadDouble = [&]() {
        const uint64_t bits = load64();
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    };
    auto storeFloat = [&](double v) {
        const float f = float(v);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        store(a, bits, 4);
    };
    auto storeDouble = [&](double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        store64(bits);
    };
    auto statusWord = [&]() { return uint16_t((fpu.status & ~kTopMask) | (fpu.top << 11)); };
    auto tagWord = [&]() {
        uint16_t t = 0;
        for (int i = 0; i < 8; ++i) {
            uint16_t tag;
            if (fpu.tags & (1u << i))
                tag = 3;
            else if (fpu.regs[size_t(i)] == 0.0)
                tag = 1;
            else if (!std::isnormal(fpu.regs[size_t(i)]))
                tag = 2;
            else
                tag = 0;
            t |= uint16_t(tag << (2 * i));
        }
        return t;
    };
    auto storeEnvironment = [&]() { // 28 bytes, 32-bit protected mode's layout
        store(a, fpu.control | 0xffff0000u, 4);
        store(a + 4, statusWord() | 0xffff0000u, 4);
        store(a + 8, tagWord() | 0xffff0000u, 4);
        for (uint32_t i = 12; i < 28; i += 4)
            store(a + i, 0, 4);
    };
    auto loadEnvironment = [&]() {
        fpu.control = uint16_t(load(a, 2));
        const uint16_t status = uint16_t(load(a + 4, 2));
        fpu.status = status & ~kTopMask;
        fpu.top = uint8_t((status >> 11) & 7);
        const uint32_t tags = load(a + 8, 2);
        fpu.tags = 0;
        for (int i = 0; i < 8; ++i)
            if (((tags >> (2 * i)) & 3) == 3)
                fpu.tags |= uint8_t(1u << i);
    };
    auto init = [&]() {
        fpu.control = 0x037f;
        fpu.status = 0;
        fpu.tags = 0xff;
        fpu.top = 0;
    };
    auto setC = [&](uint16_t bits) { fpu.status = uint16_t((fpu.status & ~(C3 | C2 | C1 | C0)) | bits); };
    auto fcmov = [&](bool cond) {
        if (cond)
            st(0) = st(d_.rm);
    };

    if (d_.memoryOperand) {
        switch (op) {
        case 0xd8: arith(r, st(0), st(0), loadFloat()); if (r == 3) fpuPop(); return;
        case 0xdc: arith(r, st(0), st(0), loadDouble()); if (r == 3) fpuPop(); return;
        case 0xda: arith(r, st(0), st(0), double(int32_t(load(a, 4)))); if (r == 3) fpuPop(); return;
        case 0xde: arith(r, st(0), st(0), double(int16_t(load(a, 2)))); if (r == 3) fpuPop(); return;
        case 0xd9:
            switch (r) {
            case 0: fpuPush(loadFloat()); return;
            case 2: storeFloat(st(0)); return;
            case 3: storeFloat(fpuPop()); return;
            case 4: loadEnvironment(); return;
            case 5: fpu.control = uint16_t(load(a, 2)); return;
            case 6: storeEnvironment(); return;
            case 7: store(a, fpu.control, 2); return;
            }
            break;
        case 0xdb:
            switch (r) {
            case 0: fpuPush(double(int32_t(load(a, 4)))); return;
            case 1: store(a, uint32_t(toInteger(std::trunc(st(0)), 32)), 4); fpuPop(); return;
            case 2: store(a, uint32_t(toInteger(roundByMode(st(0), fpu.control), 32)), 4); return;
            case 3: store(a, uint32_t(toInteger(roundByMode(st(0), fpu.control), 32)), 4); fpuPop(); return;
            case 5: fpuPush(fromExtended(at(a, 10))); return;
            case 7: toExtended(fpuPop(), at(a, 10)); return;
            }
            break;
        case 0xdd:
            switch (r) {
            case 0: fpuPush(loadDouble()); return;
            case 1: store64(uint64_t(toInteger(std::trunc(st(0)), 64))); fpuPop(); return;
            case 2: storeDouble(st(0)); return;
            case 3: storeDouble(fpuPop()); return;
            case 4: { // FRSTOR
                loadEnvironment();
                for (int i = 0; i < 8; ++i)
                    st(i) = fromExtended(at(a + 28 + 10 * uint32_t(i), 10));
                return;
            }
            case 6: { // FNSAVE
                storeEnvironment();
                for (int i = 0; i < 8; ++i)
                    toExtended(st(i), at(a + 28 + 10 * uint32_t(i), 10));
                init();
                return;
            }
            case 7: store(a, statusWord(), 2); return;
            }
            break;
        case 0xdf:
            switch (r) {
            case 0: fpuPush(double(int16_t(load(a, 2)))); return;
            case 1: store16(toInteger(std::trunc(st(0)), 16)); fpuPop(); return;
            case 2: store16(toInteger(roundByMode(st(0), fpu.control), 16)); return;
            case 3: store16(toInteger(roundByMode(st(0), fpu.control), 16)); fpuPop(); return;
            case 5: fpuPush(double(int64_t(load64()))); return;
            case 7: store64(uint64_t(toInteger(roundByMode(st(0), fpu.control), 64))); fpuPop(); return;
            }
            break;
        }
        fault("x87 instruction " + std::to_string(op) + " /" + std::to_string(r) + " (memory) not implemented");
    }

    const int i = d_.rm;
    const uint8_t modrm = uint8_t(0xc0 | r << 3 | i);
    switch (op) {
    case 0xd8: arith(r, st(0), st(0), st(i)); if (r == 3) fpuPop(); return;
    case 0xdc:
    case 0xde: {
        // st(i) = st(i) op st(0); here the reversed forms come first.
        static const int kSwap[8] = {0, 1, 2, 3, 5, 4, 7, 6};
        if (op == 0xde && r == 3) { // FCOMPP (DE D9)
            if (i != 1)
                break;
            fpuCompare(st(0), st(1), false, false);
            fpuPop();
            fpuPop();
            return;
        }
        if (r == 2 || r == 3) { // FCOM/FCOMP's aliases
            fpuCompare(st(0), st(i), false, false);
            if (r == 3 || op == 0xde)
                fpuPop();
            return;
        }
        arith(kSwap[r], st(i), st(i), st(0));
        if (op == 0xde)
            fpuPop();
        return;
    }
    case 0xd9:
        if (r == 0) {
            fpuPush(st(i));
            return;
        }
        if (r == 1) { // FXCH
            std::swap(st(0), st(i));
            return;
        }
        switch (modrm) {
        case 0xd0: return; // FNOP
        case 0xe0: st(0) = -st(0); return;
        case 0xe1: st(0) = std::fabs(st(0)); return;
        case 0xe4: fpuCompare(st(0), 0.0, false, false); return;
        case 0xe5: { // FXAM
            const double v = st(0);
            uint16_t c = std::signbit(v) ? C1 : 0;
            if (fpu.tags & (1u << fpu.top))
                c |= C3 | C0;
            else if (std::isnan(v))
                c |= C0;
            else if (std::isinf(v))
                c |= C2 | C0;
            else if (v == 0.0)
                c |= C3;
            else if (!std::isnormal(v))
                c |= C3 | C2;
            else
                c |= C2;
            setC(c);
            return;
        }
        case 0xe8: fpuPush(1.0); return;
        case 0xe9: fpuPush(3.321928094887362347870); return; // log2(10)
        case 0xea: fpuPush(1.442695040888963407360); return; // log2(e)
        case 0xeb: fpuPush(3.141592653589793238463); return;
        case 0xec: fpuPush(0.301029995663981195214); return; // log10(2)
        case 0xed: fpuPush(0.693147180559945309417); return; // ln(2)
        case 0xee: fpuPush(0.0); return;
        case 0xf0: st(0) = std::exp2(st(0)) - 1.0; return;
        case 0xf1: { // FYL2X
            const double x = st(0);
            fpuPop();
            st(0) = st(0) * std::log2(x);
            return;
        }
        case 0xf2: // FPTAN
            st(0) = std::tan(st(0));
            fpuPush(1.0);
            setC(0);
            return;
        case 0xf3: { // FPATAN
            const double x = st(0);
            fpuPop();
            st(0) = std::atan2(st(0), x);
            return;
        }
        case 0xf4: { // FXTRACT
            const double v = st(0);
            int e = 0;
            const double f = std::frexp(v, &e);
            st(0) = v == 0.0 ? -std::numeric_limits<double>::infinity() : double(e - 1);
            fpuPush(v == 0.0 ? v : f * 2.0);
            return;
        }
        case 0xf5: // FPREM1
        case 0xf8: { // FPREM
            const double x = st(0), y = st(1);
            const double q = modrm == 0xf8 ? std::trunc(x / y) : std::nearbyint(x / y);
            st(0) = modrm == 0xf8 ? std::fmod(x, y) : std::remainder(x, y);
            const uint64_t qi = std::isfinite(q) ? uint64_t(std::fabs(q)) : 0;
            setC(uint16_t(((qi & 4) ? C0 : 0) | ((qi & 2) ? C3 : 0) | ((qi & 1) ? C1 : 0)));
            return;
        }
        case 0xf6: fpu.top = uint8_t((fpu.top - 1) & 7); return;
        case 0xf7: fpu.top = uint8_t((fpu.top + 1) & 7); return;
        case 0xf9: { // FYL2XP1
            const double x = st(0);
            fpuPop();
            st(0) = st(0) * std::log1p(x) / std::log(2.0);
            return;
        }
        case 0xfa: st(0) = fpuRound(std::sqrt(st(0))); return;
        case 0xfb: { // FSINCOS
            const double v = st(0);
            st(0) = std::sin(v);
            fpuPush(std::cos(v));
            setC(0);
            return;
        }
        case 0xfc: st(0) = roundByMode(st(0), fpu.control); return;
        case 0xfd: st(0) = std::ldexp(st(0), int(std::trunc(st(1)))); return;
        case 0xfe: st(0) = std::sin(st(0)); setC(0); return;
        case 0xff: st(0) = std::cos(st(0)); setC(0); return;
        }
        break;
    case 0xda:
        switch (r) {
        case 0: fcmov(eflags & CF); return;
        case 1: fcmov(eflags & ZF); return;
        case 2: fcmov(eflags & (CF | ZF)); return;
        case 3: fcmov(eflags & PF); return;
        case 5:
            if (i == 1) { // FUCOMPP
                fpuCompare(st(0), st(1), false, true);
                fpuPop();
                fpuPop();
                return;
            }
            break;
        }
        break;
    case 0xdb:
        switch (r) {
        case 0: fcmov(!(eflags & CF)); return;
        case 1: fcmov(!(eflags & ZF)); return;
        case 2: fcmov(!(eflags & (CF | ZF))); return;
        case 3: fcmov(!(eflags & PF)); return;
        case 4:
            if (modrm == 0xe2) { // FNCLEX
                fpu.status &= 0x7f00;
                return;
            }
            if (modrm == 0xe3) {
                init();
                return;
            }
            if (modrm == 0xe0 || modrm == 0xe1 || modrm == 0xe4)
                return; // FENI, FDISI, FSETPM: nothing on later processors
            break;
        case 5: // FUCOMI
        case 6: // FCOMI
            fpuCompare(st(0), st(i), true, r == 5);
            return;
        }
        break;
    case 0xdd:
        switch (r) {
        case 0: fpu.tags |= uint8_t(1u << ((fpu.top + i) & 7)); return; // FFREE
        case 2: st(i) = st(0); return;                                  // FST st(i)
        case 3: st(i) = st(0); fpuPop(); return;                        // FSTP st(i)
        case 4: fpuCompare(st(0), st(i), false, true); return;          // FUCOM
        case 5: fpuCompare(st(0), st(i), false, true); fpuPop(); return; // FUCOMP
        }
        break;
    case 0xdf:
        switch (r) {
        case 0: fpu.tags |= uint8_t(1u << ((fpu.top + i) & 7)); fpuPop(); return; // FFREEP
        case 4:
            if (i == 0) { // FNSTSW AX
                writeReg(2, EAX, statusWord());
                return;
            }
            break;
        case 5: // FUCOMIP
        case 6: // FCOMIP
            fpuCompare(st(0), st(i), true, r == 5);
            fpuPop();
            return;
        }
        break;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02x %02x", op, modrm);
    fault(std::string("x87 instruction ") + buf + " not implemented");
}

} // namespace reluva::x86
