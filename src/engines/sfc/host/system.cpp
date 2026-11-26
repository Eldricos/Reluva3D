// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What hosting StarFleet2.exe needs of its own, besides the shared stand-ins
// (src/emulation/x86/): its C++ exceptions described, and Winsock's two
// functions a multiplayer game over TCP/IP calls
// (docs/engines/sfc/games/sfc2/systems/hosting.md).

#include "engines/sfc/host/game_process.h"

#include "common/fs.h"

#include <cstdio>
#include <string>

namespace reluva::sfc {

using x86::Cpu;

void GameProcess::stdcall(const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn) {
    const uint32_t pop = uint32_t(args) * 4;
    _process->provide(dll, name, [fn = std::move(fn), pop](Cpu &c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
}

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

void GameProcess::addSystem() {
    // Winsock's gethostname (#57, as WSOCK32 is imported by ordinal) and
    // gethostbyname (#52) of that name: hosting a multiplayer game, the game
    // names its player by the machine (to its first dot) and keeps its
    // address. The name is GetComputerNameA's; the address the machine
    // itself (127.0.0.1) until the rest of Winsock the game imports is
    // stood in for.
    static const std::string kHostName = "RELUVA";
    stdcall("WSOCK32.dll", "#57", 2, [](Cpu &c) {
        if (c.arg(1) < kHostName.size() + 1 || !c.valid(c.arg(0), uint32_t(kHostName.size() + 1)))
            return 0xffffffffu; // SOCKET_ERROR
        c.writeString(c.arg(0), kHostName, uint32_t(kHostName.size() + 1));
        return 0u;
    });
    stdcall("WSOCK32.dll", "#52", 1, [this](Cpu &c) {
        if (fs::toLower(c.string(c.arg(0), 256)) != fs::toLower(kHostName))
            return 0u; // not found
        // A hostent (name, aliases, AF_INET and 4, addresses), made once.
        if (!_hostent) {
            const uint32_t h = _process->memory().alloc(64);
            c.writeString(h + 32, kHostName, 16);
            c.write32(h, h + 32);
            c.write32(h + 4, h + 20); // no aliases: a list holding only its end
            c.write16(h + 8, 2);
            c.write16(h + 10, 4);
            c.write32(h + 12, h + 24);
            c.write32(h + 20, 0);
            c.write32(h + 24, h + 48);
            c.write32(h + 28, 0);
            c.write32(h + 48, 0x0100007fu); // 127.0.0.1, in network order
            _hostent = h;
        }
        return _hostent;
    });
}

} // namespace reluva::sfc
