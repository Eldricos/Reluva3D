// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The x86 emulator's decoded-instruction cache and its native code (JIT):
// the instructions with the cache and the JIT on and off, random
// instruction sequences compared between them, faults from native code,
// and the test DLL both ways.
//
//   reluva-tests common.unit.jit

#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "tests/common/data/x86_testdll.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace reluva;


// Runs `code` (hand-encoded, ending in RET) as a function of no arguments.
static uint32_t runX86(x86::Cpu& cpu, std::vector<uint8_t> code, bool* ok = nullptr) {
    const uint32_t at = 0x2000;
    std::copy(code.begin(), code.end(), cpu.memory().begin() + at);
    uint32_t result = 0;
    const Error e = cpu.call(at, {}, result);
    if (ok)
        *ok = !e;
    else if (e)
        std::fprintf(stderr, "x86: %s\n", e.message.c_str());
    return result;
}

// Runs `code` with the registers and flags as they are, giving whether it
// failed.
static bool runX86Keeping(x86::Cpu& cpu, const std::vector<uint8_t>& code, uint32_t& result) {
    std::copy(code.begin(), code.end(), cpu.memory().begin() + 0x2000);
    return bool(cpu.call(0x2000, {}, result));
}

static void testX86Instructions(bool cached) {
    x86::Cpu cpu(16u << 20);
    cpu.cacheOn = cached;
    cpu.setStack(0x100000);
    // In memory: past the first page, up to the end.
    const uint32_t size = 16u << 20;
    CHECK(!cpu.valid(0, 1) && !cpu.valid(4095, 1) && !cpu.valid(4095, 2) && !cpu.valid(0xfffff000u, 0x2000));
    CHECK(cpu.valid(4096, 1) && cpu.valid(4096, size - 4096) && !cpu.valid(4096, size - 4095));
    CHECK(cpu.valid(size - 4, 4) && !cpu.valid(size - 3, 4) && cpu.valid(size, 0) && !cpu.valid(size, 1));
    CHECK(!cpu.valid(0xffffffffu, 2) && !cpu.valid(8192, 0xffffffffu));
    {
        // Moves, LEA, PUSH and POP (made native by the Jit where it can):
        // mov eax, 7; push eax; pop edx; lea eax, [edx + edx*2 + 1]; ret  (22)
        CHECK(runX86(cpu, {0xb8, 7, 0, 0, 0, 0x50, 0x5a, 0x8d, 0x44, 0x52, 0x01, 0xc3}) == 22);
        // mov eax, 0; mov ah, 5; mov dl, ah; mov ecx, 0x3000; mov [ecx], dl;
        // mov bl, [ecx]; movzx eax, bl; ret  (5)
        CHECK(runX86(cpu, {0xb8, 0, 0, 0, 0, 0xc6, 0xc4, 5, 0x88, 0xe2, 0xb9, 0, 0x30, 0, 0, 0x88, 0x11, 0x8a, 0x19,
                           0x0f, 0xb6, 0xc3, 0xc3}) == 5);
        // A fault names its instruction and leaves the registers as they were
        // when it came: mov ecx, 0x10; mov eax, [ecx]; ret
        bool ok = true;
        runX86(cpu, {0xb9, 0x10, 0, 0, 0, 0x8b, 0x01, 0xc3}, &ok);
        CHECK(!ok && cpu.faultEip == 0x2005 && cpu.faultReg[1] == 0x10);
        // mov esp, 0x800; push eax; ret: ESP down before the store faults.
        runX86(cpu, {0xbc, 0, 8, 0, 0, 0x50, 0xc3}, &ok);
        CHECK(!ok && cpu.faultEip == 0x2005 && cpu.faultReg[4] == 0x7fc);
        // mov ecx, 0x10; add eax, [ecx]; ret: through a handler.
        runX86(cpu, {0xb9, 0x10, 0, 0, 0, 0x03, 0x01, 0xc3}, &ok);
        CHECK(!ok && cpu.faultEip == 0x2005);
        // And on after them.
        CHECK(runX86(cpu, {0xb8, 7, 0, 0, 0, 0x50, 0x5a, 0x8d, 0x44, 0x52, 0x01, 0xc3}) == 22);
        // Shifts and their flags: mov eax, 0x80000001; shl eax, 1; pushfd;
        // pop edx; shr eax, 1; pushfd; pop ecx; sar eax, 1; lea eax,
        // [eax + edx]; add eax, ecx; ret (flags as the handlers have them).
        {
            const std::vector<uint8_t> shifts = {0xb8, 1, 0, 0, 0x80, 0xd1, 0xe0, 0x9c, 0x5a, 0xd1, 0xe8, 0x9c, 0x59,
                                                 0xd1, 0xf8, 0x8d, 0x04, 0x10, 0x01, 0xc8, 0xc3};
            const std::vector<uint8_t> byImm = {0xb8, 1, 0, 0, 0x80, 0xc1, 0xe0, 1, 0x9c, 0x5a, 0xc1, 0xe8, 1, 0x9c, 0x59,
                                                0xc1, 0xf8, 1, 0x8d, 0x04, 0x10, 0x01, 0xc8, 0xc3};
            CHECK(runX86(cpu, shifts) == runX86(cpu, byImm));
        }
        // FCOM, FNSTSW AX and TEST AH: fld1; fldz; fcomp st(1); fnstsw ax;
        // test ah, 0x41; setnz al; fstp st(0); and eax, C3 C2 C1 C0 and AL's
        // bit; ret (C0: less).
        CHECK(runX86(cpu, {0xd9, 0xe8, 0xd9, 0xee, 0xd8, 0xd9, 0xdf, 0xe0, 0xf6, 0xc4, 0x41, 0x0f, 0x95, 0xc0, 0xdd, 0xd8,
                           0x25, 0x01, 0x47, 0, 0, 0xc3}) == 0x101);
    }
    // mov eax, 5; add eax, 0xfffffffb; setz al; movzx eax, al; ret
    CHECK(runX86(cpu, {0xb8, 5, 0, 0, 0, 0x83, 0xc0, 0xfb, 0x0f, 0x94, 0xc0, 0x0f, 0xb6, 0xc0, 0xc3}) == 1);
    // mov ecx, 10; xor eax, eax; loop: add eax, ecx; dec ecx; jnz loop; ret  (55)
    CHECK(runX86(cpu, {0xb9, 10, 0, 0, 0, 0x31, 0xc0, 0x01, 0xc8, 0x49, 0x75, 0xfb, 0xc3}) == 55);
    // mov eax, -7; cdq; mov ecx, 2; idiv ecx; imul eax, edx, 100; ret  (edx = -1, eax = -100)
    CHECK(runX86(cpu, {0xb8, 0xf9, 0xff, 0xff, 0xff, 0x99, 0xb9, 2, 0, 0, 0, 0xf7, 0xf9, 0x6b, 0xc2, 100, 0xc3}) ==
          uint32_t(-100));
    // mov eax, 0x80000000; sar eax, 31; shr eax, 28; ret  (0xf)
    CHECK(runX86(cpu, {0xb8, 0, 0, 0, 0x80, 0xc1, 0xf8, 31, 0xc1, 0xe8, 28, 0xc3}) == 0xf);
    // mov eax, 0x12345678; bswap eax; ret
    CHECK(runX86(cpu, {0xb8, 0x78, 0x56, 0x34, 0x12, 0x0f, 0xc8, 0xc3}) == 0x78563412);
    // rep stosb then repe cmpsb: mov edi, 0x3000; mov ecx, 8; mov al, 0x41; rep stosb;
    // mov esi, 0x3000; mov edi, 0x3004; mov ecx, 4; repe cmpsb; mov eax, ecx; ret  (0: all equal)
    CHECK(runX86(cpu, {0xbf, 0, 0x30, 0, 0, 0xb9, 8, 0, 0, 0, 0xb0, 0x41, 0xf3, 0xaa, 0xbe, 0, 0x30, 0, 0, 0xbf, 4, 0x30,
                       0, 0, 0xb9, 4, 0, 0, 0, 0xf3, 0xa6, 0x89, 0xc8, 0xc3}) == 0);
    CHECK(cpu.read32(0x3000) == 0x41414141);
    // x87: fld1; fld1; faddp; fsqrt; fstp dword [0x3000]; mov eax, [0x3000]; ret  (sqrt 2 as a float)
    runX86(cpu, {0xd9, 0xe8, 0xd9, 0xe8, 0xde, 0xc1, 0xd9, 0xfa, 0xd9, 0x1d, 0, 0x30, 0, 0, 0xa1, 0, 0x30, 0, 0, 0xc3});
    float f;
    const uint32_t bitsOfF = cpu.read32(0x3000);
    std::memcpy(&f, &bitsOfF, 4);
    CHECK(f == std::sqrt(2.0f));
    // fs: mov eax, fs:[4]; ret
    cpu.fsBase = 0x4000;
    cpu.write32(0x4004, 0xabcd);
    CHECK(runX86(cpu, {0x64, 0xa1, 4, 0, 0, 0, 0xc3}) == 0xabcd);
    // Faults: a null read, and division by zero, fail the call.
    bool ok = true;
    runX86(cpu, {0xa1, 0, 0, 0, 0, 0xc3}, &ok);
    CHECK(!ok);
    runX86(cpu, {0x31, 0xc9, 0xf7, 0xf1, 0xc3}, &ok);
    CHECK(!ok);
    // A native function, stdcall with one argument: push 20; call [native]; ret
    const uint32_t native = cpu.addNative("double", [](x86::Cpu& c) {
        c.reg[x86::Cpu::EAX] = c.arg(0) * 2;
        return 4u;
    });
    cpu.write32(0x3100, native);
    CHECK(runX86(cpu, {0x6a, 20, 0xff, 0x15, 0, 0x31, 0, 0, 0xc3}) == 40);
    // Code that changes is run as it now is: mov eax, imm; ret, run, then
    // its immediate rewritten.
    CHECK(runX86(cpu, {0xb8, 1, 0, 0, 0, 0xc3}) == 1);
    cpu.write32(0x2001, 2);
    CHECK(runX86(cpu, {0xb8, 2, 0, 0, 0, 0xc3}) == 2);
    // ...and by the program itself, after it's been run: mov byte [0x2011], n;
    // jmp 0x2010 (mov eax, 5; ret), with n 7 and then 9.
    for (uint8_t n : {7, 9}) {
        std::vector<uint8_t> code = {0xc6, 0x05, 0x11, 0x20, 0, 0, n, 0xe9, 4, 0, 0, 0};
        code.resize(16, 0x90);
        code.insert(code.end(), {0xb8, 5, 0, 0, 0, 0xc3});
        CHECK(runX86(cpu, code) == n);
    }
}

// The decoded-instruction cache against running from the bytes: random
// runs of the instructions it has handlers for (and some it hasn't), with
// operands in registers and memory, give the same registers, flags, x87
// state and memory both ways.
static void testX86CacheAgreement() {
    std::mt19937 random(1234);
    auto pick = [&](uint32_t n) { return uint32_t(random() % n); };
    const uint32_t kData = 0x10000, kFs = 0x100;
    // Registers written as 16 or 32 bits: not ESP or EBP (the stack and the data's base).
    auto reg32 = [&] {
        static const uint8_t kRegs[6] = {0, 1, 2, 3, 6, 7};
        return kRegs[pick(6)];
    };
    auto instruction = [&](std::vector<uint8_t>& out) {
        std::vector<uint8_t> b;
        const bool o16 = pick(6) == 0;
        // A ModRM operand: a register, [ebp+d8], [ebp+d32], [d32] (through a
        // SIB), sometimes with FS.
        auto modrm = [&](int reg, bool wordReg, bool memoryOnly = false) {
            const int kind = memoryOnly ? 1 + int(pick(3)) : int(pick(4));
            if (kind == 0) {
                b.push_back(uint8_t(0xc0 | reg << 3 | (wordReg ? reg32() : pick(8))));
            } else if (kind == 1) {
                b.push_back(uint8_t(0x45 | reg << 3));
                b.push_back(uint8_t(pick(0x78)));
            } else if (kind == 2) {
                b.push_back(uint8_t(0x85 | reg << 3));
                const uint32_t d = pick(0x78);
                b.insert(b.end(), {uint8_t(d), 0, 0, 0});
            } else {
                b.push_back(uint8_t(0x04 | reg << 3));
                b.push_back(0x25);
                const uint32_t d = kData + pick(0x78);
                b.insert(b.end(), {uint8_t(d), uint8_t(d >> 8), uint8_t(d >> 16), 0});
            }
            if (kind != 0 && pick(4) == 0)
                b.insert(b.begin(), 0x64);
        };
        auto imm = [&](int bytes) {
            for (int i = 0; i < bytes; ++i)
                b.push_back(uint8_t(random()));
        };
        const int wordSize = o16 ? 2 : 4;
        switch (pick(24)) {
        case 0:
        case 1:
        case 2: { // ALU r/m, r and r, r/m
            const uint8_t op = uint8_t(pick(8) << 3 | pick(4));
            b.push_back(op);
            modrm((op & 2) ? ((op & 1) ? reg32() : int(pick(8))) : int(pick(8)), op & 1);
            break;
        }
        case 3: // ALU AL/EAX, imm
            b.push_back(uint8_t(pick(8) << 3 | 4 | pick(2)));
            imm((b.back() & 1) ? wordSize : 1);
            break;
        case 4: { // ALU r/m, imm
            const uint8_t op = std::array<uint8_t, 3>{0x80, 0x81, 0x83}[pick(3)];
            b.push_back(op);
            modrm(int(pick(8)), op != 0x80);
            imm(op == 0x81 ? wordSize : 1);
            break;
        }
        case 5: b.push_back(uint8_t(0x40 | pick(2) << 3 | reg32())); break; // INC, DEC
        case 6: // TEST
            b.push_back(uint8_t(0x84 | pick(2)));
            modrm(int(pick(8)), b.back() & 1);
            break;
        case 7:
        case 8: { // MOV
            const uint8_t op = uint8_t(0x88 | pick(4));
            b.push_back(op);
            modrm((op & 2) && (op & 1) ? reg32() : int(pick(8)), op & 1);
            break;
        }
        case 9: // MOV r, imm
            b.push_back(pick(2) ? uint8_t(0xb0 | pick(8)) : uint8_t(0xb8 | reg32()));
            imm(b.back() >= 0xb8 ? wordSize : 1);
            break;
        case 10: { // MOV r/m, imm
            const uint8_t op = uint8_t(0xc6 | pick(2));
            b.push_back(op);
            modrm(0, op & 1);
            imm((op & 1) ? wordSize : 1);
            break;
        }
        case 11: // LEA
            b.push_back(0x8d);
            modrm(reg32(), true, true);
            break;
        case 12: { // shifts and rotates
            const uint8_t op = std::array<uint8_t, 6>{0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3}[pick(6)];
            b.push_back(op);
            modrm(int(pick(8)), op & 1);
            if (op <= 0xc1)
                imm(1);
            break;
        }
        case 13: // SETcc, MOVZX/MOVSX, IMUL
            switch (pick(3)) {
            case 0: b.insert(b.end(), {0x0f, uint8_t(0x90 | pick(16))}); modrm(int(pick(8)), false); break;
            case 1: {
                const uint8_t op = std::array<uint8_t, 4>{0xb6, 0xb7, 0xbe, 0xbf}[pick(4)];
                b.insert(b.end(), {0x0f, op});
                modrm(reg32(), op & 1);
                break;
            }
            default: b.insert(b.end(), {0x0f, 0xaf}); modrm(reg32(), true); break;
            }
            break;
        case 14: { // IMUL r, r/m, imm
            const uint8_t op = pick(2) ? 0x69 : 0x6b;
            b.push_back(op);
            modrm(reg32(), true);
            imm(op == 0x69 ? wordSize : 1);
            break;
        }
        case 15: { // TEST, NOT, NEG, MUL, IMUL r/m (all but TEST run from their bytes)
            const uint8_t op = uint8_t(0xf6 | pick(2));
            const int r = int(pick(6));
            b.push_back(op);
            modrm(r, op & 1);
            if (r <= 1)
                imm((op & 1) ? wordSize : 1);
            break;
        }
        case 16: // INC, DEC r/m
            b.push_back(uint8_t(0xfe | pick(2)));
            modrm(int(pick(2)), b.back() & 1);
            break;
        case 17: { // MOV with an absolute address
            const uint32_t d = kData + pick(0x78);
            b.push_back(uint8_t(0xa0 | pick(4)));
            b.insert(b.end(), {uint8_t(d), uint8_t(d >> 8), uint8_t(d >> 16), 0});
            break;
        }
        case 18: b.push_back(pick(2) ? 0x90 : 0x99); break;
        case 19:
        case 20: { // x87 on memory
            // (FISTP, FLDCW and FNSTCW run from their bytes.)
            static const uint8_t kOps[][2] = {{0xd8, 8}, {0xdc, 8}, {0xd9, 0}, {0xd9, 2}, {0xd9, 3}, {0xdd, 0},
                                              {0xdd, 2}, {0xdd, 3}, {0xdb, 0}, {0xdb, 3}, {0xd9, 5}, {0xd9, 7}};
            const auto& o = kOps[pick(12)];
            b.push_back(o[0]);
            modrm(o[1] == 8 ? int(pick(8)) : o[1], false, true);
            break;
        }
        case 21: { // x87 on registers
            static const uint8_t kOps[][2] = {{0xd8, 0xc0}, {0xd8, 0xc8}, {0xd8, 0xe0}, {0xd8, 0xf0}, {0xd8, 0xd0},
                                              {0xdc, 0xc0}, {0xdc, 0xe8}, {0xde, 0xc0}, {0xde, 0xf8}, {0xd9, 0xc0},
                                              {0xd9, 0xc8}, {0xdd, 0xd0}, {0xdd, 0xd8}, {0xdc, 0xd0}};
            const auto& o = kOps[pick(14)];
            b.insert(b.end(), {o[0], uint8_t(o[1] | pick(8))});
            break;
        }
        case 22: { // x87 without operands
            // (FCOMPP and FSQRT run from their bytes.)
            static const uint8_t kOps[][2] = {{0xd9, 0xe0}, {0xd9, 0xe1}, {0xd9, 0xe8}, {0xd9, 0xee},
                                              {0xdf, 0xe0}, {0xde, 0xd9}, {0xd9, 0xfa}};
            const auto& o = kOps[pick(7)];
            b.insert(b.end(), {o[0], o[1]});
            break;
        }
        default: break; // a jump, below
        }
        if (b.empty())
            return false;
        // The operand-size prefix, on what it means something for.
        if (o16) {
            const size_t at = b[0] == 0x64 ? 1 : 0;
            const uint8_t op = b[at];
            if (!(op <= 0x4f || (op >= 0x80 && op <= 0x8b) || op == 0xa1 || op == 0xa3 || (op >= 0xb8 && op <= 0xbf) ||
                  op == 0xc1 || op == 0xc7 || op == 0xd1 || op == 0xd3 || op == 0xf7 || op == 0xff || op == 0x69 ||
                  op == 0x6b || (op == 0x0f && (b[at + 1] == 0xaf || b[at + 1] == 0xb6))))
                return true; // left out: its immediate was made for 16 bits
            b.insert(b.begin(), 0x66);
        }
        out.insert(out.end(), b.begin(), b.end());
        return true;
    };
    for (int run = 0; run < 3000; ++run) {
        std::vector<uint8_t> code;
        for (int n = 0; n < 40; ++n) {
            std::vector<uint8_t> one;
            if (!instruction(one)) {
                // A jump (Jcc or JMP, short or near) over the next instruction.
                std::vector<uint8_t> next;
                while (next.empty())
                    instruction(next);
                const uint8_t length = uint8_t(next.size());
                switch (pick(4)) {
                case 0: one = {uint8_t(0x70 | pick(16)), length}; break;
                case 1: one = {0x0f, uint8_t(0x80 | pick(16)), length, 0, 0, 0}; break;
                case 2: one = {0xeb, length}; break;
                default: one = {0xe9, length, 0, 0, 0}; break;
                }
                one.insert(one.end(), next.begin(), next.end());
            }
            code.insert(code.end(), one.begin(), one.end());
        }
        code.push_back(0xc3);
        std::vector<uint8_t> data(0x200);
        for (uint8_t& d : data)
            d = uint8_t(random());
        std::array<uint32_t, 8> regs;
        for (uint32_t& r : regs)
            r = uint32_t(random());
        const uint32_t flags = 0x202 | (uint32_t(random()) & 0x8d5);
        std::array<double, 8> fpu;
        for (double& f : fpu)
            f = double(int32_t(random())) / 1024.0;
        std::array<uint32_t, 8> after[2];
        uint32_t flagsAfter[2], result[2];
        std::array<double, 8> fpuAfter[2];
        std::vector<uint8_t> dataAfter[2];
        bool ok[2];
        static x86::Cpu cpus[2] = {x86::Cpu(1u << 20), x86::Cpu(1u << 20)};
        for (int k = 0; k < 2; ++k) {
            x86::Cpu& cpu = cpus[k];
            cpu.cacheOn = k == 0;
            cpu.setInstructionLimit(10000);
            cpu.reg = regs;
            cpu.reg[x86::Cpu::EBP] = kData;
            cpu.setStack(0x80000);
            cpu.eflags = flags;
            cpu.fsBase = kFs;
            cpu.fpu.regs = fpu;
            cpu.fpu.top = 0;
            cpu.fpu.tags = 0;
            cpu.fpu.status = 0;
            std::copy(data.begin(), data.end(), cpu.memory().begin() + kData);
            std::copy(data.begin(), data.end(), cpu.memory().begin() + kData + kFs);
            ok[k] = !runX86Keeping(cpu, code, result[k]);
            after[k] = cpu.reg;
            flagsAfter[k] = cpu.eflags;
            fpuAfter[k] = cpu.fpu.regs;
            dataAfter[k].assign(cpu.memory().begin() + kData, cpu.memory().begin() + kData + 0x200 + kFs);
        }
        const bool same = ok[0] == ok[1] && after[0] == after[1] && flagsAfter[0] == flagsAfter[1] &&
                          std::memcmp(fpuAfter[0].data(), fpuAfter[1].data(), sizeof fpuAfter[0]) == 0 &&
                          dataAfter[0] == dataAfter[1] && cpus[0].fpu.top == cpus[1].fpu.top &&
                          cpus[0].fpu.status == cpus[1].fpu.status;
        if (!same) {
            std::fprintf(stderr, "x86 cache disagrees on run %d:", run);
            for (uint8_t c : code)
                std::fprintf(stderr, " %02x", c);
            std::fprintf(stderr, "\n");
            ++test::failures();
            break;
        }
    }
}

static void testX86Dll(bool cached) {
    x86::Cpu cpu(64u << 20);
    cpu.cacheOn = cached;
    x86::Win32Process process(cpu);
    const x86::Win32Process::Module* m = nullptr;
    const std::vector<uint8_t> file(std::begin(kX86TestDll), std::end(kX86TestDll));
    CHECK(!process.load("testdll.dll", file, m));
    if (!m)
        return;
    CHECK(process.missingImports().empty());
    CHECK(!process.attach(*m));
    uint32_t r = 0;
    CHECK(!cpu.call(m->exportAddress("testdll_attached"), {}, r) && r == 0x0a280105);
    unsigned heapSum = 0;
    for (unsigned i = 0; i < 100; ++i)
        heapSum += uint8_t(i * 7);
    CHECK(!cpu.call(m->exportAddress("testdll_heap"), {100}, r) && r == heapSum);
    const uint32_t run = m->exportAddress("testdll_run");
    CHECK(run != 0);
    const int inputs = int(sizeof kX86TestInputs / sizeof kX86TestInputs[0]);
    for (int t = 0; t < int(sizeof kX86TestExpected / sizeof kX86TestExpected[0]); ++t)
        for (int i = 0; i < inputs; ++i) {
            const Error e = cpu.call(run, {uint32_t(t), kX86TestInputs[i][0], kX86TestInputs[i][1]}, r);
            if (e || r != kX86TestExpected[t][i]) {
                std::fprintf(stderr, "x86 test %d, input %d: %s (0x%08x, expected 0x%08x)\n", t, i,
                             e ? e.message.c_str() : "wrong result", r, kX86TestExpected[t][i]);
                ++test::failures();
            }
        }
    // Loaded again it goes elsewhere, relocated: its table of function
    // pointers (test 13) must still work.
    const x86::Win32Process::Module* again = nullptr;
    CHECK(!process.load("testdll2.dll", file, again));
    if (again) {
        CHECK(again->base != m->base);
        for (int i = 0; i < inputs; ++i)
            CHECK(!cpu.call(again->exportAddress("testdll_run"), {13, kX86TestInputs[i][0], kX86TestInputs[i][1]}, r) &&
                  r == kX86TestExpected[13][i]);
    }
}

// The program's threads, run in turns: one counts and sleeps 50 ms at a
// time, getting its turn when the main thread sleeps and it's due; one
// returns, which ends it.

static const test::Suite suite("common.unit.jit", {
    {"instructions, cached and native", [](const std::filesystem::path&) { testX86Instructions(true); }},
    {"instructions, interpreted", [](const std::filesystem::path&) { testX86Instructions(false); }},
    {"test dll, cached and native", [](const std::filesystem::path&) { testX86Dll(true); }},
    {"test dll, interpreted", [](const std::filesystem::path&) { testX86Dll(false); }},
    {"cache and interpreter agree", [](const std::filesystem::path&) { testX86CacheAgreement(); }},
});
