// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The DirectPlay stand-in (src/emulation/x86/dplay.cpp), called as a
// program would: the lobby and DirectPlay through their interfaces, an
// address built and read back, and the answers without a network.
//
//   reluva-tests common.unit.dplay

#include "emulation/x86/com.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

struct Fixture {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    x86::DirectPlay play{process};
    uint32_t mem = 0;
    Fixture() { mem = process.memory().alloc(4096); }
    uint32_t exported(const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn("DPLAYX.dll", name), args, r));
        return r;
    }
    // Method `index` (from 0, IUnknown's included) of the interface `self` points at.
    uint32_t method(uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        uint32_t r = 0;
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    }
    uint32_t guid(uint32_t at, const char *g) {
        x86::ComHost::writeGuid(cpu, at, g);
        return at;
    }
};

// IDirectPlayLobby3A's methods by index.
constexpr int kQueryInterface = 0;
constexpr int kRelease = 2;
constexpr int kCreateAddress = 4;
constexpr int kEnumAddress = 5;
constexpr int kGetConnectionSettings = 8;
// IDirectPlay4A's.
constexpr int kEnumSessions = 13;
constexpr int kInitializeConnection = 38;

void testLobby() {
    Fixture f;
    // DirectPlayLobbyCreateA (ordinal 4), then IDirectPlayLobby3A.
    CHECK(f.exported("#4", {0, f.mem, 0, 0, 0}) == 0);
    const uint32_t lobbyA = f.cpu.read32(f.mem);
    CHECK(f.method(lobbyA, kQueryInterface,
                   {f.guid(f.mem + 16, "{2db72491-652c-11d1-a7a8-0000f803abfc}"), f.mem + 4}) == 0);
    const uint32_t lobby = f.cpu.read32(f.mem + 4);
    CHECK(lobby != 0);
    // Not started by a lobby: DPERR_NOTLOBBIED.
    f.cpu.write32(f.mem + 8, 0);
    CHECK(f.method(lobby, kGetConnectionSettings, {0, 0, f.mem + 8}) == 0x8877042eu);
    // CreateAddress(TCP/IP, DPAID_INet, "host", 5): first the size it needs, then the address.
    const uint32_t sp = f.guid(f.mem + 32, "{36e95ee0-8577-11cf-960c-0080c7534e82}");
    const uint32_t type = f.guid(f.mem + 48, "{c4a54da0-e0af-11cf-9c4e-00a0c905425e}");
    f.cpu.writeString(f.mem + 64, "host", 5);
    f.cpu.write32(f.mem + 8, 0);
    CHECK(f.method(lobby, kCreateAddress, {sp, type, f.mem + 64, 5, 0, f.mem + 8}) == 0x8877001eu);
    const uint32_t size = f.cpu.read32(f.mem + 8);
    CHECK(size == 20 + 4 + 20 + 16 + 20 + 5);
    CHECK(f.method(lobby, kCreateAddress, {sp, type, f.mem + 64, 5, f.mem + 1024, f.mem + 8}) == 0);
    CHECK(x86::ComHost::guid(f.cpu, f.mem + 1024) == "{1318f560-912c-11d0-9daa-00a0c90a43cb}");
    CHECK(f.cpu.read32(f.mem + 1024 + 20) == size);
    // EnumAddress hands each element to the callback: count them.
    int seen = 0;
    std::string lastType;
    f.process.provideStdcall("TEST", "Element", 4, [&](x86::Cpu &c) {
        ++seen;
        lastType = x86::ComHost::guid(c, c.arg(0));
        return 1u;
    });
    CHECK(f.method(lobby, kEnumAddress, {f.process.standIn("TEST", "Element"), f.mem + 1024, size, 0}) == 0);
    CHECK(seen == 3 && lastType == "{c4a54da0-e0af-11cf-9c4e-00a0c905425e}");
    CHECK(f.method(lobby, kRelease, {}) == 1);
}

void testDirectPlay() {
    Fixture f;
    // DirectPlayCreate (ordinal 1) gives IDirectPlay; IDirectPlay4A from it.
    CHECK(f.exported("#1", {f.guid(f.mem + 32, "{36e95ee0-8577-11cf-960c-0080c7534e82}"), f.mem, 0}) == 0);
    const uint32_t first = f.cpu.read32(f.mem);
    CHECK(f.method(first, kQueryInterface, {f.guid(f.mem + 16, "{0ab1c531-4745-11d1-a7a1-0000f803abfc}"), f.mem + 4}) ==
          0);
    const uint32_t play = f.cpu.read32(f.mem + 4);
    // No network: a connection can't be made, and the sessions' search times out at once.
    CHECK(f.method(play, kInitializeConnection, {f.mem + 64, 0}) == 0x887700d2u);
    uint32_t flags = 0;
    f.process.provideStdcall("TEST", "Session", 4, [&](x86::Cpu &c) {
        flags = c.arg(2);
        return 0u;
    });
    CHECK(f.method(play, kEnumSessions, {f.mem + 128, 0, f.process.standIn("TEST", "Session"), 0, 0}) == 0);
    CHECK(flags == 1); // DPESC_TIMEDOUT
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.dplay",
                                        {
                                            {"lobby", [](const std::filesystem::path &) { testLobby(); }},
                                            {"directplay", [](const std::filesystem::path &) { testDirectPlay(); }},
                                        });
