// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: turning by time (host/turning.h), on the code
// it patches, built in the test, run by the interpreter.

#include "emulation/x86/cpu.h"
#include "engines/legoisland/host/turning.h"

#include "tests/support/test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace reluva;

namespace {

constexpr uint32_t kBase = 0x400000;  // where the test's "DLL" is
constexpr uint32_t kData = 0x200000;  // the navigation's fields, and the constant
constexpr uint32_t kStack = 0x300000; // its top: a bare processor has none of its own
constexpr double kDegreesToRadians = 0.017453333333333335;

uint32_t bits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// The code the patch looks for, as LEGO1.DLL has it, then a return: the
// turn in radians on the x87 stack.
void writeTurnCode(x86::Cpu &c) {
    const uint8_t code[] = {0x80, 0x7e, 0x54, 0x00, 0x74, 0x08, 0xd9, 0x07, 0xd8, 0x4c, 0x24,
                            0x14, 0xeb, 0x05, 0xd9, 0x46, 0x50, 0xd8, 0x0f, 0xdc, 0x0d};
    const uint32_t at = kBase + legoisland::kTurnCodeOffset;
    for (uint32_t i = 0; i < sizeof code; ++i)
        c.write8(at + i, code[i]);
    c.write32(at + sizeof code, kData + 0x100); // fmul qword [the constant]
    c.write8(at + sizeof code + 4, 0xc3);       // ret
    c.write32(kData + 0x100, uint32_t(std::bit_cast<uint64_t>(kDegreesToRadians)));
    c.write32(kData + 0x104, uint32_t(std::bit_cast<uint64_t>(kDegreesToRadians) >> 32));
}

// The turn the code works out: the navigation's factor (+0x50) times its
// turn speed (EDI), the frame's time where the caller keeps it ([esp+0x14]),
// untimed (+0x54 clear).
double turn(x86::Cpu &c, float factor, float speed, float seconds) {
    c.write8(kData + 0x54, 0);
    c.write32(kData + 0x50, bits(factor));
    c.write32(kData + 0x80, bits(speed));
    c.reg[x86::Cpu::ESP] = kStack;
    c.reg[x86::Cpu::ESI] = kData;
    c.reg[x86::Cpu::EDI] = kData + 0x80;
    c.fpu.top = 0;
    uint32_t result = 0;
    // Four words before the time: [esp+0x14] past the return address.
    CHECK(!c.call(kBase + legoisland::kTurnCodeOffset, {0, 0, 0, 0, bits(seconds)}, result));
    return c.fpuPop();
}

// Unpatched, the turn is the same whatever the frame's time: the
// original's way.
void testUnpatched() {
    x86::Cpu c(16u << 20);
    writeTurnCode(c);
    const double a = turn(c, 2, 4.5f, 1.0f / 20);
    const double b = turn(c, 2, 4.5f, 1.0f / 60);
    CHECK(std::fabs(a - 9 * kDegreesToRadians) < 1e-6 && std::fabs(a - b) < 1e-9);
}

// Patched, it's by time: as unpatched at 20 frames a second, a third of it
// at 60, and a quarter second's for a longer frame.
void testPatched() {
    x86::Cpu c(16u << 20);
    writeTurnCode(c);
    CHECK(legoisland::turnByTime(c, kBase));
    const double at20 = turn(c, 2, 4.5f, 1.0f / 20);
    CHECK(std::fabs(at20 - 9 * kDegreesToRadians) < 1e-6);
    CHECK(std::fabs(turn(c, 2, 4.5f, 1.0f / 60) - at20 / 3) < 1e-6);
    CHECK(std::fabs(turn(c, 2, 4.5f, 2.0f) - 5 * at20) < 1e-6); // 0.25 s, five twentieths
}

// Code not as expected (another release) is left as it is.
void testOtherCode() {
    x86::Cpu c(16u << 20);
    writeTurnCode(c);
    c.write8(kBase + legoisland::kTurnCodeOffset + 15, 0x47); // fld [edi+0x50]
    std::vector<uint8_t> before(32);
    for (uint32_t i = 0; i < before.size(); ++i)
        before[i] = c.read8(kBase + legoisland::kTurnCodeOffset + i);
    CHECK(!legoisland::turnByTime(c, kBase));
    for (uint32_t i = 0; i < before.size(); ++i)
        CHECK(c.read8(kBase + legoisland::kTurnCodeOffset + i) == before[i]);
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.turning",
                                        {
                                            {"unpatched", [](const std::filesystem::path &) { testUnpatched(); }},
                                            {"patched", [](const std::filesystem::path &) { testPatched(); }},
                                            {"other code", [](const std::filesystem::path &) { testOtherCode(); }},
                                        });
