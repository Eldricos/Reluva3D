// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: Network play over the in-memory network:
// finding a game, joining, the lobby, chat, the start, the players' actions
// in one order, a lossy network, and a player gone.

#include "engines/clue/game/ai.h"
#include "engines/clue/game/board.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/formats/resources.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"
#include "tests/engines/clue/support.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;
using namespace reluva::clue::testing;

// Network play over the in-memory network: finding a game, joining, the
// lobby, chat, the start, the players' actions in one order, a lossy
// network, and a player gone.
// About one datagram in three lost, by a hash of its number: not every
// third, whose rhythm can meet the resends' and lose the same message each
// time, as no real network would.
static bool dropAboutAThird(const net::Address &, const net::Address &, size_t index) {
    return (uint32_t(index) * 2654435761u >> 16) % 3 == 0;
}

static void testNetplay(bool lossy) {
    using namespace netplay;
    auto network = net::LoopbackNetwork::create();
    if (lossy)
        network->setDropFilter(dropAboutAThird);
    auto host = Session::host(network->open(kDefaultPort), "Boddy", "Hostess", 4);
    auto browser = Session::browse(network->open(0), {net::Address::broadcast(kDefaultPort)});
    uint64_t now = 1000;
    std::vector<Session *> all{host.get(), browser.get()};
    const auto run = [&](int ms) {
        for (int t = 0; t < ms; t += 20) {
            now += 20;
            for (Session *s : all)
                s->update(now);
        }
    };
    run(lossy ? 8000 : 3000); // each ask or its answer may be lost: more of them
    CHECK(browser->found().size() == 1);
    if (browser->found().empty())
        return;
    CHECK(browser->found()[0].gameName == "Boddy" && browser->found()[0].hostName == "Hostess" &&
          browser->found()[0].humans == 1 && browser->found()[0].maxHumans == 4);
    const net::Address at = browser->found()[0].address;
    auto anne = Session::join(network->open(0), at, "Anne");
    auto bob = Session::join(network->open(0), at, "Bob");
    all = {host.get(), anne.get(), bob.get()};
    run(3000);
    CHECK(anne->state() == Session::State::kLobby && bob->state() == Session::State::kLobby);
    CHECK(anne->me() != bob->me() && anne->me() > 0);
    // Taking suspects; the host sets a computer player and the variant.
    host->take(kScarlet);
    anne->take(kMustard);
    bob->take(kScarlet); // taken: refused
    run(2000);
    CHECK(!host->canStart()); // Bob has no suspect yet
    bob->take(kWhite);
    host->setSeat(kPlum, Seat::kComputer, 2);
    host->setSeat(kMustard, Seat::kComputer, 0); // Anne's: not the host's to change
    host->setNoDieRoll(true);
    run(2000);
    for (Session *s : all) {
        const Lobby &l = s->lobby();
        CHECK(l.seats[kScarlet].kind == Seat::kHuman && l.seats[kScarlet].owner == 0 &&
              l.seats[kScarlet].name == "Hostess");
        CHECK(l.seats[kMustard].kind == Seat::kHuman && l.seats[kMustard].name == "Anne");
        CHECK(l.seats[kWhite].kind == Seat::kHuman && l.seats[kWhite].name == "Bob");
        CHECK(l.seats[kPlum].kind == Seat::kComputer && l.seats[kPlum].level == 2 && l.noDieRoll);
    }
    // Chat goes to everyone, said by its speaker.
    std::string line;
    while (anne->nextChat(line) || bob->nextChat(line) || host->nextChat(line)) {}
    anne->chat("Hello");
    run(1000);
    for (Session *s : all)
        CHECK(s->nextChat(line) && line == "Anne: Hello");
    // The start, and the players' actions: in the host's order, the same
    // for everyone; one for a suspect not theirs is dropped.
    CHECK(host->canStart());
    host->start(4242);
    run(1000);
    CHECK(anne->state() == Session::State::kPlaying && anne->seed() == 4242 && bob->lobby().started);
    Command c;
    c.type = Command::kRoomWalk; // a walk about the room, to a world point
    c.seat = kMustard;
    c.args = {1900, -2100};
    anne->submit(c);
    c.type = Command::kWalk;
    c.seat = kMustard;
    c.args = {3, 28, 12, 28, 13, -1, 4};
    anne->submit(c);
    c.type = Command::kRoll;
    c.seat = kScarlet;
    c.args.clear();
    host->submit(c);
    c.seat = kWhite; // Bob's, sent by Anne
    anne->submit(c);
    for (int k = 0; k < 20; ++k) {
        c.type = Command::kShow;
        c.seat = kWhite;
        c.args = {int16_t(k)};
        bob->submit(c);
    }
    run(4000);
    std::vector<std::vector<Command>> got(all.size());
    for (size_t i = 0; i < all.size(); ++i)
        while (all[i]->nextCommand(c))
            got[i].push_back(c);
    CHECK(got[0].size() == 23);
    CHECK(std::any_of(got[0].begin(), got[0].end(), [](const Command &k) {
        return k.type == Command::kRoomWalk && k.args == std::vector<int16_t>{1900, -2100};
    }));
    for (size_t i = 1; i < all.size(); ++i) {
        CHECK(got[i].size() == got[0].size());
        for (size_t k = 0; k < got[0].size() && k < got[i].size(); ++k)
            CHECK(got[i][k].type == got[0][k].type && got[i][k].seat == got[0][k].seat &&
                  got[i][k].args == got[0][k].args);
    }
    // Bob's shows in the order he sent them.
    int expect = 0;
    for (const Command &k : got[0])
        if (k.type == Command::kShow)
            CHECK(k.args[0] == expect++);
    // Bob goes quiet: the host gives Mrs. White to the computer, for everyone.
    all = {host.get(), anne.get()};
    run(int(Channel::kTimeoutMs) + 2000);
    CHECK(host->nextLeft() == kWhite);
    bool takenOver = false;
    while (anne->nextCommand(c))
        takenOver = takenOver || (c.type == Command::kTakeOver && c.seat == kWhite);
    CHECK(takenOver);
    // The host gone: Anne's session is lost.
    host->leave();
    run(1000);
    CHECK(anne->state() == Session::State::kLost);
    Command decoded;
    size_t offset = 0;
    const Bytes bytes = encodeCommand({Command::kAccuse, 2, {1, 2, 3}});
    CHECK(decodeCommand(bytes, offset, decoded) && decoded.type == Command::kAccuse && decoded.args.size() == 3);
}

static const reluva::test::Suite kSuite("clue.unit.network",
                                        {
                                            {"network", [](const std::filesystem::path &) { testNetplay(false); }},
                                            {"lossy network", [](const std::filesystem::path &) { testNetplay(true); }},
                                        });
