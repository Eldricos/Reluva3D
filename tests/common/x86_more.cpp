// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The x86 processor's additions for whole programs of the late 1990s:
// 64-bit integers through the x87 unit kept exact, segment registers in a
// flat model, code written over, reads of the first page, and pokes
// (memory written at a moment of a run).
//
//   reluva-tests common.unit.x86_more

#include "emulation/x86/cpu.h"
#include "emulation/x86/pokes.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

uint32_t runX86(x86::Cpu &cpu, std::vector<uint8_t> code, bool *ok = nullptr) {
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

void testExact(x86::Cpu &cpu) {
    // FILD/FISTP of 64-bit integers copy them exactly (a memcpy trick):
    // fild qword [0x3000]; fild qword [0x3010]; fxch st1; fistp qword [0x3018];
    // fistp qword [0x3020]; ret. Both values need more than a double's 53 bits.
    cpu.write32(0x3000, 0x6009fb10);
    cpu.write32(0x3004, 0x4853e428);
    cpu.write32(0x3010, 0x89abcdef);
    cpu.write32(0x3014, 0xf1234567);
    runX86(cpu, {0xdf, 0x2d, 0,    0x30, 0, 0, 0xdf, 0x2d, 0x10, 0x30, 0, 0, 0xd9, 0xc9,
                 0xdf, 0x3d, 0x18, 0x30, 0, 0, 0xdf, 0x3d, 0x20, 0x30, 0, 0, 0xc3});
    CHECK(cpu.read32(0x3018) == 0x6009fb10 && cpu.read32(0x301c) == 0x4853e428);
    CHECK(cpu.read32(0x3020) == 0x89abcdef && cpu.read32(0x3024) == 0xf1234567);
}

void testWrittenOver(x86::Cpu &cpu) {
    // Code written over runs as written: mov eax, [0x3000]; ret, then its
    // displacement changed to 0x3004 from outside; then by the program
    // itself, with a jump between, as the Pentium's manual asks of code
    // that changes the instructions just ahead: mov byte [0x200b], n;
    // jmp $+2; mov eax, [0x3000]; ret.
    cpu.write32(0x3000, 111);
    cpu.write32(0x3004, 222);
    cpu.write32(0x3008, 333);
    CHECK(runX86(cpu, {0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 111);
    CHECK(runX86(cpu, {0x8b, 0x05, 0x04, 0x30, 0, 0, 0xc3}) == 222);
    CHECK(runX86(cpu, {0xc6, 0x05, 0x0b, 0x20, 0, 0, 0x00, 0xeb, 0x00, 0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 111);
    CHECK(runX86(cpu, {0xc6, 0x05, 0x0b, 0x20, 0, 0, 0x08, 0xeb, 0x00, 0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 333);
}

void testSegments(x86::Cpu &cpu) {
    // Segment registers in a flat model: push es; pop es; mov eax, 0; mov ax, ds; ret
    CHECK(runX86(cpu, {0x06, 0x07, 0xb8, 0, 0, 0, 0, 0x66, 0x8c, 0xd8, 0xc3}) == 0x23);
}

void testFirstPage(x86::Cpu &cpu) {
    // mov eax, [0x10]; ret: a fault, then 0 with the first page readable,
    // run enough times for the block's native code to be made; a write
    // there still faults.
    bool ok = true;
    runX86(cpu, {0xa1, 0x10, 0, 0, 0, 0xc3}, &ok);
    CHECK(!ok);
    cpu.readableFirstPage = true;
    for (int i = 0; i < 4; ++i) {
        CHECK(runX86(cpu, {0x8b, 0x05, 0x10, 0, 0, 0, 0x40, 0xc3}, &ok) == 1);
        CHECK(ok);
    }
    runX86(cpu, {0xc7, 0x05, 0x10, 0, 0, 0, 1, 0, 0, 0, 0xc3}, &ok); // mov dword [0x10], 1
    CHECK(!ok);
    cpu.readableFirstPage = false;
}

void runAll() {
    x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    testExact(cpu);
    testWrittenOver(cpu);
    testSegments(cpu);
    testFirstPage(cpu);
}

void testPokes() {
    x86::Poke p;
    std::string error;
    CHECK(x86::parsePoke("ms=12000:0x6fd3b8:3:1", false, p, error) && p.ms == 12000 && !p.atAddress &&
          p.address == 0x6fd3b8 && p.bytes == std::vector<uint8_t>{3});
    CHECK(x86::parsePoke("at=0x4c62fa#3:0x100:0x01020304", true, p, error) && p.atAddress == 0x4c62fa && p.nth == 3 &&
          p.every && p.bytes == (std::vector<uint8_t>{4, 3, 2, 1}));
    CHECK(x86::parsePoke("ms=0:0x100:-1:2", false, p, error) && p.bytes == (std::vector<uint8_t>{0xff, 0xff}));
    CHECK(x86::parsePoke("at=0x10:0x100:bytes=0aff", false, p, error) && p.bytes == (std::vector<uint8_t>{10, 255}));
    CHECK(!x86::parsePoke("at=0x10:0x100:bytes=0af", false, p, error));
    CHECK(!x86::parsePoke("ms=1:0x100:5:3", false, p, error));
    CHECK(!x86::parsePoke("soon:0x100:5", false, p, error));
    CHECK(!x86::parsePoke("ms=1:0x100", false, p, error));
    CHECK(x86::parseDump("at=0x401000#2:0x6fd000:4096:/a/b.bin", p, error) && p.nth == 2 && p.dumpSize == 4096 &&
          p.dumpFile == "/a/b.bin");
    CHECK(!x86::parseDump("ms=5:0x6fd000:0:b.bin", p, error));

    // Before the second instruction, the value it reads is written.
    x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    x86::Pokes pokes;
    int lines = 0;
    pokes.log = [&](const std::string &) { ++lines; };
    CHECK(x86::parsePoke("at=0x2001:0x3000:42", false, p, error));
    pokes.list.push_back(p);
    CHECK(x86::parsePoke("ms=50:0x3004:7:1", false, p, error));
    pokes.list.push_back(p);
    CHECK(pokes.needsTrace());
    cpu.trace = [&](x86::Cpu &c) { pokes.instruction(c, 0); };
    // nop; mov eax, [0x3000]; ret
    CHECK(runX86(cpu, {0x90, 0xa1, 0x00, 0x30, 0x00, 0x00, 0xc3}) == 42);
    cpu.trace = nullptr;
    pokes.time(cpu, 49);
    CHECK(cpu.read8(0x3004) == 0);
    pokes.time(cpu, 50);
    pokes.time(cpu, 60); // once only
    CHECK(cpu.read8(0x3004) == 7 && lines == 2 && pokes.list[1].applied == 1);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.x86_more",
                                        {
                                            {"instructions", [](const std::filesystem::path &) { runAll(); }},
                                            {"pokes", [](const std::filesystem::path &) { testPokes(); }},
                                        });
