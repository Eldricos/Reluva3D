// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The x86 interpreter's MMX instructions, hand-encoded: each one, its
// saturation edges, shifts past the lane's width, packs and unpacks, the
// memory forms, and the registers' aliasing with the x87 (EMMS, the tag
// word, FSAVE and FRSTOR). The expected values are worked out from Intel's
// instruction set reference.

#include "emulation/x86/cpu.h"
#include "tests/support/test.h"

#include <filesystem>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

using namespace reluva;

#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        const uint64_t a_ = (a), b_ = (b);                                                                             \
        if (a_ != b_) {                                                                                                \
            std::fprintf(stderr, "%s:%d: %s: %016llx, expected %016llx\n", __FILE__, __LINE__, #a,                     \
                         (unsigned long long)a_, (unsigned long long)b_);                                              \
            ++::reluva::test::failures();                                                                              \
        }                                                                                                              \
    } while (0)

namespace {

constexpr uint32_t kCode = 0x2000, kA = 0x3000, kB = 0x3008, kOut = 0x3010;

void put(std::vector<uint8_t> &code, std::initializer_list<int> bytes) {
    for (int b : bytes)
        code.push_back(uint8_t(b));
}
void put32(std::vector<uint8_t> &code, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        code.push_back(uint8_t(v >> (8 * i)));
}
// 0f op modrm disp32 (an absolute memory operand) for register r.
void withMemory(std::vector<uint8_t> &code, int op, int r, uint32_t address) {
    put(code, {0x0f, op, (r << 3) | 5});
    put32(code, address);
}

uint64_t read64(x86::Cpu &cpu, uint32_t a) {
    return cpu.read32(a) | uint64_t(cpu.read32(a + 4)) << 32;
}
void write64(x86::Cpu &cpu, uint32_t a, uint64_t v) {
    cpu.write32(a, uint32_t(v));
    cpu.write32(a + 4, uint32_t(v >> 32));
}

bool run(x86::Cpu &cpu, std::vector<uint8_t> code) {
    code.push_back(0xc3); // ret
    std::copy(code.begin(), code.end(), cpu.memory().begin() + kCode);
    uint32_t result = 0;
    const Error e = cpu.call(kCode, {}, result);
    if (e)
        std::fprintf(stderr, "x86: %s\n", e.message.c_str());
    return !e;
}

// movq mm0, [A]; movq mm1, [B]; op mm0, mm1; movq [OUT], mm0: the result.
// With fromMemory the source is [B] itself.
uint64_t binary(x86::Cpu &cpu, int op, uint64_t a, uint64_t b, bool fromMemory = false) {
    write64(cpu, kA, a);
    write64(cpu, kB, b);
    std::vector<uint8_t> code;
    withMemory(code, 0x6f, 0, kA);
    withMemory(code, 0x6f, 1, kB);
    if (fromMemory)
        withMemory(code, op, 0, kB);
    else
        put(code, {0x0f, op, 0xc1});
    withMemory(code, 0x7f, 0, kOut);
    put(code, {0x0f, 0x77}); // emms
    if (!run(cpu, code))
        return 0xdeadbeefdeadbeefull;
    return read64(cpu, kOut);
}

// movq mm2, [A]; op mm2, imm8 (group /r); movq [OUT], mm2.
uint64_t shiftImm(x86::Cpu &cpu, int op, int r, uint64_t a, int count) {
    write64(cpu, kA, a);
    std::vector<uint8_t> code;
    withMemory(code, 0x6f, 2, kA);
    put(code, {0x0f, op, 0xc0 | (r << 3) | 2, count});
    withMemory(code, 0x7f, 2, kOut);
    put(code, {0x0f, 0x77});
    if (!run(cpu, code))
        return 0xdeadbeefdeadbeefull;
    return read64(cpu, kOut);
}

void testMoves(x86::Cpu &cpu) {
    // movd mm3, eax (zero-extended); movd ecx, mm3; movd [OUT], mm3; movq mm4, mm3; movq [OUT+8], mm4.
    write64(cpu, kOut, ~0ull);
    write64(cpu, kOut + 8, ~0ull);
    std::vector<uint8_t> code;
    put(code, {0xb8, 0x78, 0x56, 0x34, 0x12}); // mov eax, 0x12345678
    put(code, {0x0f, 0x6e, 0xd8});             // movd mm3, eax
    put(code, {0x0f, 0x7e, 0xd9});             // movd ecx, mm3
    put(code, {0x0f, 0x6f, 0xe3});             // movq mm4, mm3
    withMemory(code, 0x7f, 4, kOut + 8);
    withMemory(code, 0x7e, 3, kOut); // movd [OUT], mm3 (4 bytes only)
    put(code, {0x0f, 0x77});
    put(code, {0x89, 0xc8}); // mov eax, ecx
    CHECK(run(cpu, code));
    CHECK_EQ(cpu.reg[x86::Cpu::EAX], 0x12345678u);
    CHECK_EQ(read64(cpu, kOut), 0xffffffff12345678ull);
    CHECK_EQ(read64(cpu, kOut + 8), 0x12345678ull);
    // movd mm5, [A]; movq [mm/m64] register form: movq mm6, mm5 by 0f 7f (mm5 -> mm6).
    write64(cpu, kA, 0xaaaaaaaa87654321ull);
    code.clear();
    withMemory(code, 0x6e, 5, kA);
    put(code, {0x0f, 0x7f, 0xee}); // movq mm6, mm5 (store form, register)
    withMemory(code, 0x7f, 6, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0x87654321ull);
}

void testArithmetic(x86::Cpu &cpu) {
    const uint64_t a = 0x7f80ff017fff8000ull, b = 0x0180ff0200018000ull;
    // Wrapping.
    CHECK_EQ(binary(cpu, 0xfc, a, b), 0x8000fe037f000000ull);                                         // paddb
    CHECK_EQ(binary(cpu, 0xfd, a, b), 0x8100fe0380000000ull);                                         // paddw
    CHECK_EQ(binary(cpu, 0xfe, 0xffffffff7fffffffull, 0x0000000200000001ull), 0x0000000180000000ull); // paddd
    CHECK_EQ(binary(cpu, 0xf8, 0x0001020300000000ull, 0x0102030401010101ull), 0xffffffffffffffffull); // psubb
    CHECK_EQ(binary(cpu, 0xf9, 0x0000800000010000ull, 0x0001000100020001ull), 0xffff7fffffffffffull); // psubw
    CHECK_EQ(binary(cpu, 0xfa, 0x0000000080000000ull, 0x0000000100000001ull), 0xffffffff7fffffffull); // psubd
    // Signed saturation: 0x7f + 0x01 stays 0x7f, 0x80 + 0x80 stays 0x80.
    CHECK_EQ(binary(cpu, 0xec, 0x7f80ff7f00000000ull, 0x0180ff8000000000ull), 0x7f80feff00000000ull); // paddsb
    CHECK_EQ(binary(cpu, 0xed, 0x7fff80007fff0001ull, 0x00018000800000ffull), 0x7fff8000ffff0100ull); // paddsw
    CHECK_EQ(binary(cpu, 0xe8, 0x807f000000000000ull, 0x01ff010000000000ull), 0x807fff0000000000ull); // psubsb
    CHECK_EQ(binary(cpu, 0xe9, 0x80007fff00000005ull, 0x0001ffff7fff0007ull), 0x80007fff8001fffeull); // psubsw
    // Unsigned saturation.
    CHECK_EQ(binary(cpu, 0xdc, 0xff80010000000000ull, 0x0180fe0000000000ull), 0xffffff0000000000ull); // paddusb
    CHECK_EQ(binary(cpu, 0xdd, 0xffff800000010000ull, 0x0001800000020000ull), 0xffffffff00030000ull); // paddusw
    CHECK_EQ(binary(cpu, 0xd8, 0x0010ff0000000000ull, 0x0120fe0000000000ull), 0x0000010000000000ull); // psubusb
    CHECK_EQ(binary(cpu, 0xd9, 0x0010ffff00050000ull, 0x0020000100070000ull), 0x0000fffe00000000ull); // psubusw
    // Multiplies: lanes 0x7fff*0x7fff, 0x8000*0x8000, -1*2, 0x1234*0x10.
    const uint64_t x = 0x7fff8000ffff1234ull, y = 0x7fff800000020010ull;
    CHECK_EQ(binary(cpu, 0xd5, x, y), 0x00010000fffe2340ull); // pmullw
    CHECK_EQ(binary(cpu, 0xe5, x, y), 0x3fff4000ffff0001ull); // pmulhw (signed, the high word: -2 >> 16 is -1)
    // pmaddwd: 0x7fff*0x7fff + 0x8000*0x8000 = 0x3fff0001 + 0x40000000; -1*2 + 0x1234*0x10.
    CHECK_EQ(binary(cpu, 0xf5, x, y), 0x7fff00010001233eull);
    // Both pairs 0x8000 * 0x8000: 2^31, which wraps to 0x80000000.
    CHECK_EQ(binary(cpu, 0xf5, 0x8000800000000000ull, 0x8000800000000000ull), 0x8000000000000000ull);
    // A memory source.
    CHECK_EQ(binary(cpu, 0xfd, 0x0001000200030004ull, 0x0010002000300040ull, true), 0x0011002200330044ull);
}

void testLogicAndCompare(x86::Cpu &cpu) {
    const uint64_t a = 0xf0f0ff00aaaa1234ull, b = 0x0ff0f0f05555ffffull;
    CHECK_EQ(binary(cpu, 0xdb, a, b), a & b);  // pand
    CHECK_EQ(binary(cpu, 0xdf, a, b), ~a & b); // pandn
    CHECK_EQ(binary(cpu, 0xeb, a, b), a | b);  // por
    CHECK_EQ(binary(cpu, 0xef, a, b), a ^ b);  // pxor
    CHECK_EQ(binary(cpu, 0xef, a, a), 0);
    CHECK_EQ(binary(cpu, 0x74, 0x0102030405060708ull, 0x0100030005000700ull), 0xff00ff00ff00ff00ull); // pcmpeqb
    CHECK_EQ(binary(cpu, 0x75, 0x0001000200030004ull, 0x0001000000030000ull), 0xffff0000ffff0000ull); // pcmpeqw
    CHECK_EQ(binary(cpu, 0x76, 0x0000000100000002ull, 0x0000000100000003ull), 0xffffffff00000000ull); // pcmpeqd
    // Signed: 0x01 > 0xff (-1), 0x80 (-128) > 0x7f no, equal no.
    CHECK_EQ(binary(cpu, 0x64, 0x0180050000000000ull, 0xff7f050000000000ull), 0xff00000000000000ull); // pcmpgtb
    CHECK_EQ(binary(cpu, 0x65, 0x0001800000057fffull, 0xffff7fff00058000ull), 0xffff00000000ffffull); // pcmpgtw
    CHECK_EQ(binary(cpu, 0x66, 0x000000017fffffffull, 0xffffffff80000000ull), 0xffffffffffffffffull); // pcmpgtd
    CHECK_EQ(binary(cpu, 0x66, 0x8000000000000000ull, 0x7fffffff00000000ull), 0);
}

void testShifts(x86::Cpu &cpu) {
    const uint64_t v = 0x8001400220047008ull;
    // By register (the count a whole 64-bit value).
    CHECK_EQ(binary(cpu, 0xf1, v, 4), 0x0010002000400080ull);              // psllw
    CHECK_EQ(binary(cpu, 0xd1, v, 4), 0x0800040002000700ull);              // psrlw
    CHECK_EQ(binary(cpu, 0xe1, v, 4), 0xf800040002000700ull);              // psraw
    CHECK_EQ(binary(cpu, 0xf1, v, 15), 0x8000000000000000ull);             // psllw 15
    CHECK_EQ(binary(cpu, 0xf1, v, 16), 0);                                 // psllw past the width
    CHECK_EQ(binary(cpu, 0xd1, v, 16), 0);                                 // psrlw past the width
    CHECK_EQ(binary(cpu, 0xe1, v, 16), 0xffff000000000000ull);             // psraw past: the sign
    CHECK_EQ(binary(cpu, 0xe1, v, 0x100000000ull), 0xffff000000000000ull); // a count in the high dword
    CHECK_EQ(binary(cpu, 0xf1, v, 0x100000000ull), 0);
    CHECK_EQ(binary(cpu, 0xf2, 0x8000000100000003ull, 1), 0x0000000200000006ull);  // pslld
    CHECK_EQ(binary(cpu, 0xd2, 0x8000000100000003ull, 1), 0x4000000000000001ull);  // psrld
    CHECK_EQ(binary(cpu, 0xe2, 0x8000000100000003ull, 1), 0xc000000000000001ull);  // psrad
    CHECK_EQ(binary(cpu, 0xe2, 0x8000000070000000ull, 32), 0xffffffff00000000ull); // psrad past
    CHECK_EQ(binary(cpu, 0xd2, 0x8000000070000000ull, 32), 0);
    CHECK_EQ(binary(cpu, 0xf3, 0x0123456789abcdefull, 4), 0x123456789abcdef0ull); // psllq
    CHECK_EQ(binary(cpu, 0xd3, 0x0123456789abcdefull, 4), 0x00123456789abcdeull); // psrlq
    CHECK_EQ(binary(cpu, 0xf3, 0x0123456789abcdefull, 63), 0x8000000000000000ull);
    CHECK_EQ(binary(cpu, 0xf3, 0x0123456789abcdefull, 64), 0);
    CHECK_EQ(binary(cpu, 0xd3, 0x0123456789abcdefull, 64), 0);
    // A count from memory.
    CHECK_EQ(binary(cpu, 0xd3, 0xff00000000000000ull, 56, true), 0xffull);
    // By an immediate: 71 /6 /2 /4, 72, 73.
    CHECK_EQ(shiftImm(cpu, 0x71, 6, v, 4), 0x0010002000400080ull);
    CHECK_EQ(shiftImm(cpu, 0x71, 2, v, 4), 0x0800040002000700ull);
    CHECK_EQ(shiftImm(cpu, 0x71, 4, v, 4), 0xf800040002000700ull);
    CHECK_EQ(shiftImm(cpu, 0x71, 4, v, 200), 0xffff000000000000ull);
    CHECK_EQ(shiftImm(cpu, 0x71, 2, v, 16), 0);
    CHECK_EQ(shiftImm(cpu, 0x72, 6, 0x8000000100000003ull, 1), 0x0000000200000006ull);
    CHECK_EQ(shiftImm(cpu, 0x72, 2, 0x8000000100000003ull, 1), 0x4000000000000001ull);
    CHECK_EQ(shiftImm(cpu, 0x72, 4, 0x8000000100000003ull, 1), 0xc000000000000001ull);
    CHECK_EQ(shiftImm(cpu, 0x72, 4, 0x8000000100000003ull, 33), 0xffffffff00000000ull);
    CHECK_EQ(shiftImm(cpu, 0x73, 6, 0x0123456789abcdefull, 8), 0x23456789abcdef00ull);
    CHECK_EQ(shiftImm(cpu, 0x73, 2, 0x0123456789abcdefull, 8), 0x000123456789abcdull);
    CHECK_EQ(shiftImm(cpu, 0x73, 2, 0x0123456789abcdefull, 64), 0);
}

void testPackUnpack(x86::Cpu &cpu) {
    // packsswb: dst words 0x0080 (128 -> 127), 0xff7f (-129 -> -128), 5, -1; src 0x7fff, 0x8000, 0x0100, 0.
    CHECK_EQ(binary(cpu, 0x63, 0x0080ff7f0005ffffull, 0x7fff800001000000ull), 0x7f807f007f8005ffull);
    // packuswb: negative to 0, past 255 to 255.
    CHECK_EQ(binary(cpu, 0x67, 0x0080ff7f0005ffffull, 0x7fff800001000000ull), 0xff00ff0080000500ull);
    // packssdw: dst 0x00010000 (65536 -> 32767), 0xffff0000 (-65536 -> -32768); src -5, 300.
    CHECK_EQ(binary(cpu, 0x6b, 0x00010000ffff0000ull, 0xfffffffb0000012cull), 0xfffb012c7fff8000ull);
    const uint64_t a = 0x8877665544332211ull, b = 0xffeeddccbbaa9988ull;
    CHECK_EQ(binary(cpu, 0x60, a, b), 0xbb44aa3399228811ull); // punpcklbw
    CHECK_EQ(binary(cpu, 0x61, a, b), 0xbbaa443399882211ull); // punpcklwd
    CHECK_EQ(binary(cpu, 0x62, a, b), 0xbbaa998844332211ull); // punpckldq
    CHECK_EQ(binary(cpu, 0x68, a, b), 0xff88ee77dd66cc55ull); // punpckhbw
    CHECK_EQ(binary(cpu, 0x69, a, b), 0xffee8877ddcc6655ull); // punpckhwd
    CHECK_EQ(binary(cpu, 0x6a, a, b), 0xffeeddcc88776655ull); // punpckhdq
    // A memory source: the low unpacks read 4 bytes, so one at the end of
    // memory works.
    CHECK_EQ(binary(cpu, 0x60, a, b, true), 0xbb44aa3399228811ull);
    x86::Cpu small(1u << 20);
    small.setStack(0x80000);
    write64(small, kA, a);
    small.write32(0xffffc, 0xbbaa9988);
    std::vector<uint8_t> code;
    withMemory(code, 0x6f, 0, kA);
    withMemory(code, 0x60, 0, 0xffffc);
    withMemory(code, 0x7f, 0, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(small, code));
    CHECK_EQ(read64(small, kOut), 0xbb44aa3399228811ull);
    // punpckhbw there reads 8 bytes: past the end, a fault.
    code.clear();
    withMemory(code, 0x68, 0, 0xffffc);
    std::fprintf(stderr, "(a fault expected:) ");
    CHECK(!run(small, code));
}

void testAliasing(x86::Cpu &cpu) {
    // fninit; fld1: 1.0 in physical register 7; movq [OUT], mm7: its mantissa.
    std::vector<uint8_t> code;
    put(code, {0xdb, 0xe3, 0xd9, 0xe8});
    withMemory(code, 0x7f, 7, kOut);
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0x8000000000000000ull);
    // Every MMX instruction but EMMS sets the stack top to 0 and tags all valid.
    CHECK(cpu.fpu.top == 0 && cpu.fpu.tags == 0);
    run(cpu, {0x0f, 0x77});
    CHECK(cpu.fpu.tags == 0xff);
    // fild qword: the integer's mantissa exactly (3 << 62 for 3, and a 64-bit one).
    write64(cpu, kA, 0x0123456789abcdefull);
    code.clear();
    put(code, {0xdb, 0xe3, 0xdf, 0x2d});
    put32(code, kA); // fild qword [A]
    withMemory(code, 0x7f, 7, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0x0123456789abcdefull << 7);
    // An MMX write makes the register a NaN to the x87 (exponent and sign
    // all ones): movq mm0, [A]; fstp tword [OUT]: the 64 bits, then 0xffff.
    write64(cpu, kA, 0x0000000000001234ull);
    code.clear();
    withMemory(code, 0x6f, 0, kA);
    put(code, {0xdb, 0x3d});
    put32(code, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0x1234);
    CHECK(cpu.read16(kOut + 8) == 0xffff);
    // fld tword [OUT] loads it back; movq [OUT+16], mm7: the same 64 bits.
    code.clear();
    put(code, {0xdb, 0xe3, 0xdb, 0x2d});
    put32(code, kOut);
    withMemory(code, 0x7f, 7, kOut + 16);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut + 16), 0x1234);
    // The x87 overwrites MMX: movq mm0, [A]; emms; fld1 (into register 7);
    // fld1 ... eight pushes reach register 0: then mm0 is 1.0's mantissa.
    code.clear();
    withMemory(code, 0x6f, 0, kA);
    put(code, {0x0f, 0x77, 0xdb, 0xe3});
    for (int i = 0; i < 8; ++i)
        put(code, {0xd9, 0xe8});
    withMemory(code, 0x7f, 0, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0x8000000000000000ull);
    // FSAVE and FRSTOR keep the 64 bits: movq mm3, [A]; fnsave [0x3100];
    // pxor mm3, mm3; frstor [0x3100]; movq [OUT], mm3.
    write64(cpu, kA, 0xfedcba9876543210ull);
    code.clear();
    withMemory(code, 0x6f, 3, kA);
    put(code, {0xdd, 0x35});
    put32(code, 0x3100);
    put(code, {0x0f, 0xef, 0xdb});
    put(code, {0xdd, 0x25});
    put32(code, 0x3100);
    withMemory(code, 0x7f, 3, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, 0x3100 + 28 + 30), 0xfedcba9876543210ull); // st(3) is register 3 (top 0)
    CHECK(cpu.read16(0x3100 + 28 + 38) == 0xffff);
    CHECK_EQ(read64(cpu, kOut), 0xfedcba9876543210ull);
    // FXCH moves an MMX value with its register: movq mm0, [A]; emms
    // leaves top 0; fxch st(1); movq [OUT], mm1.
    code.clear();
    withMemory(code, 0x6f, 0, kA);
    put(code, {0x0f, 0x77, 0xd9, 0xc9});
    withMemory(code, 0x7f, 1, kOut);
    put(code, {0x0f, 0x77});
    CHECK(run(cpu, code));
    CHECK_EQ(read64(cpu, kOut), 0xfedcba9876543210ull);
    // The registers are the thread's: saved and loaded with the context.
    cpu.mmWrite(5, 0x1122334455667788ull);
    const x86::Cpu::Context saved = cpu.saveContext();
    cpu.mmWrite(5, 0);
    cpu.loadContext(saved);
    CHECK_EQ(cpu.mmRead(5), 0x1122334455667788ull);
    // The SSE forms (66 0f ...) are not MMX: a fault, not a wrong result.
    std::fprintf(stderr, "(a fault expected:) ");
    CHECK(!run(cpu, {0x66, 0x0f, 0xef, 0xc0}));
}

void testCpuid(x86::Cpu &cpu) {
    // mov eax, 1; cpuid; mov eax, edx
    const std::vector<uint8_t> code = {0xb8, 1, 0, 0, 0, 0x0f, 0xa2, 0x89, 0xd0};
    CHECK(run(cpu, code));
    CHECK(!(cpu.reg[x86::Cpu::EAX] & (1u << 23))); // no MMX by default
    cpu.reportMmx = true;
    CHECK(run(cpu, code));
    CHECK(cpu.reg[x86::Cpu::EAX] & (1u << 23));
    cpu.reportMmx = false;
}

} // namespace

static void runAll() {
    x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    testMoves(cpu);
    testArithmetic(cpu);
    testLogicAndCompare(cpu);
    testShifts(cpu);
    testPackUnpack(cpu);
    testAliasing(cpu);
    testCpuid(cpu);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.mmx", {
                                                               {"all", [](const std::filesystem::path &) { runAll(); }},
                                                           });
