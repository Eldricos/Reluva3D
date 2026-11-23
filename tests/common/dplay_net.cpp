// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for DirectPlay's network between copies of Reluva (dplay_net.cpp),
// on the machine's loopback: a session found and joined, players and
// messages passed between three programs through the host, a program
// leaving, and the host going.

#include "backends/networking/sockets/socket_net.h"
#include "emulation/x86/dplay_net.h"
#include "tests/support/test.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {

using namespace reluva;
using x86::dplay::Event;
using x86::dplay::Node;
using x86::dplay::PlayerInfo;
using x86::dplay::SessionInfo;

const char *const kApplication = "{12345678-1234-1234-1234-123456789abc}";

// Polls `node` until an event `want` accepts comes, for up to two seconds;
// the events seen are kept in `seen`. The host (`host`, while there's one)
// is polled too, as it passes on what the others send.
bool waitFor(Node &node, std::vector<Event> &seen, const std::function<bool(const Event &)> &want, Node *host) {
    for (int i = 0; i < 200; ++i) {
        if (host && host != &node)
            host->poll();
        for (Event &e : node.poll()) {
            seen.push_back(e);
            if (want(e))
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void runAll() {
    auto sockets = net::systemSockets();
    // A port of its own, so runs side by side don't meet.
    const uint16_t port = uint16_t(30000 + getpid() % 20000);
    auto hostNode = std::make_unique<Node>(sockets, std::vector<uint32_t>{}, port);
    Node &host = *hostNode;
    SessionInfo session;
    session.instance = "{aaaaaaaa-0000-0000-0000-000000000001}";
    session.application = kApplication;
    session.name = "Duel";
    session.password = "kobayashi";
    session.maxPlayers = 4;
    CHECK(host.host(session));
    Node taken(sockets, {}, port);
    CHECK(!taken.host(session)); // the port's in use
    host.addPlayer({0x100001, 0, "Kirk", "", {1, 2, 3}});

    // Found at the host's address, as asked for another application isn't.
    auto aNode = std::make_unique<Node>(sockets, std::vector<uint32_t>{}, port);
    Node &a = *aNode;
    Node b(sockets, {}, port);
    a.find("127.0.0.1", "{99999999-9999-9999-9999-999999999999}", 300);
    for (int i = 0; i < 20 && a.found().empty(); ++i)
        host.poll();
    CHECK(a.found().empty());
    std::vector<x86::dplay::FoundSession> found;
    for (int i = 0; i < 100 && found.empty(); ++i) {
        a.find("127.0.0.1", kApplication, 0);
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        found = a.found();
    }
    CHECK(found.size() == 1 && found[0].info.name == "Duel" && found[0].info.hasPassword &&
          found[0].info.password.empty() && found[0].info.currentPlayers == 1 && found[0].host.port == port);

    // Joined, the host polled from another thread meanwhile; a wrong
    // password refused (DPERR_ACCESSDENIED).
    auto join = [&](Node &n, const std::string &password, SessionInfo &got, std::vector<PlayerInfo> &players,
                    uint32_t &firstId) {
        bool done = false;
        std::thread hosting([&] {
            while (!done) {
                host.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        const uint32_t r = n.join(session.instance, password, 2000, got, players, firstId);
        done = true;
        hosting.join();
        return r;
    };
    SessionInfo got;
    std::vector<PlayerInfo> players;
    uint32_t firstA = 0;
    CHECK(join(a, "wrong", got, players, firstA) == 0x8877000au);
    CHECK(join(a, "kobayashi", got, players, firstA) == 0);
    CHECK(got.name == "Duel" && got.maxPlayers == 4 && players.size() == 1 && players[0].shortName == "Kirk" &&
          players[0].data.size() == 3 && firstA > 0x100001);
    for (int i = 0; i < 100 && b.found().empty(); ++i) {
        b.find("", kApplication, 0); // the local network, and this machine
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    uint32_t firstB = 0;
    CHECK(join(b, "kobayashi", got, players, firstB) == 0 && firstB != firstA);

    // A's player: the host and B told; its data changed, likewise.
    a.addPlayer({firstA, 0, "Spock", "", {}});
    std::vector<Event> seen;
    auto wait = [&](Node &node, const std::function<bool(const Event &)> &want) {
        return waitFor(node, seen, want, hostNode.get());
    };
    CHECK(wait(host, [&](const Event &e) {
        return e.kind == Event::kPlayerAdded && e.player.id == firstA && e.player.shortName == "Spock";
    }));
    CHECK(wait(b, [&](const Event &e) { return e.kind == Event::kPlayerAdded && e.player.id == firstA; }));
    b.addPlayer({firstB, 0, "McCoy", "", {}});
    CHECK(wait(host, [&](const Event &e) { return e.kind == Event::kPlayerAdded && e.player.id == firstB; }));
    CHECK(wait(a, [&](const Event &e) { return e.kind == Event::kPlayerAdded && e.player.id == firstB; }));
    a.setPlayerData(firstA, {9});
    CHECK(wait(b, [&](const Event &e) {
        return e.kind == Event::kPlayerData && e.player.id == firstA && e.player.data == std::vector<uint8_t>{9};
    }));

    // Messages: to one player (through the host), and to every one.
    a.send(firstA, firstB, {7, 7});
    for (int i = 0; i < 20; ++i) { // the host passes it on as it polls
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(wait(b, [&](const Event &e) {
        return e.kind == Event::kMessage && e.from == firstA && e.to == firstB && e.bytes.size() == 2;
    }));
    host.send(0x100001, 0, {1});
    CHECK(wait(a, [&](const Event &e) { return e.kind == Event::kMessage && e.from == 0x100001; }));
    CHECK(wait(b, [&](const Event &e) { return e.kind == Event::kMessage && e.from == 0x100001; }));
    b.send(firstB, 0x100001, {2});
    CHECK(
        wait(host, [&](const Event &e) { return e.kind == Event::kMessage && e.from == firstB && e.to == 0x100001; }));

    // The session's description changed by the host.
    session.user[0] = 42;
    host.setSession(session);
    CHECK(wait(a, [](const Event &e) { return e.kind == Event::kSession && e.session.user[0] == 42; }));

    // A program gone: its player is, for the others.
    aNode.reset();
    CHECK(wait(host, [&](const Event &e) { return e.kind == Event::kPlayerRemoved && e.player.id == firstA; }));
    CHECK(wait(b, [&](const Event &e) { return e.kind == Event::kPlayerRemoved && e.player.id == firstA; }));

    // The host gone: the session's lost.
    hostNode.reset();
    CHECK(wait(b, [](const Event &e) { return e.kind == Event::kSessionLost; }));
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.dplay_net",
                                        {
                                            {"all", [](const std::filesystem::path &) { runAll(); }},
                                        });
