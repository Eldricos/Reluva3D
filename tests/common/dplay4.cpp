// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay 4's own protocol, as Microsoft publishes it
// (src/emulation/x86/dplay4_wire.h, dplay4_reliable.h): the messages that
// join a machine to a session and make its players, read back as built
// and laid out as [MC-DPL4CS] says; and the reliable protocol's link
// ([MC-DPL4R]) between two ends on a network the test makes lose,
// repeat and reorder frames, on a clock the test moves.
//
//   reluva-tests common.unit.dplay4

#include "emulation/x86/dplay4_reliable.h"
#include "emulation/x86/dplay4_wire.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::x86;
namespace dp4 = reluva::x86::dp4;

namespace {

uint32_t at32(const std::vector<uint8_t> &v, size_t at) {
    return uint32_t(v[at]) | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24;
}

dp4::Player systemPlayer() {
    dp4::Player p;
    p.id = 0x12345678;
    p.flags = dp4::kSystemPlayer | dp4::kNameServer;
    p.addresses = true;
    p.stream = {0xc0a80102u, 2300};
    p.datagram = {0xc0a80102u, 2350};
    p.systemPlayer = p.id;
    return p;
}

dp4::Player gamePlayer() {
    dp4::Player p;
    p.id = 0x0abc0001;
    p.shortName = "Ann";
    p.longName = "Ann Long";
    p.data = {9, 8, 7};
    p.systemPlayer = 0x12345678;
    return p;
}

bool samePlayer(const dp4::Player &a, const dp4::Player &b) {
    return a.id == b.id && a.flags == b.flags && a.shortName == b.shortName && a.longName == b.longName &&
           a.data == b.data && a.addresses == b.addresses && a.stream == b.stream && a.datagram == b.datagram &&
           a.systemPlayer == b.systemPlayer && a.players == b.players && a.parent == b.parent;
}

// A player's index, by [MC-DPL4R] 2.2.1's examples and its limits.
void testIndexes() {
    std::vector<uint8_t> v;
    dp4::putIndex(v, 0x01);
    CHECK(v == std::vector<uint8_t>{0x01});
    v.clear();
    dp4::putIndex(v, 0xfffe);
    CHECK((v == std::vector<uint8_t>{0xfe, 0xff, 0x03}));
    for (uint32_t i : {0u, 0x7fu, 0x80u, 0x3fffu, 0x4000u, 0xffffu}) {
        v.clear();
        dp4::putIndex(v, uint16_t(i));
        CHECK(v.size() == (i < 0x80 ? 1u : i < 0x4000 ? 2u : 3u));
        size_t at = 0;
        uint16_t back = 0;
        CHECK(dp4::getIndex(v, at, back) && back == i && at == v.size());
    }
    size_t at = 0;
    uint16_t index = 0;
    CHECK(!dp4::getIndex({0x80}, at, index)); // cut short
    at = 0;
    CHECK(!dp4::getIndex({0xff, 0xff, 0x04}, at, index)); // above 0xffff
    at = 0;
    CHECK(!dp4::getIndex({0xff, 0xff, 0x83}, at, index)); // a fourth byte promised
}

void testPlayers() {
    // DPLAYI_PACKEDPLAYER: 48 fixed bytes, the names in UTF-16, the
    // addresses in network order, the data.
    const dp4::Player system = systemPlayer();
    std::vector<uint8_t> v;
    dp4::putPackedPlayer(v, system);
    CHECK(v.size() == 48 + 32 && at32(v, 0) == 80 && at32(v, 36) == 48 && at32(v, 40) == dp4::kDialect);
    CHECK(v[48] == 2 && v[49] == 0 && v[50] == 0x08 && v[51] == 0xfc); // AF_INET, port 2300 big-endian
    CHECK(v[52] == 192 && v[53] == 168 && v[54] == 1 && v[55] == 2);
    dp4::Player back;
    CHECK(dp4::readPackedPlayer(v, 0, back) && samePlayer(back, system));
    const dp4::Player game = gamePlayer();
    v.clear();
    dp4::putPackedPlayer(v, game);
    CHECK(at32(v, 12) == 8 && at32(v, 16) == 18 && at32(v, 24) == 3); // "Ann" and "Ann Long" with terminators
    CHECK(dp4::readPackedPlayer(v, 0, back) && samePlayer(back, game));
    v.resize(v.size() - 1);
    CHECK(!dp4::readPackedPlayer(v, 0, back));

    // DPLAYI_SUPERPACKEDPLAYER: a system player gives its dialect; the
    // service provider's data has a 1-byte length of 0x20, as Windows'
    // ([MC-DPL4CS] note 3).
    v.clear();
    dp4::putSuperPackedPlayer(v, system);
    CHECK(at32(v, 0) == 16 && at32(v, 12) == (1u << 2) && at32(v, 16) == dp4::kDialect && v[20] == 0x20);
    size_t at = 0;
    CHECK(dp4::readSuperPackedPlayer(v, at, back) && at == v.size() && samePlayer(back, system));
    v.clear();
    dp4::Player group = game;
    group.players = {1, 2, 3};
    group.parent = 77;
    dp4::putSuperPackedPlayer(v, game);
    dp4::putSuperPackedPlayer(v, group);
    at = 0;
    CHECK(dp4::readSuperPackedPlayer(v, at, back) && samePlayer(back, game));
    CHECK(at32(v, 12) == (1u | 2u | 1u << 4)); // both names, a 1-byte data length
    CHECK(dp4::readSuperPackedPlayer(v, at, back) && samePlayer(back, group) && at == v.size());
}

// The joining messages: each read back as built, its offsets from the
// signature, and the readers taking it bare as the reliable protocol
// carries it.
void testJoining() {
    std::vector<uint8_t> m = dp4::requestPlayerId(2301, dp4::kSystemPlayer | dp4::kLocal);
    CHECK(m.size() == 32 && dp4::command(m) == dp4::kRequestPlayerId && dp4::senderPort(m) == 2301);
    uint32_t flags = 0;
    CHECK(dp4::readRequestPlayerId(m, flags) && flags == 9);
    CHECK(dp4::readRequestPlayerId(dp4::bare(m), flags) && flags == 9 && dp4::bare(m).size() == 12);

    m = dp4::requestPlayerReply(2300, 0x5555, 0);
    CHECK(m.size() == 28 + 40);
    uint32_t id = 0;
    uint32_t result = 1;
    CHECK(dp4::readRequestPlayerReply(m, id, result) && id == 0x5555 && result == 0);
    m = dp4::requestPlayerReply(2300, 0, dp4::kNoNewPlayers);
    CHECK(dp4::readRequestPlayerReply(m, id, result) && result == dp4::kNoNewPlayers);

    dp4::Player system = systemPlayer();
    m = dp4::addForwardRequest(2301, 0, system, "secret", 1234);
    CHECK(at32(m, 40) == 28 && at32(m, 44) == 28 + 80); // the player, then the password, from the signature
    CHECK(at32(m, m.size() - 4) == 1234);
    dp4::Player back;
    std::string password;
    CHECK(dp4::readAddForwardRequest(m, back, password) && samePlayer(back, system) && password == "secret");
    m = dp4::addForwardRequest(2301, 0, system, "", 0);
    CHECK(dp4::readAddForwardRequest(m, back, password) && password.empty());

    m = dp4::addForward(2300, 7, system);
    CHECK(at32(m, 28) == 7 && at32(m, 32) == system.id && at32(m, 40) == 28);
    CHECK(dp4::readAddForward(m, back) && samePlayer(back, system));
    CHECK(dp4::readAddForward(dp4::bare(m), back) && samePlayer(back, system));
    m = dp4::addForwardAck(2300, 42);
    CHECK(dp4::readAddForwardAck(m, id) && id == 42);
    m = dp4::addForwardReply(2300, dp4::kNoNewPlayers);
    CHECK(dp4::readAddForwardReply(m, result) && result == dp4::kNoNewPlayers);

    const dp4::Player game = gamePlayer();
    m = dp4::createPlayer(2300, game);
    CHECK(dp4::command(m) == dp4::kCreatePlayer && at32(m, 32) == game.id);
    CHECK(dp4::readCreatePlayer(dp4::bare(m), back) && samePlayer(back, game));
    m = dp4::deletePlayer(2300, game.id);
    CHECK(dp4::readDeletePlayer(m, id) && id == game.id && !dp4::readCreatePlayer(m, back));
    uint32_t from = 0;
    uint32_t tick = 0;
    m = dp4::ping(2300, 99, 5000, false);
    CHECK(dp4::command(m) == dp4::kPing && dp4::readPing(dp4::bare(m), from, tick) && from == 99 && tick == 5000);
    CHECK(dp4::command(dp4::ping(2300, 1, 2, true)) == dp4::kPingReply);

    // The session and its players for a machine joining: the session's
    // description at 36 from the signature, its name after it.
    dplay::SessionInfo session;
    session.guid = "{21faa08e-42fc-b546-afd3-5e1584fbbb60}";
    session.application = "{a052a50b-ffe0-cf11-9c4e-00a0c905425e}";
    session.flags = 0xa040;
    session.maxPlayers = 4;
    session.players = 1;
    session.name = "Loco";
    session.user = {1, 2, 3, 4};
    m = dp4::superEnumPlayersReply(2300, session, 0x1e52a0a1, {system, game});
    CHECK(at32(m, 28) == 2 && at32(m, 32) == 0 && at32(m, 44) == 36 && at32(m, 48) == 36 + 80);
    CHECK(at32(m, 36) == 36 + 80 + 10); // the players, after the name's 5 characters
    dplay::SessionInfo read;
    uint32_t seed = 0;
    std::vector<dp4::Player> players;
    CHECK(dp4::readSuperEnumPlayersReply(m, read, seed, players) && seed == 0x1e52a0a1);
    CHECK(read.guid == session.guid && read.flags == 0xa040 && read.name == "Loco" && read.user == session.user &&
          read.maxPlayers == 4 && read.password.empty());
    CHECK(players.size() == 2 && samePlayer(players[0], system) && samePlayer(players[1], game));
    session.password = "pw";
    m = dp4::superEnumPlayersReply(2300, session, 1, {});
    CHECK(dp4::readSuperEnumPlayersReply(m, read, seed, players) && read.password == "?" && players.empty());
}

void testPlayerMessages() {
    uint32_t from = 0;
    uint32_t to = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> m = dp4::playerMessage(2300, 5, 6, {1, 2, 3}, true);
    CHECK(m.size() == 31 && !dp4::isSystem(m) && dp4::messageLength(m) == 31);
    const std::vector<uint8_t> payload{1, 2, 3};
    CHECK(dp4::readPlayerMessage(m, true, from, to, data) && from == 5 && to == 6 && data == payload);
    // Without ids, data starting "play" would be taken for DirectPlay's
    // own: it goes wrapped, and comes out as it was.
    const std::vector<uint8_t> tricky{'p', 'l', 'a', 'y', 14, 0, 5, 0};
    m = dp4::playerMessage(2300, 0, 0, tricky, false);
    CHECK(dp4::command(m) == dp4::kPlayerWrapper);
    CHECK(dp4::readPlayerMessage(m, false, from, to, data) && data == tricky);
    CHECK(!dp4::readPlayerMessage(dp4::requestPlayerId(1, 1), true, from, to, data));
}

// Two ends of a link on a network that does what `fate` says with each
// frame: deliver it (1), lose it (0), or deliver it twice (2); frames
// are held back and delivered last-first when `reorder` says.
struct Pair {
    uint64_t now = 1000;
    dp4::ReliableLink a{[this] { return now; }, 64};
    dp4::ReliableLink b{[this] { return now; }, 64};
    std::function<int(size_t index, bool toB)> fate = [](size_t, bool) { return 1; };
    bool reorder = false;
    size_t sent = 0;
    std::vector<dp4::ReliableLink::Delivered> atB;
    std::vector<dp4::ReliableLink::Delivered> atA;

    void carry(dp4::ReliableLink &from, dp4::ReliableLink &to, bool toB) {
        std::vector<std::vector<uint8_t>> frames = from.takeFrames();
        if (reorder)
            std::reverse(frames.begin(), frames.end());
        for (const std::vector<uint8_t> &f : frames) {
            const int n = fate(sent++, toB);
            for (int i = 0; i < n; ++i)
                to.receive(f);
        }
    }
    void run(uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 10) {
            now += 10;
            a.update();
            b.update();
            carry(a, b, true);
            carry(b, a, false);
            while (auto d = b.take())
                atB.push_back(*d);
            while (auto d = a.take())
                atA.push_back(*d);
        }
    }
};

std::vector<uint8_t> numbered(int i, size_t size) {
    std::vector<uint8_t> v(size);
    for (size_t k = 0; k < size; ++k)
        v[k] = uint8_t(i * 7 + int(k));
    return v;
}

// Reliable messages, long and short, arrive whole, once and in order,
// whatever the network does with the frames.
void testReliableOrder() {
    for (int kind = 0; kind < 3; ++kind) {
        Pair p;
        if (kind == 0) // a frame in three lost, both ways
            p.fate = [](size_t i, bool) { return i % 3 == 1 ? 0 : 1; };
        else if (kind == 1) // every frame twice
            p.fate = [](size_t, bool) { return 2; };
        else // reordered, and one in five lost
            p.reorder = true, p.fate = [](size_t i, bool) { return i % 5 == 2 ? 0 : 1; };
        for (int i = 0; i < 40; ++i)
            p.a.send(numbered(i, size_t(i % 4 == 0 ? 150 : 10 + i)), true, 3, 4);
        p.run(30000);
        CHECK(p.atB.size() == 40);
        for (size_t i = 0; i < p.atB.size() && i < 40; ++i) {
            CHECK(p.atB[i].reliable && p.atB[i].from == 3 && p.atB[i].to == 4);
            CHECK(p.atB[i].data == numbered(int(i), size_t(i % 4 == 0 ? 150 : 10 + i)));
        }
        CHECK(p.a.outstanding() == 0);
    }
}

// No more than 24 message ids are out at once; the rest wait their turn.
void testWindow() {
    Pair p;
    p.fate = [](size_t, bool) { return 0; };
    for (int i = 0; i < 40; ++i)
        p.a.send(numbered(i, 5), true, 1, 2);
    std::vector<uint8_t> ids;
    for (const std::vector<uint8_t> &f : p.a.takeFrames())
        if (std::find(ids.begin(), ids.end(), f[3]) == ids.end())
            ids.push_back(f[3]);
    CHECK(ids.size() == size_t(dp4::ReliableLink::kWindow) && ids.front() == 1);
    CHECK(p.a.outstanding() == 40);
    p.fate = [](size_t, bool) { return 1; };
    p.run(10000);
    CHECK(p.atB.size() == 40 && p.a.outstanding() == 0);
}

// A frame not acknowledged goes again after a second, before any round
// trip is measured; then after the measured time and its spread.
void testRetry() {
    Pair p;
    CHECK(p.a.retryMs() == 1000);
    p.a.send({1, 2, 3}, true, 1, 2);
    const std::vector<std::vector<uint8_t>> first = p.a.takeFrames();
    CHECK(first.size() == 1 && (first[0][2] & 0x25) == 0x25 && first[0][5] == 0); // CMD, SAK, RLY; serial 0
    p.now += 999;
    p.a.update();
    CHECK(p.a.takeFrames().empty());
    p.now += 1;
    p.a.update();
    const std::vector<std::vector<uint8_t>> again = p.a.takeFrames();
    CHECK(again.size() == 1 && again[0][4] == first[0][4] && again[0][5] == 1); // same sequence, serial 1
    // Answered 20 ms after a fresh send: the wait comes down to what the
    // trips take.
    p.b.receive(again[0]);
    p.a.send({4}, true, 1, 2);
    for (const std::vector<uint8_t> &f : p.a.takeFrames())
        p.b.receive(f);
    p.now += 20;
    for (const std::vector<uint8_t> &f : p.b.takeFrames())
        p.a.receive(f);
    CHECK(p.a.outstanding() == 0 && p.a.retryMs() < 1000 && p.a.retryMs() >= 50);
}

// A frame missing while later ones come: the receiver says which (a NACK)
// and the sender sends it again at once, without waiting out its timer.
void testNack() {
    Pair p;
    p.a.send(numbered(1, 10), true, 1, 2);
    p.a.send(numbered(2, 10), true, 1, 2);
    p.a.send(numbered(3, 10), true, 1, 2);
    const std::vector<std::vector<uint8_t>> frames = p.a.takeFrames();
    CHECK(frames.size() == 3);
    p.b.receive(frames[0]);
    p.b.receive(frames[2]);
    std::vector<std::vector<uint8_t>> answers = p.b.takeFrames();
    const auto nack = std::find_if(answers.begin(), answers.end(), [](const std::vector<uint8_t> &f) {
        return f.size() >= 3 && (f[2] & 0x82) == 0x82;
    });
    CHECK(nack != answers.end());
    if (nack != answers.end())
        CHECK((*nack)[5] == frames[1][4]); // the first missing sequence
    for (const std::vector<uint8_t> &f : answers)
        p.a.receive(f);
    const std::vector<std::vector<uint8_t>> resent = p.a.takeFrames();
    CHECK(resent.size() == 1 && resent[0][4] == frames[1][4]);
    p.b.receive(resent[0]);
    std::vector<std::vector<uint8_t>> got;
    while (auto d = p.b.take())
        got.push_back(d->data);
    CHECK(got.size() == 3 && got[1] == numbered(2, 10) && got[2] == numbered(3, 10));
}

// Unreliable messages come as soon as they're whole, aren't sent again,
// and one lost holds up a reliable one behind it only for a while.
void testUnreliable() {
    Pair p;
    p.fate = [](size_t i, bool toB) { return toB && i == 0 ? 0 : 1; };
    p.a.send({1}, false, 1, 2);
    p.a.send({2}, true, 1, 2);
    p.a.send({3}, false, 1, 2);
    p.run(100);
    CHECK(p.atB.size() == 1 && !p.atB[0].reliable && p.atB[0].data == std::vector<uint8_t>{3});
    p.run(10000);
    CHECK(p.atB.size() == 2 && p.atB[1].reliable && p.atB[1].data == std::vector<uint8_t>{2});
    CHECK(p.a.outstanding() == 0);
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.dplay4", {
                                     {"indexes", [](const std::filesystem::path &) { testIndexes(); }},
                                     {"players", [](const std::filesystem::path &) { testPlayers(); }},
                                     {"joining", [](const std::filesystem::path &) { testJoining(); }},
                                     {"player-messages", [](const std::filesystem::path &) { testPlayerMessages(); }},
                                     {"reliable-order", [](const std::filesystem::path &) { testReliableOrder(); }},
                                     {"window", [](const std::filesystem::path &) { testWindow(); }},
                                     {"retry", [](const std::filesystem::path &) { testRetry(); }},
                                     {"nack", [](const std::filesystem::path &) { testNack(); }},
                                     {"unreliable", [](const std::filesystem::path &) { testUnreliable(); }},
                                 });
