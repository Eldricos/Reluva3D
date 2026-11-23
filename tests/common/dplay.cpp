// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The DirectPlay stand-in (src/emulation/x86/dplay.cpp), called as a
// program would: the lobby and DirectPlay through their interfaces, an
// address built and read back, the answers without a network, and two
// objects meeting on one network.
//
//   reluva-tests common.unit.dplay

#include "emulation/x86/com.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
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
constexpr int kAddPlayerToGroup = 3;
constexpr int kClose = 4;
constexpr int kCreateGroup = 5;
constexpr int kCreatePlayer = 6;
constexpr int kEnumGroupPlayers = 10;
constexpr int kEnumSessions = 13;
constexpr int kGetCaps = 14;
constexpr int kGetMessageCount = 17;
constexpr int kOpen = 24;
constexpr int kReceive = 25;
constexpr int kSend = 26;
constexpr int kInitializeConnection = 38;
constexpr int kSendEx = 49;

constexpr uint32_t kNoMessages = 0x887700beu;
constexpr uint32_t kBufferTooSmall = 0x8877001eu;
constexpr const char *kTcpIp = "{36e95ee0-8577-11cf-960c-0080c7534e82}";
constexpr const char *kApplication = "{12345678-1234-1234-1234-123456789abc}";

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

// A DirectPlay object on `network`, through TCP/IP, as the game makes it.
struct Peer : Fixture {
    uint32_t dp = 0; // its IDirectPlay4A
    explicit Peer(const std::shared_ptr<x86::DirectPlay::Network> &network) {
        play.network = network;
        CHECK(exported("#1", {guid(mem + 32, kTcpIp), mem, 0}) == 0);
        const uint32_t first = cpu.read32(mem);
        CHECK(method(first, kQueryInterface, {guid(mem + 16, "{0ab1c531-4745-11d1-a7a1-0000f803abfc}"), mem + 4}) == 0);
        dp = cpu.read32(mem + 4);
        // An address of the total size, then the service provider.
        const uint32_t address = mem + 256;
        guid(address, "{1318f560-912c-11d0-9daa-00a0c90a43cb}");
        cpu.write32(address + 16, 4);
        cpu.write32(address + 20, 60);
        guid(address + 24, "{07d916c0-e0af-11cf-9c4e-00a0c905425e}");
        cpu.write32(address + 40, 16);
        guid(address + 44, kTcpIp);
        CHECK(method(dp, kInitializeConnection, {address, 0}) == 0);
    }
    // A DPSESSIONDESC2 at mem + 512, its name at mem + 640.
    uint32_t desc(const char *name, uint32_t maxPlayers) {
        const uint32_t d = mem + 512;
        for (uint32_t i = 0; i < 80; i += 4)
            cpu.write32(d + i, 0);
        cpu.write32(d, 80);
        guid(d + 24, kApplication);
        cpu.write32(d + 40, maxPlayers);
        cpu.writeString(mem + 640, name, 32);
        cpu.write32(d + 48, mem + 640);
        return d;
    }
    // A player named `name` (DPNAME at mem + 704); its id.
    uint32_t player(const char *name) {
        const uint32_t dpname = mem + 704;
        cpu.write32(dpname, 16);
        cpu.write32(dpname + 4, 0);
        cpu.write32(dpname + 8, 0);
        cpu.writeString(dpname + 16, name, 32);
        cpu.write32(dpname + 12, dpname + 16);
        CHECK(method(dp, kCreatePlayer, {mem + 760, dpname, 0, 0, 0, 0}) == 0);
        return cpu.read32(mem + 760);
    }
    // Receives into mem + 2048, waiting up to `waitMs` for a message to
    // come over the network: the result, and from whom.
    uint32_t receive(uint32_t &from, uint32_t room = 1024, int waitMs = 2000) {
        uint32_t r = kNoMessages;
        for (int waited = 0; r == kNoMessages && waited <= waitMs; waited += 5) {
            cpu.write32(mem + 768, 0);
            cpu.write32(mem + 772, 0);
            cpu.write32(mem + 776, room);
            r = method(dp, kReceive, {mem + 768, mem + 772, 1, mem + 2048, mem + 776});
            if (r == kNoMessages)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        from = cpu.read32(mem + 768);
        return r;
    }
    uint32_t nothing() {
        uint32_t from = 0;
        return receive(from, 1024, 100);
    }
    uint32_t received(uint32_t offset) { return cpu.read32(mem + 2048 + offset); }
};

void testNetwork() {
    auto network = x86::DirectPlay::Network::inMemory();
    Peer host(network);
    Peer guest(network);

    // The host's session, made; its GUID written back into the desc.
    CHECK(host.method(host.dp, kOpen, {host.desc("Loco", 4), 2}) == 0);
    const std::string session = x86::ComHost::guid(host.cpu, host.mem + 512 + 8);
    const uint32_t hostPlayer = host.player("Host");
    CHECK(host.method(host.dp, kGetCaps, {host.mem + 800, 0}) == 0);
    CHECK(host.cpu.read32(host.mem + 804) == 2); // DPCAPS_ISHOST

    // The guest finds it, then hears the search has timed out.
    std::string found;
    std::string name;
    int timedOut = 0;
    guest.process.provideStdcall("TEST", "Session", 4, [&](x86::Cpu &c) {
        if (c.arg(2) & 1) {
            ++timedOut;
            return 0u;
        }
        found = x86::ComHost::guid(c, c.arg(0) + 8);
        name = c.string(c.read32(c.arg(0) + 48));
        CHECK(c.read32(c.arg(0) + 44) == 1); // its players
        return 1u;
    });
    const uint32_t any = guest.desc("", 0);
    guest.guid(any + 24, "{00000000-0000-0000-0000-000000000000}");
    CHECK(guest.method(guest.dp, kEnumSessions, {any, 0, guest.process.standIn("TEST", "Session"), 0, 1}) == 0);
    CHECK(found == session && name == "Loco" && timedOut == 1);

    // Joined, with a player: the host is told (DPSYS_CREATEPLAYERORGROUP).
    guest.guid(any + 8, found.c_str());
    CHECK(guest.method(guest.dp, kOpen, {any, 1}) == 0);
    const uint32_t guestPlayer = guest.player("Guest");
    CHECK(guestPlayer != hostPlayer);
    uint32_t from = 1;
    CHECK(host.receive(from) == 0);
    CHECK(from == 0 && host.received(0) == 3 && host.received(8) == guestPlayer);
    CHECK(host.cpu.string(host.received(36)) == "Guest"); // its long name, made the host's pointer
    CHECK(host.nothing() == kNoMessages);

    // A message to everyone reaches the host alone; too little room says how much.
    guest.cpu.writeString(guest.mem + 1024, "hello", 6);
    CHECK(guest.method(guest.dp, kSend, {guestPlayer, 0, 1, guest.mem + 1024, 6}) == 0);
    CHECK(guest.nothing() == kNoMessages);
    CHECK(host.method(host.dp, kGetMessageCount, {hostPlayer, host.mem + 800}) == 0);
    CHECK(host.cpu.read32(host.mem + 800) == 1);
    CHECK(host.receive(from, 2) == kBufferTooSmall);
    CHECK(host.cpu.read32(host.mem + 776) == 6);
    CHECK(host.receive(from) == 0);
    CHECK(from == guestPlayer && host.cpu.string(host.mem + 2048) == "hello");

    // A group: the guest told it's made and of its member; it sees the member.
    host.cpu.write32(host.mem + 704, 16);
    host.cpu.write32(host.mem + 712, 0);
    host.cpu.write32(host.mem + 716, 0);
    CHECK(host.method(host.dp, kCreateGroup, {host.mem + 800, host.mem + 704, 0, 0, 0}) == 0);
    const uint32_t group = host.cpu.read32(host.mem + 800);
    CHECK(host.method(host.dp, kAddPlayerToGroup, {group, guestPlayer}) == 0);
    CHECK(guest.receive(from) == 0 && guest.received(0) == 3 && guest.received(4) == 0);
    CHECK(guest.receive(from) == 0 && guest.received(0) == 7 && guest.received(8) == guestPlayer);
    uint32_t members = 0;
    guest.process.provideStdcall("TEST", "Player", 5, [&](x86::Cpu &) {
        ++members;
        return 1u;
    });
    CHECK(guest.method(guest.dp, kEnumGroupPlayers, {group, 0, guest.process.standIn("TEST", "Player"), 0, 0}) == 0);
    CHECK(members == 1);

    // Sent asynchronously: pending, then the sender is told it's done
    // (DPSYS_SENDCOMPLETE, 0x10d) with the id it was given.
    CHECK(host.method(host.dp, kSendEx, {hostPlayer, group, 0x201, host.mem + 1024, 1, 0, 0, 7, host.mem + 808}) ==
          0x8000000au);
    CHECK(host.receive(from) == 0 && from == 0 && host.received(0) == 0x10d);
    CHECK(host.received(28) == host.cpu.read32(host.mem + 808) && host.received(24) == 7);
    CHECK(guest.receive(from) == 0 && from == hostPlayer);

    // The host gone: the guest hears its player and group are gone, and
    // the session lost.
    CHECK(host.method(host.dp, kClose, {}) == 0);
    CHECK(guest.receive(from) == 0 && guest.received(0) == 5 && guest.received(8) == hostPlayer);
    CHECK(guest.receive(from) == 0 && guest.received(0) == 5 && guest.received(8) == group);
    CHECK(guest.receive(from) == 0 && guest.received(0) == 0x31);
    CHECK(guest.method(guest.dp, kClose, {}) == 0);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.dplay",
                                        {
                                            {"lobby", [](const std::filesystem::path &) { testLobby(); }},
                                            {"directplay", [](const std::filesystem::path &) { testDirectPlay(); }},
                                            {"network", [](const std::filesystem::path &) { testNetwork(); }},
                                        });
