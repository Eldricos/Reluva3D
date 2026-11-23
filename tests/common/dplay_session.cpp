// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay's sessions over UDP (src/emulation/x86/dplay_session.cpp),
// without the emulator: a host and two joiners on a network in memory
// that loses datagrams, on a clock the test moves. Sessions found by
// broadcast, at an address and through a meeting server, joining
// refused, players and groups kept in step, messages long and short
// arriving whole and in order, and endpoints going quietly or not.
//
//   reluva-tests common.unit.dplay_session

#include "common/meeting.h"
#include "emulation/x86/dplay_session.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::x86::dplay;

namespace {

constexpr uint16_t kPort = 47624;
const char *const kApplication = "{12345678-1234-1234-1234-123456789abc}";
const char *const kAnyApplication = "{00000000-0000-0000-0000-000000000000}";

// One datagram in three lost.
bool lossy(const net::Address &, const net::Address &, size_t index) {
    return index % 3 == 1;
}

struct World {
    std::shared_ptr<net::LoopbackNetwork> network = net::LoopbackNetwork::create();
    Transport transport;
    uint64_t now = 1000; // the endpoints' clock, in milliseconds; they're gone first
    std::vector<std::unique_ptr<Endpoint>> endpoints;
    World() {
        auto n = network;
        transport.open = [n](uint16_t port) { return n->open(port); };
        transport.port = kPort;
        transport.clock = [this] { return now; };
        transport.thread = false;
        transport.timeoutMs = 5000;
    }
    Endpoint &add() {
        endpoints.push_back(std::make_unique<Endpoint>(transport));
        return *endpoints.back();
    }
    // Moves the clock on by `ms`, letting every endpoint work every 10 ms.
    void run(uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 10) {
            now += 10;
            for (auto &e : endpoints)
                e->update();
        }
    }
};

Member player(const std::string &name) {
    Member m;
    m.shortName = name;
    m.longName = name + " long";
    m.data = {1, 2, 3};
    return m;
}

std::optional<Received> take(Endpoint &e) {
    return e.receive([](const Received &) { return true; }, false);
}

// Joins the first session found, the clock run until it's settled.
Endpoint::Status joinFirst(World &w, Endpoint &e, const std::string &password = "") {
    e.search(kAnyApplication, {});
    w.run(50);
    const std::vector<SessionInfo> found = e.found();
    if (found.empty())
        return Endpoint::Status::kNoSession;
    Endpoint::Status status = e.join(found[0].guid, password);
    for (int i = 0; i < 100 && status == Endpoint::Status::kConnecting; ++i) {
        w.run(20);
        status = e.join(found[0].guid, password);
    }
    return status;
}

void testFindAndJoin() {
    World w;
    Endpoint &host = w.add();
    Endpoint &guest = w.add();
    SessionInfo info;
    info.application = kApplication;
    info.name = "Loco";
    info.maxPlayers = 2;
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    CHECK(!info.guid.empty() && host.hosting());

    // By broadcast, then another application's search finds nothing.
    guest.search(kAnyApplication, {});
    w.run(50);
    std::vector<SessionInfo> found = guest.found();
    CHECK(found.size() == 1 && found[0].guid == info.guid && found[0].name == "Loco" && found[0].maxPlayers == 2);
    Endpoint &other = w.add();
    other.search("{99999999-1234-1234-1234-123456789abc}", {});
    w.run(50);
    CHECK(other.found().empty());
    // At its address: a port given that it isn't on, and its own tried too.
    other.search(kApplication, {0x7f000001u, 31415});
    w.run(50);
    CHECK(other.found().size() == 1);
    // Forgotten when it stops answering.
    w.run(4000);
    CHECK(other.found().empty());

    uint32_t hostPlayer = 0;
    CHECK(host.create(player("Host"), hostPlayer) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, guest) == Endpoint::Status::kOk);
    // The joiner has the session's players as they were; no messages for them.
    CHECK(guest.members().size() == 1 && guest.members().count(hostPlayer));
    CHECK(guest.members()[hostPlayer].longName == "Host long");
    CHECK(!take(guest));
    CHECK(guest.session() && guest.session()->name == "Loco" && guest.session()->players == 1);
    uint32_t guestPlayer = 0;
    CHECK(guest.create(player("Guest"), guestPlayer) == Endpoint::Status::kOk);
    w.run(50);
    std::optional<Received> r = take(host);
    CHECK(r && r->event && r->event->kind == Event::kCreated && r->event->id == guestPlayer);
    CHECK(r->event->member.shortName == "Guest" && r->event->players == 2 && r->to == hostPlayer);

    // Full now: a third is refused (a search still finds it; leaving out
    // full sessions is the caller's choice).
    Endpoint &third = w.add();
    CHECK(joinFirst(w, third) == Endpoint::Status::kRefused);
}

void testPassword() {
    World w;
    Endpoint &host = w.add();
    Endpoint &guest = w.add();
    SessionInfo info;
    info.application = kApplication;
    info.password = "secret";
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, guest, "wrong") == Endpoint::Status::kRefused);
    CHECK(joinFirst(w, guest, "secret") == Endpoint::Status::kOk);
}

void testMessages() {
    World w;
    Endpoint &host = w.add();
    Endpoint &a = w.add();
    Endpoint &b = w.add();
    SessionInfo info;
    info.application = kApplication;
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    uint32_t h = 0;
    uint32_t pa = 0;
    uint32_t pb = 0;
    CHECK(host.create(player("H"), h) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, a) == Endpoint::Status::kOk);
    CHECK(a.create(player("A"), pa) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, b) == Endpoint::Status::kOk);
    CHECK(b.create(player("B"), pb) == Endpoint::Status::kOk);
    w.run(100);
    CHECK(a.members().size() == 3 && b.members().size() == 3 && host.members().size() == 3);
    while (take(host) || take(a) || take(b)) {}

    // From now on a third of the datagrams are lost.
    w.network->setDropFilter(lossy);
    // A joiner to another joiner, through the host: a hundred in order,
    // and one of 40000 bytes, in pieces, whole.
    for (uint32_t i = 0; i < 100; ++i)
        CHECK(a.send(pa, pb, {uint8_t(i)}, true) == Endpoint::Status::kOk);
    std::vector<uint8_t> big(40000);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = uint8_t(i * 7);
    CHECK(a.send(pa, pb, big, true) == Endpoint::Status::kOk);
    w.run(3000);
    bool inOrder = true;
    for (uint32_t i = 0; i < 100; ++i) {
        std::optional<Received> r = take(b);
        inOrder = inOrder && r && r->from == pa && r->to == pb && r->data == std::vector<uint8_t>{uint8_t(i)};
    }
    CHECK(inOrder);
    std::optional<Received> r = take(b);
    CHECK(r && r->data == big);
    CHECK(!take(b) && !take(host));

    // To everyone: each of the others once, never back to the sender.
    CHECK(b.send(pb, 0, {9}, true) == Endpoint::Status::kOk);
    w.run(1000);
    r = take(host);
    CHECK(r && r->from == pb && r->to == h && !take(host));
    r = take(a);
    CHECK(r && r->from == pb && r->to == pa && !take(a));
    CHECK(!take(b));

    // A group of the host's, and A in it: everyone told; a message to it
    // reaches A alone.
    uint32_t group = 0;
    Member g;
    g.group = true;
    CHECK(host.create(g, group) == Endpoint::Status::kOk);
    CHECK(host.membership(group, pa, true) == Endpoint::Status::kOk);
    w.run(1000);
    r = take(b);
    CHECK(r && r->event && r->event->kind == Event::kCreated && r->event->id == group && r->event->member.group);
    r = take(b);
    CHECK(r && r->event && r->event->kind == Event::kAdded && r->event->id == group && r->event->player == pa);
    CHECK(b.members()[group].members.count(pa) == 1);
    CHECK(b.send(pb, group, {5}, true) == Endpoint::Status::kOk);
    w.run(1000);
    while ((r = take(a)) && r->event) {}
    CHECK(r && r->from == pb && r->to == pa && r->data == std::vector<uint8_t>{5});
    CHECK(!take(host));

    // Not guaranteed: sent once, so it may be lost; on a sound network it comes.
    w.network->setDropFilter(nullptr);
    CHECK(a.send(pa, h, {7}, false) == Endpoint::Status::kOk);
    w.run(100);
    r = take(host);
    CHECK(r && r->from == pa && r->data == std::vector<uint8_t>{7});
}

void testLeaving() {
    World w;
    Endpoint &host = w.add();
    Endpoint &a = w.add();
    Endpoint &b = w.add();
    SessionInfo info;
    info.application = kApplication;
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    uint32_t h = 0;
    uint32_t pa = 0;
    uint32_t pb = 0;
    CHECK(host.create(player("H"), h) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, a) == Endpoint::Status::kOk);
    CHECK(a.create(player("A"), pa) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, b) == Endpoint::Status::kOk);
    CHECK(b.create(player("B"), pb) == Endpoint::Status::kOk);
    w.run(100);
    while (take(host) || take(a) || take(b)) {}

    // A leaves: the others are told its player is gone.
    a.leave();
    w.run(100);
    std::optional<Received> r = take(b);
    CHECK(r && r->event && r->event->kind == Event::kDestroyed && r->event->id == pa);
    r = take(host);
    CHECK(r && r->event && r->event->kind == Event::kDestroyed && r->event->id == pa);
    CHECK(host.members().size() == 2);

    // B goes silent (its datagrams all lost): the host gives up on it after
    // the timeout, and so does B on the host.
    w.network->setDropFilter([](const net::Address &from, const net::Address &to, size_t) {
        return from.port == kPort || to.port == kPort; // the host's port cut off
    });
    w.run(6000);
    r = take(host);
    CHECK(r && r->event && r->event->kind == Event::kDestroyed && r->event->id == pb);
    r = take(b);
    CHECK(r && r->event && r->event->kind == Event::kDestroyed && r->event->id == h);
    r = take(b);
    CHECK(r && r->event && r->event->kind == Event::kSessionLost);
    CHECK(b.send(pb, 0, {1}, true) == Endpoint::Status::kNotInSession);
}

// Routers that pass nothing between the host and the others: only the
// meeting server reaches either.
bool walledOff(const net::Address &from, const net::Address &to, size_t) {
    const bool server = from.port == net::kMeetingPort || to.port == net::kMeetingPort;
    return !server && (from.port == kPort || to.port == kPort);
}

void testMeetingServer() {
    World w;
    w.network->setDropFilter(walledOff);
    net::MeetingServer server(w.network->open(net::kMeetingPort));
    w.transport.meeting = {0x7f000001u, net::kMeetingPort};
    w.transport.broadcast.clear();
    uint16_t mapped = 0;
    w.transport.mapPort = [&](uint16_t port) {
        mapped = port;
        return std::make_shared<int>(0);
    };
    Endpoint &host = w.add();
    Endpoint &guest = w.add();
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 10) {
            w.run(10);
            server.update(w.now);
        }
    };
    SessionInfo info;
    info.application = kApplication;
    info.name = "Far away";
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    CHECK(mapped == kPort);
    uint32_t h = 0;
    CHECK(host.create(player("H"), h) == Endpoint::Status::kOk);
    run(100);
    CHECK(server.sessions().size() == 1);

    // Found through the server's list alone, and joined through it.
    guest.search(kApplication, {});
    run(100);
    std::vector<SessionInfo> found = guest.found();
    CHECK(found.size() == 1 && found[0].name == "Far away" && found[0].players == 1);
    Endpoint::Status status = guest.join(info.guid, "");
    for (int i = 0; i < 200 && status == Endpoint::Status::kConnecting; ++i) {
        run(20);
        status = guest.join(info.guid, "");
    }
    CHECK(status == Endpoint::Status::kOk);
    uint32_t g = 0;
    CHECK(guest.create(player("G"), g) == Endpoint::Status::kOk);
    CHECK(guest.send(g, h, {4, 2}, true) == Endpoint::Status::kOk);
    run(500);
    std::optional<Received> r = take(host);
    CHECK(r && r->event && r->event->kind == Event::kCreated && r->event->id == g);
    r = take(host);
    CHECK(r && r->from == g && r->data == std::vector<uint8_t>({4, 2}));

    // Gone from the list when the host leaves.
    host.leave();
    run(200);
    CHECK(server.sessions().empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.dplay_session",
           {
               {"find-and-join", [](const std::filesystem::path &) { testFindAndJoin(); }},
               {"password", [](const std::filesystem::path &) { testPassword(); }},
               {"messages", [](const std::filesystem::path &) { testMessages(); }},
               {"leaving", [](const std::filesystem::path &) { testLeaving(); }},
               {"meeting-server", [](const std::filesystem::path &) { testMeetingServer(); }},
           });
