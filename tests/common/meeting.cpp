// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The meeting server for network play: a host's session listed, a player
// introduced to it, their datagrams going straight between them once
// they've reached each other, or through the server when their routers
// won't let them; a game's other datagrams untouched.
//
//   reluva-tests common.unit.meeting

#include "common/meeting.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::net;

namespace {

constexpr uint16_t kHostPort = 40001;
constexpr uint16_t kJoinPort = 40002;
constexpr uint16_t kOtherPort = 40003;

// Two players' routers that pass nothing between them: only the server
// reaches either.
bool walledOff(const Address &from, const Address &to, size_t) {
    const bool host = from.port == kHostPort || to.port == kHostPort;
    const bool joiner = from.port == kJoinPort || to.port == kJoinPort;
    return host && joiner;
}

std::vector<uint8_t> bytes(const std::string &s) {
    return {s.begin(), s.end()};
}

// A host, a player and the server on one loopback network, driven a tenth
// of a second at a time.
struct Meeting {
    std::shared_ptr<LoopbackNetwork> network = LoopbackNetwork::create();
    uint64_t t = 1000;
    std::unique_ptr<MeetingServer> server;
    std::unique_ptr<MeetingSocket> host;
    std::unique_ptr<MeetingSocket> joiner;
    const Address serverAt{0x7f000001, kMeetingPort};
    // What each side's game was given, and from where.
    std::vector<std::pair<Address, std::vector<uint8_t>>> atHost;
    std::vector<std::pair<Address, std::vector<uint8_t>>> atJoiner;

    Meeting() {
        server = std::make_unique<MeetingServer>(network->open(kMeetingPort));
        host = std::make_unique<MeetingSocket>(network->open(kHostPort), serverAt, "age");
        joiner = std::make_unique<MeetingSocket>(network->open(kJoinPort), serverAt, "age");
        host->setClock([this] { return t; });
        joiner->setClock([this] { return t; });
    }
    void run(int ms) {
        for (int i = 0; i < ms / 100; ++i) {
            t += 100;
            server->update(t);
            Address from;
            std::vector<uint8_t> data;
            while (host->receive(from, data))
                atHost.push_back({from, data});
            while (joiner->receive(from, data))
                atJoiner.push_back({from, data});
            server->update(t);
        }
    }
};

void testListAndConnect() {
    Meeting m;
    m.host->publish("Sunday race", bytes("info"));
    m.run(300);
    CHECK(m.host->published() && m.server->sessions().size() == 1);

    // Listed for its game only.
    MeetingSocket other(m.network->open(kOtherPort), m.serverAt, "clue");
    other.setClock([&] { return m.t; });
    other.requestList();
    m.joiner->requestList();
    m.run(300);
    Address from;
    std::vector<uint8_t> data;
    while (other.receive(from, data)) {}
    CHECK(other.listed().empty());
    CHECK(m.joiner->listed().size() == 1 && m.joiner->listed()[0].name == "Sunday race" &&
          m.joiner->listed()[0].info == bytes("info") && m.joiner->heardFromServerMs() > 0);

    // Introduced: the host hears the player from the connection's own
    // address, and its answer comes back from the session's; once they've
    // reached each other, straight between them.
    const Address hostAt = m.joiner->connect(m.joiner->listed()[0].id);
    CHECK(isMeetingAddress(hostAt) && !m.joiner->send(hostAt, bytes("too soon")));
    m.run(1000);
    CHECK(m.joiner->send(hostAt, bytes("hello")));
    m.run(300);
    CHECK(!m.atHost.empty() && m.atHost.back().second == bytes("hello") && isMeetingAddress(m.atHost.back().first));
    CHECK(m.host->send(m.atHost.back().first, bytes("welcome")));
    m.run(300);
    CHECK(!m.atJoiner.empty() && m.atJoiner.back().first == hostAt && m.atJoiner.back().second == bytes("welcome"));
    CHECK(m.joiner->links().at(hostAt).direct && m.host->links().at(m.atHost.back().first).direct);

    // A game's own datagrams to anyone else go as they are.
    CHECK(m.host->send({0x7f000001, kOtherPort}, bytes("plain")));
    CHECK(other.receive(from, data) && data == bytes("plain") && from.port == kHostPort);

    // Taken off the list; and a session whose host goes quiet is forgotten.
    m.host->unpublish();
    m.run(200);
    CHECK(m.server->sessions().empty());
    m.host->publish("Again", {});
    m.run(200);
    CHECK(m.server->sessions().size() == 1);
    for (int i = 0; i < 200; ++i) { // 20 s of the server alone
        m.t += 100;
        m.server->update(m.t);
    }
    CHECK(m.server->sessions().empty() && m.server->connections().size() == 1);
    for (int i = 0; i < 500; ++i) { // and a connection unused for a minute
        m.t += 100;
        m.server->update(m.t);
    }
    CHECK(m.server->connections().empty());
}

void testRelayed() {
    // Routers that let nothing through between the two: the punches are
    // lost, and everything goes through the server, both ways.
    Meeting m;
    m.network->setDropFilter(walledOff);
    m.host->publish("Walled", {});
    m.joiner->requestList();
    m.run(500);
    CHECK(m.joiner->listed().size() == 1);
    const Address hostAt = m.joiner->connect(m.joiner->listed()[0].id);
    m.run(1500);
    for (int i = 0; i < 3; ++i)
        CHECK(m.joiner->send(hostAt, bytes("lap " + std::to_string(i))));
    m.run(300);
    CHECK(m.atHost.size() == 3 && m.atHost[2].second == bytes("lap 2"));
    CHECK(m.host->send(m.atHost[0].first, bytes("ok")));
    m.run(300);
    CHECK(m.atJoiner.size() == 1 && m.atJoiner[0].first == hostAt);
    CHECK(!m.joiner->links().at(hostAt).direct && m.server->connections().begin()->second.relayed == 4);
    // Kept open while used, past what a silent one lasts.
    for (int i = 0; i < 9; ++i) {
        m.run(10000);
        m.host->publish("Walled", {});
    }
    CHECK(m.server->connections().size() == 1 && m.joiner->send(hostAt, bytes("still")));
    m.run(300);
    CHECK(m.atHost.back().second == bytes("still"));

    // A session that isn't there: asked once, then let be.
    const Address nowhere = m.joiner->connect(99999);
    m.run(1500);
    CHECK(!m.joiner->send(nowhere, bytes("anyone?")) && !m.joiner->links().at(nowhere).known);
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.meeting", {
                                      {"list and connect", [](const std::filesystem::path &) { testListAndConnect(); }},
                                      {"relayed", [](const std::filesystem::path &) { testRelayed(); }},
                                  });
