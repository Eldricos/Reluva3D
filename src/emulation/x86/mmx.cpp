// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The MMX instructions of the Pentium MMX, from Intel's instruction set
// reference. The eight 64-bit registers are the x87 registers' mantissas
// (MMi is physical register i, whatever the stack top): every MMX
// instruction but EMMS sets the stack top to 0 and tags all eight valid,
// one that writes MMi sets its exponent and sign to all ones (a NaN to the
// x87), and EMMS tags them all empty. Cpu::Fpu says how the 64 bits are
// kept beside the doubles.

#include "emulation/x86/cpu.h"

#include <cmath>
#include <cstring>

namespace reluva::x86 {

namespace {

uint64_t bitsOf(double v) {
    uint64_t b;
    std::memcpy(&b, &v, 8);
    return b;
}

// What an MMX write leaves in the double: a NaN (exponent all ones, sign
// set, as the 80-bit register gets), one for each register so that a copy
// of it elsewhere on the stack is not taken for that register's own.
double mmxNaN(int i) {
    const uint64_t b = 0xfff84d4d58000000ull | uint64_t(i);
    double v;
    std::memcpy(&v, &b, 8);
    return v;
}

// The 64-bit mantissa of a double as the 80-bit format holds it.
uint64_t mantissaOf(double v) {
    if (std::isnan(v))
        return 0xc000000000000000ull;
    if (std::isinf(v))
        return 0x8000000000000000ull;
    if (v == 0.0)
        return 0;
    int e;
    const double f = std::frexp(std::fabs(v), &e); // [0.5, 1)
    return uint64_t(std::ldexp(f, 64));
}

// The same for a 64-bit integer, exactly.
uint64_t mantissaOf(int64_t v) {
    uint64_t m = v < 0 ? uint64_t(0) - uint64_t(v) : uint64_t(v);
    if (m)
        while (!(m & 0x8000000000000000ull))
            m <<= 1;
    return m;
}

// Lanes of `bits` bits (8, 16, 32): f applied to each pair.
template<int bits, typename F>
uint64_t lanes(uint64_t a, uint64_t b, F f) {
    constexpr uint64_t m = bits == 64 ? ~0ull : (uint64_t(1) << bits) - 1;
    uint64_t r = 0;
    for (int i = 0; i < 64; i += bits)
        r |= (uint64_t(f((a >> i) & m, (b >> i) & m)) & m) << i;
    return r;
}

template<int bits>
int64_t sx(uint64_t v) {
    return int64_t(v << (64 - bits)) >> (64 - bits);
}

int64_t clampTo(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

uint64_t unpack(uint64_t a, uint64_t b, int bits, bool high) {
    // Interleaves the low (or high) halves of a and b: a's lane first.
    const uint64_t m = bits == 32 ? 0xffffffffull : (uint64_t(1) << bits) - 1;
    const int n = 32 / bits;
    const int from = high ? 32 : 0;
    uint64_t r = 0;
    for (int i = 0; i < n; ++i) {
        r |= ((a >> (from + i * bits)) & m) << (2 * i * bits);
        r |= ((b >> (from + i * bits)) & m) << ((2 * i + 1) * bits);
    }
    return r;
}

// Each lane shifted by `count` (the whole 64-bit count: past the lane's
// width a logical shift gives 0, an arithmetic one the sign).
template<int bits>
uint64_t shiftLanes(uint64_t v, uint64_t count, int kind) { // kind 0 left, 1 logical right, 2 arithmetic right
    if (count >= uint64_t(bits)) {
        if (kind != 2)
            return 0;
        count = bits - 1;
    }
    const int c = int(count);
    return lanes<bits>(v, 0, [&](uint64_t x, uint64_t) -> uint64_t {
        if (kind == 0)
            return x << c;
        if (kind == 1)
            return x >> c;
        return uint64_t(sx<bits>(x) >> c);
    });
}

uint64_t shiftBy(uint64_t v, uint64_t count, int bits, int kind) {
    if (bits == 16)
        return shiftLanes<16>(v, count, kind);
    if (bits == 32)
        return shiftLanes<32>(v, count, kind);
    if (count >= 64)
        return 0;
    return kind == 0 ? v << count : v >> count;
}

} // namespace

uint64_t Cpu::mmRead(int i) const {
    const unsigned bit = 1u << i;
    const double v = fpu.regs[size_t(i)];
    if ((fpu.mmValid & bit) && bitsOf(v) == fpu.mmDouble[size_t(i)])
        return fpu.mm[size_t(i)];
    if ((fpu.exactValid & bit) && double(fpu.exact[size_t(i)]) == v)
        return mantissaOf(fpu.exact[size_t(i)]);
    return mantissaOf(v);
}

void Cpu::mmWrite(int i, uint64_t v) {
    const unsigned bit = 1u << i;
    fpu.regs[size_t(i)] = mmxNaN(i);
    fpu.mm[size_t(i)] = v;
    fpu.mmDouble[size_t(i)] = bitsOf(fpu.regs[size_t(i)]);
    fpu.mmValid |= uint8_t(bit);
    fpu.exactValid &= uint8_t(~bit);
}

void Cpu::mmx(uint8_t op) {
    if (op == 0x77) { // EMMS
        fpu.tags = 0xff;
        return;
    }
    if (d_.opSize16 || d_.rep || d_.repne) // the SSE forms
        fault("instruction " +
              std::string(d_.opSize16 ? "66"
                          : d_.rep    ? "f3"
                                      : "f2") +
              " 0f " + std::to_string(op) + " (SSE) not implemented");
    decodeModrm();
    fpu.top = 0;
    fpu.tags = 0;
    const int r = d_.regField;
    // The source operand: an MMX register or 64 bits of memory.
    auto source = [&]() { return d_.memoryOperand ? loadQword(d_.address) : mmRead(d_.rm); };
    // The low unpacks read 32 bits of memory.
    auto sourceLow = [&]() { return d_.memoryOperand ? uint64_t(load(d_.address, 4)) : mmRead(d_.rm); };
    const uint64_t dst = mmRead(r);
    uint64_t result;
    switch (op) {
    case 0x60:
        result = unpack(dst, sourceLow(), 8, false);
        break; // PUNPCKLBW
    case 0x61:
        result = unpack(dst, sourceLow(), 16, false);
        break; // PUNPCKLWD
    case 0x62:
        result = unpack(dst, sourceLow(), 32, false);
        break; // PUNPCKLDQ
    case 0x68:
        result = unpack(dst, source(), 8, true);
        break; // PUNPCKHBW
    case 0x69:
        result = unpack(dst, source(), 16, true);
        break; // PUNPCKHWD
    case 0x6a:
        result = unpack(dst, source(), 32, true);
        break;   // PUNPCKHDQ
    case 0x63:   // PACKSSWB
    case 0x67:   // PACKUSWB
    case 0x6b: { // PACKSSDW
        const uint64_t src = source();
        const bool words = op == 0x6b;
        const int from = words ? 32 : 16;
        const int to = words ? 16 : 8;
        const int n = 64 / from;
        const int64_t lo = op == 0x67 ? 0 : words ? -32768 : -128;
        const int64_t hi = op == 0x67 ? 255 : words ? 32767 : 127;
        result = 0;
        for (int i = 0; i < 2 * n; ++i) {
            const uint64_t v = ((i < n ? dst : src) >> ((i % n) * from)) & (words ? 0xffffffffull : 0xffffull);
            const int64_t s = words ? sx<32>(v) : sx<16>(v);
            result |= (uint64_t(clampTo(s, lo, hi)) & ((uint64_t(1) << to) - 1)) << (i * to);
        }
        break;
    }
    case 0x64:
        result = lanes<8>(dst, source(), [](uint64_t a, uint64_t b) { return sx<8>(a) > sx<8>(b) ? ~0ull : 0; });
        break; // PCMPGTB
    case 0x65:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return sx<16>(a) > sx<16>(b) ? ~0ull : 0; });
        break; // PCMPGTW
    case 0x66:
        result = lanes<32>(dst, source(), [](uint64_t a, uint64_t b) { return sx<32>(a) > sx<32>(b) ? ~0ull : 0; });
        break; // PCMPGTD
    case 0x74:
        result = lanes<8>(dst, source(), [](uint64_t a, uint64_t b) { return a == b ? ~0ull : 0; });
        break; // PCMPEQB
    case 0x75:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return a == b ? ~0ull : 0; });
        break; // PCMPEQW
    case 0x76:
        result = lanes<32>(dst, source(), [](uint64_t a, uint64_t b) { return a == b ? ~0ull : 0; });
        break; // PCMPEQD
    case 0x6e: // MOVD mm, r/m32
        result = d_.memoryOperand ? load(d_.address, 4) : reg[d_.rm];
        break;
    case 0x6f:
        result = source();
        break; // MOVQ mm, mm/m64
    case 0x7e: // MOVD r/m32, mm
        if (d_.memoryOperand)
            store(d_.address, uint32_t(dst), 4);
        else
            reg[d_.rm] = uint32_t(dst);
        return;
    case 0x7f: // MOVQ mm/m64, mm
        if (d_.memoryOperand)
            storeQword(d_.address, dst);
        else
            mmWrite(d_.rm, dst);
        return;
    case 0x71:   // PSRLW/PSRAW/PSLLW mm, imm8
    case 0x72:   // PSRLD/PSRAD/PSLLD mm, imm8
    case 0x73: { // PSRLQ/PSLLQ mm, imm8
        if (d_.memoryOperand)
            fault("undefined instruction 0f " + std::to_string(op) + " with a memory operand");
        const uint64_t count = fetch8();
        const int kind = r == 6 ? 0 : r == 2 ? 1 : r == 4 && op != 0x73 ? 2 : -1;
        if (kind < 0)
            fault("undefined instruction 0f " + std::to_string(op) + " /" + std::to_string(r));
        mmWrite(d_.rm, shiftBy(mmRead(d_.rm), count, op == 0x71 ? 16 : op == 0x72 ? 32 : 64, kind));
        return;
    }
    case 0xd1:
        result = shiftBy(dst, source(), 16, 1);
        break; // PSRLW
    case 0xd2:
        result = shiftBy(dst, source(), 32, 1);
        break; // PSRLD
    case 0xd3:
        result = shiftBy(dst, source(), 64, 1);
        break; // PSRLQ
    case 0xe1:
        result = shiftBy(dst, source(), 16, 2);
        break; // PSRAW
    case 0xe2:
        result = shiftBy(dst, source(), 32, 2);
        break; // PSRAD
    case 0xf1:
        result = shiftBy(dst, source(), 16, 0);
        break; // PSLLW
    case 0xf2:
        result = shiftBy(dst, source(), 32, 0);
        break; // PSLLD
    case 0xf3:
        result = shiftBy(dst, source(), 64, 0);
        break; // PSLLQ
    case 0xd5:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return uint64_t(sx<16>(a) * sx<16>(b)); });
        break; // PMULLW
    case 0xe5:
        result =
            lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return uint64_t((sx<16>(a) * sx<16>(b)) >> 16); });
        break; // PMULHW
    case 0xf5: // PMADDWD: two products of words summed into each dword (0x8000 * 0x8000 twice wraps to 0x80000000)
        result = lanes<32>(dst, source(), [](uint64_t a, uint64_t b) {
            return uint64_t(sx<16>(a) * sx<16>(b) + sx<16>(a >> 16) * sx<16>(b >> 16));
        });
        break;
    case 0xdb:
        result = dst & source();
        break; // PAND
    case 0xdf:
        result = ~dst & source();
        break; // PANDN
    case 0xeb:
        result = dst | source();
        break; // POR
    case 0xef:
        result = dst ^ source();
        break; // PXOR
    case 0xfc:
        result = lanes<8>(dst, source(), [](uint64_t a, uint64_t b) { return a + b; });
        break; // PADDB
    case 0xfd:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return a + b; });
        break; // PADDW
    case 0xfe:
        result = lanes<32>(dst, source(), [](uint64_t a, uint64_t b) { return a + b; });
        break; // PADDD
    case 0xf8:
        result = lanes<8>(dst, source(), [](uint64_t a, uint64_t b) { return a - b; });
        break; // PSUBB
    case 0xf9:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) { return a - b; });
        break; // PSUBW
    case 0xfa:
        result = lanes<32>(dst, source(), [](uint64_t a, uint64_t b) { return a - b; });
        break; // PSUBD
    case 0xec:
        result = lanes<8>(dst, source(),
                          [](uint64_t a, uint64_t b) { return uint64_t(clampTo(sx<8>(a) + sx<8>(b), -128, 127)); });
        break; // PADDSB
    case 0xed:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) {
            return uint64_t(clampTo(sx<16>(a) + sx<16>(b), -32768, 32767));
        });
        break; // PADDSW
    case 0xe8:
        result = lanes<8>(dst, source(),
                          [](uint64_t a, uint64_t b) { return uint64_t(clampTo(sx<8>(a) - sx<8>(b), -128, 127)); });
        break; // PSUBSB
    case 0xe9:
        result = lanes<16>(dst, source(), [](uint64_t a, uint64_t b) {
            return uint64_t(clampTo(sx<16>(a) - sx<16>(b), -32768, 32767));
        });
        break; // PSUBSW
    case 0xdc:
        result =
            lanes<8>(dst, source(), [](uint64_t a, uint64_t b) { return uint64_t(clampTo(int64_t(a + b), 0, 255)); });
        break; // PADDUSB
    case 0xdd:
        result = lanes<16>(dst, source(),
                           [](uint64_t a, uint64_t b) { return uint64_t(clampTo(int64_t(a + b), 0, 65535)); });
        break; // PADDUSW
    case 0xd8:
        result = lanes<8>(dst, source(),
                          [](uint64_t a, uint64_t b) { return uint64_t(clampTo(int64_t(a) - int64_t(b), 0, 255)); });
        break; // PSUBUSB
    case 0xd9:
        result = lanes<16>(dst, source(),
                           [](uint64_t a, uint64_t b) { return uint64_t(clampTo(int64_t(a) - int64_t(b), 0, 65535)); });
        break; // PSUBUSW
    default:
        fault("instruction 0f " + std::to_string(op) + " not implemented (not an MMX instruction of the Pentium MMX)");
    }
    mmWrite(r, result);
}

} // namespace reluva::x86
