// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO1.DLL's navigation (0x10054de0) moves the player by the time since
// it last did, but, unless it eases its turns in (a flag at +0x54 of it),
// turns it by its turn speed times a factor (+0x1c, +0x50) each time,
// whatever the time: in degrees, made radians just after. There the
// angle, worked out by `fld [esi+0x50]; fmul [edi]` (5 bytes), is made
// by a call to a native instead, times the frame's time (seconds, in the
// caller's [esp+0x14]) and kTurnFramesPerSecond.

#include "engines/legoisland/host/turning.h"

#include <algorithm>
#include <cstring>

namespace reluva::legoisland {

bool turnByTime(x86::Cpu &cpu, uint32_t dllBase) {
    static constexpr uint8_t kExpected[] = {0x80, 0x7e, 0x54, 0x00, // cmp byte [esi+0x54], 0
                                            0x74, 0x08,             // je the untimed turn
                                            0xd9, 0x07,             // fld [edi]
                                            0xd8, 0x4c, 0x24, 0x14, // fmul [esp+0x14]
                                            0xeb, 0x05,             // jmp past it
                                            0xd9, 0x46, 0x50,       // the untimed turn: fld [esi+0x50]
                                            0xd8, 0x0f,             //                   fmul [edi]
                                            0xdc, 0x0d};            // fmul qword [degrees to radians]
    constexpr uint32_t kTurn = kTurnCodeOffset + 14;
    const uint32_t at = dllBase + kTurnCodeOffset;
    if (!cpu.valid(at, sizeof kExpected))
        return false;
    for (uint32_t i = 0; i < sizeof kExpected; ++i)
        if (cpu.read8(at + i) != kExpected[i])
            return false;
    const uint32_t native = cpu.addNative("LEGO1.DLL turn by time", [](x86::Cpu &c) {
        auto f32 = [&c](uint32_t a) {
            const uint32_t v = c.read32(a);
            float f;
            std::memcpy(&f, &v, 4);
            return double(f);
        };
        const uint32_t esp = c.reg[x86::Cpu::ESP] + 4; // the caller's, past the return address
        const double seconds = std::clamp(f32(esp + 0x14), 0.0, kLongestTurnFrame);
        c.fpuPush(f32(c.reg[x86::Cpu::ESI] + 0x50) * f32(c.reg[x86::Cpu::EDI]) * seconds * kTurnFramesPerSecond);
        return 0u;
    });
    if (!native)
        return false;
    cpu.write8(dllBase + kTurn, 0xe8); // CALL rel32
    cpu.write32(dllBase + kTurn + 1, native - (dllBase + kTurn + 5));
    return true;
}

} // namespace reluva::legoisland
