// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What hosting StarFleet2.exe needs of its own, besides the shared stand-ins
// (src/emulation/x86/): its C++ exceptions described
// (docs/engines/sfc/games/sfc2/systems/hosting.md).

#include "engines/sfc/host/game_process.h"

#include "common/fs.h"

#include <cstdio>
#include <string>

namespace reluva::sfc {

using x86::Cpu;

std::string GameProcess::describeException(x86::Cpu &c, uint32_t record) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "exception 0x%08x at 0x%08x", c.read32(record), c.read32(record + 12));
    std::string what = buf;
    // Visual C++'s throw raises 0xe06d7363 with 0x19930520, the object and
    // its ThrowInfo; ThrowInfo's fourth word is its catchable types, whose
    // first names the thrown type (at its type descriptor's +8).
    if (c.read32(record) != 0xe06d7363 || c.read32(record + 16) < 3)
        return what;
    const uint32_t object = c.read32(record + 24);
    const uint32_t throwInfo = c.read32(record + 28);
    const uint32_t types = throwInfo ? c.read32(throwInfo + 12) : 0;
    const uint32_t count = types ? c.read32(types) : 0;
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        const uint32_t type = c.read32(c.read32(types + 4 + 4 * i) + 4);
        what += (i ? ", " : ": a C++ throw of ") + c.string(type + 8, 200);
    }
    // The game's tErrInfo holds its message as text, in fields of their
    // own: every run of text in the object's first 1 KB.
    std::string text;
    for (uint32_t i = 0; i < 1024 && c.valid(object + i, 1); ++i) {
        const uint8_t ch = c.read8(object + i);
        if (ch >= 0x20 && ch < 0x7f)
            text += char(ch);
        else if (!text.empty() && text.back() != '|')
            text += '|';
    }
    if (text.size() > 8)
        what += " text: " + text;
    return what;
}

} // namespace reluva::sfc
