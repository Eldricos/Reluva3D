// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Network games over the in-memory network: finding a game, joining it,
// a third refused, the lobby's chat, mission and countdown, a lossy
// network, a player gone; and the Choose Netgame and Net Lobby screens.

#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/netplay.h"
#include "engines/stawayteam/ui/net_menus.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;
using namespace reluva::stawayteam::netplay;

namespace {

// About one datagram in three lost, by a hash of its number, so that the
// loss doesn't keep step with the resends.
bool dropAboutAThird(const net::Address &, const net::Address &, size_t index) {
    return (uint32_t(index) * 2654435761u >> 16) % 3 == 0;
}

void testSession(bool lossy) {
    auto network = net::LoopbackNetwork::create();
    if (lossy)
        network->setDropFilter(dropAboutAThird);
    auto host = Session::host(network->open(kDefaultPort), "Kirk");
    auto browser = Session::browse(network->open(0), {net::Address::broadcast(kDefaultPort)});
    uint64_t now = 1000;
    std::vector<Session *> all{host.get(), browser.get()};
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 20) {
            now += 20;
            for (Session *s : all)
                s->update(now);
        }
    };
    run(2000);
    if (lossy && browser->found().empty()) {
        browser->refresh();
        run(2000);
    }
    CHECK(browser->found().size() == 1);
    if (browser->found().empty())
        return;
    CHECK(browser->found()[0].gameName == "Kirk's Game" && browser->found()[0].players == 1 &&
          browser->found()[0].open);
    auto spock = Session::join(network->open(0), browser->found()[0].address, "Spock");
    all = {host.get(), spock.get()};
    run(2000);
    CHECK(spock->state() == Session::State::kLobby && spock->me() == 1);
    CHECK(host->players() == std::vector<std::string>({"Kirk", "Spock"}));
    CHECK(spock->players() == host->players());
    // A third: two play a game.
    auto bones = Session::join(network->open(0), browser->found()[0].address, "McCoy");
    all = {host.get(), spock.get(), bones.get()};
    run(lossy ? 6000 : 1000);
    CHECK(bones->state() == Session::State::kRefused && host->players().size() == 2);
    all = {host.get(), spock.get()};
    // Both talk; each sees every line, in the host's order.
    host->chat("Hailing frequencies open.");
    spock->chat("Fascinating.");
    spock->chat("Logical.");
    run(lossy ? 4000 : 1000);
    const auto lines = [](Session &s) {
        std::vector<std::string> out;
        ChatLine line;
        while (s.nextChat(line))
            out.push_back(std::to_string(line.from) + " " + line.text);
        return out;
    };
    const std::vector<std::string> hostSaw = lines(*host);
    CHECK(hostSaw == std::vector<std::string>({"0 Hailing frequencies open.", "1 Fascinating.", "1 Logical."}));
    CHECK(lines(*spock) == hostSaw);
    // Only the host chooses the mission and starts; both count down and start.
    spock->chooseMission(7);
    spock->start();
    host->chooseMission(3);
    run(lossy ? 3000 : 500);
    CHECK(spock->mission() == 3 && !spock->counting());
    host->start();
    run(lossy ? 1500 : 500);
    CHECK(host->counting() && spock->counting() && spock->countdownLeft() <= kCountdownMs);
    run(kCountdownMs + (lossy ? 3000 : 500));
    CHECK(host->state() == Session::State::kStarted && spock->state() == Session::State::kStarted);
    // The host gone, the other is cut off.
    host->leave();
    run(lossy ? Session::kTimeoutMs + 1000 : 500);
    CHECK(spock->state() == Session::State::kLost);
}

void testSessions() {
    testSession(false);
    testSession(true);
}

void testCountdownStopped() {
    auto network = net::LoopbackNetwork::create();
    auto host = Session::host(network->open(kDefaultPort), "Kirk");
    auto spock = Session::join(network->open(0), {0x7f000001u, kDefaultPort}, "Spock");
    uint64_t now = 0;
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 20) {
            now += 20;
            host->update(now);
            if (spock)
                spock->update(now);
        }
    };
    run(500);
    host->start(); // no mission yet: nothing
    run(200);
    CHECK(!host->counting());
    host->chooseMission(1);
    host->start();
    run(1000);
    CHECK(spock->counting());
    // Another mission chosen stops it; and the other leaving does, and
    // opens the game again.
    host->chooseMission(2);
    run(300);
    CHECK(!host->counting() && !spock->counting());
    host->start();
    run(300);
    spock->leave();
    spock.reset();
    run(300);
    CHECK(!host->counting() && host->players().size() == 1);
}

TextObject netRules() {
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\nMenu=CAnimatedMenuInfo\n{\nMenu Name=Choose Netgame\n"
                          "Menu Definition=CStarTrekChooseNetGameMenu\n{\nList Pos=66,28\nList Size=142,182\n"
                          "Name Pos=229,62\nEnterNameText Pos=302,32\n}\n}\n"
                          "Menu=CAnimatedMenuInfo\n{\nMenu Name=Net Lobby\n"
                          "Menu Definition=CStarTrekNetLobbyMenu\n{\nChat Pos=50,58\nChatMargin Size=8,7\n}\n}\n}\n",
                          rules));
    return rules;
}

Event typed(const char *text) {
    Event ev;
    ev.type = EventType::kTextInput;
    std::strncpy(ev.text, text, sizeof ev.text - 1);
    return ev;
}

Event keyDown(uint32_t key) {
    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = key;
    return ev;
}

void testScreens() {
    MemoryArchive data;
    const TextObject rules = netRules();
    // Without a network it says so, and nothing can be made or joined.
    ChooseNetGameScreen none(data, rules, {}, "Kirk", 640, 480);
    CHECK(none.message() == "No Network Detected");
    // A game made on the network is found; the name typed is kept, cut
    // at 16.
    auto network = net::LoopbackNetwork::create();
    NetAccess access;
    access.open = [&](uint16_t port) { return network->open(port); };
    access.askAt = {net::Address::broadcast(kDefaultPort)};
    auto host = Session::host(network->open(kDefaultPort), "Kirk");
    ChooseNetGameScreen choose(data, rules, access, "", 640, 480);
    CHECK(choose.message().empty());
    for (const char *c : {"Spock", " of Vulcan, son of Sarek"})
        choose.handleEvent(typed(c), nullptr, 0);
    CHECK(choose.name() == "Spock of Vulcan,");
    choose.handleEvent(keyDown(key::kBackspace), nullptr, 0);
    CHECK(choose.name() == "Spock of Vulcan");
    uint64_t now = 0;
    for (int i = 0; i < 50; ++i) {
        now += 20;
        host->update(now);
        CHECK(choose.update(now) == ChooseNetGameScreen::Choice::kNone);
    }
    CHECK(choose.games().size() == 1 && choose.games()[0].gameName == "Kirk's Game");
    CHECK(choose.handleEvent(keyDown(key::kEscape), nullptr, now) == ChooseNetGameScreen::Choice::kCancel);
    // The lobby: what's typed, sent with Enter, comes back as the host's
    // line; the host's lobby chooses the first mission it offers.
    auto spock = Session::join(network->open(0), {0x7f000001u, kDefaultPort}, "Spock");
    NetLobbyScreen hostLobby(data, rules, {{4, "Academy", "Detain Admiral Quay."}, {5, "Ship", ""}}, std::move(host));
    NetLobbyScreen spockLobby(data, rules, {{4, "Academy", ""}, {5, "Ship", ""}}, std::move(spock));
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 20) {
            now += 20;
            hostLobby.update(now);
            spockLobby.update(now);
        }
    };
    run(500);
    CHECK(hostLobby.session().mission() == 4 && spockLobby.session().mission() == 4);
    spockLobby.handleEvent(typed("Live long"), nullptr);
    spockLobby.handleEvent(keyDown(key::kReturn), nullptr);
    run(500);
    CHECK(hostLobby.lines() == std::vector<std::string>({"Spock: Live long"}));
    CHECK(spockLobby.lines() == hostLobby.lines());
    // Started, each is told once.
    hostLobby.session().start();
    int starts = 0;
    for (uint64_t t = 0; t < kCountdownMs + 1000; t += 20) {
        now += 20;
        starts += hostLobby.update(now) == NetLobbyScreen::Choice::kStart;
        starts += spockLobby.update(now) == NetLobbyScreen::Choice::kStart;
    }
    CHECK(starts == 2);
    // Leaving, the other's lobby closes.
    CHECK(hostLobby.handleEvent(keyDown(key::kEscape), nullptr) == NetLobbyScreen::Choice::kExit);
    CHECK(spockLobby.update(now + 20) == NetLobbyScreen::Choice::kExit);
}

} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.network", {
                                          {"sessions", [](const std::filesystem::path &) { testSessions(); }},
                                          {"countdown", [](const std::filesystem::path &) { testCountdownStopped(); }},
                                          {"screens", [](const std::filesystem::path &) { testScreens(); }},
                                      });
