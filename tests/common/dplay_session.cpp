// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay's sessions over UDP (src/emulation/x86/dplay_session.cpp),
// without the emulator: a host and two joiners on a network in memory
// that loses datagrams, on a clock the test moves. Sessions found by
// broadcast, at an address and through a meeting server, joining
// refused, players and groups kept in step, messages long and short
// arriving whole and in order, endpoints going quietly or not, another
// taking over when the host goes; and DirectPlay 4's own messages, to find
// the original program's games and answer its searches.
//
//   reluva-tests common.unit.dplay_session

#include "backends/networking/sockets/socket_net.h"
#include "common/meeting.h"
#include "emulation/x86/dplay4_wire.h"
#include "emulation/x86/dplay_session.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace reluva;
using namespace reluva::x86::dplay;
namespace dp4 = reluva::x86::dp4;

namespace {

constexpr uint16_t kPort = 47624;
constexpr uint16_t kFirstFree = 27960; // the loopback network's first port given out
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

// Every message waiting, the system messages' kinds and the others' bytes.
std::vector<Received> drain(Endpoint &e) {
    std::vector<Received> all;
    while (std::optional<Received> r = take(e))
        all.push_back(*r);
    return all;
}

bool has(const std::vector<Received> &all, Event::Kind kind, uint32_t id = 0) {
    for (const Received &r : all)
        if (r.event && r.event->kind == kind && (!id || r.event->id == id))
            return true;
    return false;
}

bool hasData(const std::vector<Received> &all, uint32_t from, const std::vector<uint8_t> &data) {
    for (const Received &r : all)
        if (!r.event && r.from == from && r.data == data)
            return true;
    return false;
}

// DPSESSION_MIGRATEHOST
constexpr uint32_t kMigrate = 4;

void testNewHost() {
    World w;
    Endpoint &host = w.add();
    Endpoint &a = w.add();
    Endpoint &b = w.add();
    Endpoint &c = w.add();
    SessionInfo info;
    info.application = kApplication;
    info.flags = kMigrate;
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    uint32_t h = 0;
    uint32_t pa = 0;
    uint32_t pb = 0;
    uint32_t pc = 0;
    CHECK(host.create(player("H"), h) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, a) == Endpoint::Status::kOk);
    CHECK(a.create(player("A"), pa) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, b) == Endpoint::Status::kOk);
    CHECK(b.create(player("B"), pb) == Endpoint::Status::kOk);
    CHECK(joinFirst(w, c) == Endpoint::Status::kOk);
    CHECK(c.create(player("C"), pc) == Endpoint::Status::kOk);
    w.run(500);
    for (Endpoint *e : {&host, &a, &b, &c})
        drain(*e);
    w.network->setDropFilter(lossy);

    // The host leaves: A, first in after it, takes over and is told so;
    // the others come back to it, and the session goes on without H.
    host.leave();
    w.endpoints.erase(w.endpoints.begin()); // its program closed, its port free
    w.run(1000);
    std::vector<Received> got = drain(a);
    CHECK(has(got, Event::kDestroyed, h) && has(got, Event::kHost) && !has(got, Event::kSessionLost));
    CHECK(a.hosting() && !b.hosting() && !c.hosting());
    got = drain(b);
    CHECK(has(got, Event::kDestroyed, h) && !has(got, Event::kHost) && !has(got, Event::kSessionLost));
    CHECK(a.members().size() == 3 && b.members().size() == 3 && c.members().size() == 3);
    CHECK(b.send(pb, 0, {7}, true) == Endpoint::Status::kOk);
    CHECK(a.send(pa, pc, {8}, true) == Endpoint::Status::kOk);
    w.run(1000);
    got = drain(a);
    CHECK(hasData(got, pb, {7}));
    got = drain(c);
    CHECK(hasData(got, pb, {7}) && hasData(got, pa, {8}));

    // A newcomer finds the session at its new host and joins it.
    w.network->setDropFilter(nullptr);
    Endpoint &d = w.add();
    uint32_t pd = 0;
    CHECK(joinFirst(w, d) == Endpoint::Status::kOk);
    CHECK(d.create(player("D"), pd) == Endpoint::Status::kOk);
    CHECK(d.members().size() == 4);
    w.run(1000);
    CHECK(has(drain(b), Event::kCreated, pd) && has(drain(c), Event::kCreated, pd));

    // A goes silent: B, next in, takes over, and C and D come to it.
    CHECK(a.port() == kFirstFree);
    w.network->setDropFilter([](const net::Address &from, const net::Address &to, size_t) {
        return from.port == kFirstFree || to.port == kFirstFree;
    });
    w.run(8000);
    got = drain(b);
    CHECK(has(got, Event::kDestroyed, pa) && has(got, Event::kHost));
    CHECK(b.hosting() && !c.hosting() && !d.hosting());
    CHECK(!has(drain(c), Event::kSessionLost) && !has(drain(d), Event::kSessionLost));
    CHECK(c.send(pc, 0, {9}, true) == Endpoint::Status::kOk);
    w.run(1000);
    CHECK(hasData(drain(b), pc, {9}) && hasData(drain(d), pc, {9}));
    CHECK(b.members().size() == 3 && c.members().size() == 3 && d.members().size() == 3);
}

// Over the meeting server, where the endpoints know each other only by
// the addresses it gives them: the new host's listing is found instead.
void testNewHostMeeting() {
    World w;
    w.network->setDropFilter(walledOff);
    net::MeetingServer server(w.network->open(net::kMeetingPort));
    w.transport.meeting = {0x7f000001u, net::kMeetingPort};
    w.transport.broadcast.clear();
    Endpoint &host = w.add();
    Endpoint &a = w.add();
    Endpoint &b = w.add();
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 10) {
            w.run(10);
            server.update(w.now);
        }
    };
    SessionInfo info;
    info.application = kApplication;
    info.flags = kMigrate;
    CHECK(host.host(info, 0) == Endpoint::Status::kOk);
    uint32_t h = 0;
    CHECK(host.create(player("H"), h) == Endpoint::Status::kOk);
    run(100);
    uint32_t pa = 0;
    uint32_t pb = 0;
    for (auto [e, id, name] : {std::tuple{&a, &pa, "A"}, std::tuple{&b, &pb, "B"}}) {
        e->search(kApplication, {});
        run(100);
        Endpoint::Status status = e->join(info.guid, "");
        for (int i = 0; i < 200 && status == Endpoint::Status::kConnecting; ++i) {
            run(20);
            status = e->join(info.guid, "");
        }
        CHECK(status == Endpoint::Status::kOk);
        CHECK(e->create(player(name), *id) == Endpoint::Status::kOk);
    }
    run(500);

    host.leave();
    run(3000);
    CHECK(a.hosting() && !b.hosting());
    CHECK(!has(drain(b), Event::kSessionLost));
    CHECK(b.send(pb, pa, {5}, true) == Endpoint::Status::kOk);
    run(1000);
    CHECK(hasData(drain(a), pb, {5}));
    CHECK(server.sessions().size() == 1);
}

// DirectPlay 4's own messages, against the examples [MC-DPL4CS] gives
// (section 4): a search with a password, and a session's answer.
void testOriginalMessages() {
    dp4::EnumRequest request;
    request.application = "{a052a50b-ffe0-cf11-9c4e-00a0c905425e}";
    request.flags = 2;
    request.password = "Password";
    request.replyPort = 2300;
    const std::vector<uint8_t> m = dp4::enumSessions(request);
    CHECK(m.size() == 70 && (m[0] | m[1] << 8 | m[2] << 16 | uint32_t(m[3]) << 24) == (0xfabu << 20 | 70));
    CHECK(m[4] == 2 && m[5] == 0 && m[6] == 0x08 && m[7] == 0xfc); // AF_INET, port 2300 in network order
    CHECK(std::string(m.begin() + 20, m.begin() + 24) == "play" && m[24] == 14 && m[26] == 2);
    CHECK(m[28] == 0x0b && m[29] == 0xa5 && m[30] == 0x52 && m[31] == 0xa0); // the GUID's first field, little-endian
    CHECK(m[44] == 32); // the password's offset, from the signature
    dp4::EnumRequest back;
    CHECK(dp4::readEnumSessions(m, back) && back.application == request.application && back.flags == 2 &&
          back.password == "Password" && back.replyPort == 2300 && back.replyIp == 0);

    SessionInfo session;
    session.guid = "{21faa08e-42fc-b546-afd3-5e1584fbbb60}";
    session.application = request.application;
    session.flags = 0x404;
    session.maxPlayers = 1000;
    session.players = 1;
    session.name = "LOTHAIR";
    session.user = {0, 2, 3, 4};
    const std::vector<uint8_t> reply = dp4::enumSessionsReply(session, 2300, 0x1e52a0a1);
    CHECK(reply.size() == 128 && reply[28] == 80 && reply[108] == 92); // DPSESSIONDESC2's size; the name's offset
    CHECK((reply[84] | reply[85] << 8 | reply[86] << 16 | uint32_t(reply[87]) << 24) == 0x1e52a0a1); // Reserved1
    SessionInfo found;
    CHECK(dp4::readEnumSessionsReply(reply, found) && found.guid == session.guid && found.name == "LOTHAIR" &&
          found.maxPlayers == 1000 && found.players == 1 && found.user == session.user && found.flags == 0x404 &&
          found.password == "?"); // DPSESSION_PASSWORDREQUIRED
    // A stream of two: one whole, then the next cut short.
    std::vector<uint8_t> stream = reply;
    stream.insert(stream.end(), m.begin(), m.begin() + 30);
    CHECK(dp4::messageLength(stream) == 128);
    stream.erase(stream.begin(), stream.begin() + 128);
    CHECK(dp4::messageLength(stream) == 0 && dp4::messageLength({1, 2, 3, 4}) == -1);
    // Ours aren't taken for the original's.
    CHECK(!dp4::command({'R', 'L', 'D', 'P', 2, 1}));
}

// Both ways over the machine's own TCP, the original stood in for by
// the test with the same messages: a searcher finds its game (which
// can't be joined), and a host answers its search.
void testOriginalOnTcp() {
    std::shared_ptr<net::Sockets> tcp = net::systemSockets();
    World w;
    w.transport.tcp = tcp;
    w.transport.broadcast = {0x7f000001u};
    Endpoint &searcher = w.add();
    // The original's host: its search port on the loopback network.
    std::unique_ptr<net::UdpSocket> original = w.network->open(dp4::kEnumPort);
    CHECK(original != nullptr);
    searcher.search(kAnyApplication, {});
    net::Address from;
    std::vector<uint8_t> data;
    dp4::EnumRequest asked;
    // Ours goes to the same port first; the original's search follows.
    bool searched = false;
    while (!searched && original->receive(from, data))
        searched = dp4::readEnumSessions(data, asked);
    CHECK(searched && asked.replyPort >= dp4::kFirstPort && asked.replyPort <= dp4::kLastPort);
    SessionInfo theirs;
    theirs.guid = "{11111111-2222-3333-4444-555555555555}";
    theirs.application = kApplication;
    theirs.name = "On Windows";
    theirs.maxPlayers = 8;
    theirs.players = 1;
    auto sendOver = [&](uint16_t port, const std::vector<uint8_t> &message) {
        const int s = tcp->open(net::Sockets::kStream);
        CHECK(s >= 0);
        tcp->connect(s, {0x7f000001u, port});
        for (int i = 0; i < 200 && !tcp->ready(s, 10).write; ++i) {}
        size_t sent = 0;
        CHECK(tcp->send(s, message.data(), message.size(), sent) == net::Sockets::kOk && sent == message.size());
        return s;
    };
    const int answer = sendOver(asked.replyPort, dp4::enumSessionsReply(theirs, 2300, 7));
    std::vector<SessionInfo> found;
    for (int i = 0; i < 100 && found.empty(); ++i) {
        w.run(10);
        found = searcher.found();
    }
    tcp->close(answer);
    CHECK(found.size() == 1 && found[0].name == "On Windows" && found[0].application == kApplication);
    CHECK(searcher.join(theirs.guid, "") == Endpoint::Status::kOriginal);

    // Ours, hosted (on the search port, freed): the original's search
    // answered at the TCP port it gives.
    original.reset();
    Endpoint &host = w.add();
    SessionInfo ours;
    ours.application = kApplication;
    ours.name = "In Reluva";
    CHECK(host.host(ours, 0) == Endpoint::Status::kOk);
    const int listen = tcp->open(net::Sockets::kStream);
    uint16_t port = 0;
    for (uint16_t p = dp4::kFirstPort + 50; p <= dp4::kLastPort && !port; ++p)
        if (tcp->bind(listen, {0, p}) == net::Sockets::kOk)
            port = p;
    CHECK(port && tcp->listen(listen, 2) == net::Sockets::kOk);
    dp4::EnumRequest search;
    search.application = kApplication;
    search.flags = 1;
    search.replyPort = port;
    search.replyIp = 0x7f000001u;
    std::unique_ptr<net::UdpSocket> asker = w.network->open(0);
    asker->send({0x7f000001u, kPort}, dp4::enumSessions(search));
    int accepted = -1;
    net::Address peer;
    for (int i = 0; i < 200 && accepted < 0; ++i) {
        w.run(10);
        if (tcp->accept(listen, accepted, peer) != net::Sockets::kOk)
            accepted = -1;
    }
    CHECK(accepted >= 0);
    std::vector<uint8_t> received;
    for (int i = 0; i < 200 && accepted >= 0 && dp4::messageLength(received) <= 0; ++i) {
        uint8_t buffer[512];
        size_t got = 0;
        if (tcp->receive(accepted, buffer, sizeof buffer, got, false) == net::Sockets::kOk)
            received.insert(received.end(), buffer, buffer + got);
        w.run(10);
    }
    SessionInfo answered;
    CHECK(dp4::readEnumSessionsReply(received, answered) && answered.name == "In Reluva" && answered.guid == ours.guid);
    if (accepted >= 0)
        tcp->close(accepted);
    tcp->close(listen);
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
               {"new-host", [](const std::filesystem::path &) { testNewHost(); }},
               {"new-host-meeting", [](const std::filesystem::path &) { testNewHostMeeting(); }},
               {"original-messages", [](const std::filesystem::path &) { testOriginalMessages(); }},
               {"original-on-tcp", [](const std::filesystem::path &) { testOriginalOnTcp(); }},
           });
