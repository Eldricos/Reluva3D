// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: multiplayer: the session and Cops & Robbers.
// Every file is built in the test.

#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testNetSession() {
    // Rotations through quaternions, whichever way the largest term falls.
    for (const Mat3 &m : {Mat3::rotation(normalized(Vec3{0.3f, 1, 0.2f}), 1.1f), Mat3::rotationY(3.0f),
                          Mat3::rotation({1, 0, 0}, 2.9f), Mat3::rotation({0, 0, 1}, -2.8f), Mat3{}})
        CHECK(sameMatrix(matrixOfQuaternion(quaternionOf(m)), m));

    // A car between two states, and carried on past the last.
    std::deque<NetCarState> states(2);
    states[1].clock = 1;
    states[1].position = {10, 0, 0};
    states[1].velocity = {10, 0, 0};
    states[1].rotation = quaternionOf(Mat3::rotationY(1));
    NetCarState at;
    CHECK(carStateAt(states, 0.5f, at) && std::fabs(at.position[0] - 5) < 1e-4f);
    CHECK(sameMatrix(matrixOfQuaternion(at.rotation), Mat3::rotationY(0.5f)));
    CHECK(carStateAt(states, 1.2f, at) && std::fabs(at.position[0] - 12) < 1e-3f);
    CHECK(carStateAt(states, 9, at) && std::fabs(at.position[0] - 15) < 1e-3f); // half a second at most
    CHECK(!carStateAt({}, 0, at));

    // A host, found by a broadcast, and two players who join it; a third
    // is one too many.
    auto network = net::LoopbackNetwork::create();
    NetSettings settings;
    settings.mode = NetMode::kCircuit;
    settings.maxPlayers = 3;
    auto host = NetSession::host(network->open(kNetPort), "Hosting", settings);
    NetBrowser browser(network->open(0));
    std::vector<NetSession *> players;
    uint64_t t = 1000;
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 10; ++i) {
            t += 10;
            host->update(t);
            for (NetSession *p : players)
                p->update(t);
            browser.update(t);
        }
    };
    run(300);
    CHECK(browser.sessions().size() == 1 && browser.sessions()[0].name == "Hosting" &&
          browser.sessions()[0].players == 1 && browser.sessions()[0].mode == NetMode::kCircuit);
    const net::Address at0 = browser.sessions()[0].address;
    auto ann = NetSession::join(network->open(0), at0, "Ann");
    auto bob = NetSession::join(network->open(0), at0, "Bob");
    players = {ann.get(), bob.get()};
    CHECK(ann->joining());
    run(600);
    CHECK(ann->myId() == 1 && bob->myId() == 2 && host->players().size() == 3);
    CHECK(ann->players().size() == 3 && ann->settings().mode == NetMode::kCircuit && ann->sessionName() == "Hosting");
    // The host sides them to even up the teams; a player's own status
    // doesn't undo it until they choose.
    CHECK(host->player(1)->team == 1 && host->player(2)->team == 0 && ann->me()->team == 1);
    auto joined = host->takeNotices();
    CHECK(joined.size() == 2 && joined[0].kind == NetNotice::Kind::kJoined && joined[0].name == "Ann");
    auto carl = NetSession::join(network->open(0), at0, "Carl");
    players.push_back(carl.get());
    run(300);
    auto refused = carl->takeNotices();
    CHECK(carl->closed() && refused.size() == 1 && refused[0].kind == NetNotice::Kind::kRefused &&
          refused[0].reason == kRefusedFull);
    players.pop_back();

    // Chat, cars and teams, with a third of the datagrams lost.
    network->setDropFilter([](const net::Address &, const net::Address &, size_t i) { return i % 3 == 0; });
    ann->say("hello");
    ann->say("again");
    ann->setCar("vpcab", 2);
    bob->setTeam(1);
    run(2500);
    CHECK(host->chat().size() == 2 && host->chat()[0].from == "Ann" && host->chat()[0].text == "hello" &&
          host->chat()[1].text == "again");
    CHECK(bob->chat().size() == 2 && bob->chat()[1].text == "again" && ann->chat().size() == 2);
    CHECK(host->player(1)->car == "vpcab" && host->player(1)->paint == 2 && bob->player(2)->team == 1);

    // Starting: everyone ready, then loaded; the clocks agree.
    CHECK(!host->everyoneReady() && host->start(t));
    ann->setReady(true);
    bob->setReady(true);
    run(800);
    CHECK(host->everyoneReady() && !host->start(t));
    run(800);
    bool started = false;
    for (const NetNotice &n : ann->takeNotices())
        started = started || n.kind == NetNotice::Kind::kStarted;
    CHECK(started && ann->phase() == NetSession::Phase::kLoading && ann->round() == host->round() &&
          ann->seed() == host->seed());
    host->setLoaded();
    ann->setLoaded();
    run(800);
    CHECK(host->phase() == NetSession::Phase::kLoading); // Bob's still loading
    bob->setLoaded();
    run(800);
    CHECK(host->phase() == NetSession::Phase::kPlaying && bob->phase() == NetSession::Phase::kPlaying);
    run(3000);
    CHECK(ann->counting() && std::fabs(ann->clock(t) - host->clock(t)) < 0.1f && host->clock(t) > 0.5f);

    // Cars passed on to the others.
    NetCarState mine;
    mine.position = {1, 2, 3};
    mine.next = 4;
    for (int i = 0; i < 10; ++i) {
        mine.clock = ann->clock(t);
        mine.position[0] += 1;
        ann->sendCar(mine);
        NetCarState h;
        h.clock = host->clock(t);
        h.position = {50, 0, 0};
        host->sendCar(h);
        run(60);
    }
    run(300);
    CHECK(bob->cars().count(1) && bob->cars().count(0) && !bob->cars().count(2) && host->cars().count(1));
    CHECK(!bob->cars().at(1).empty() && bob->cars().at(1).back().next == 4 &&
          std::fabs(bob->cars().at(1).back().position[0] - 11) < 1e-4f);
    host->setGameState({7, 8, 9}, true);
    run(500);
    CHECK(ann->gameState() == std::vector<uint8_t>({7, 8, 9}) && ann->over());

    // Back to the lobby: a new round, nobody ready.
    host->backToLobby();
    run(800);
    CHECK(ann->phase() == NetSession::Phase::kLobby && ann->round() == host->round() && !host->player(1)->ready &&
          !host->everyoneReady());
    network->setDropFilter(nullptr);

    // A computer player: ready and loaded from the start, sided like
    // anyone, its car passed on as anyone's.
    CHECK(host->addBot("Opponent 1", "vpcab") == kNoPlayer); // full
    settings.maxPlayers = 4;
    host->setSettings(settings);
    const uint8_t botId = host->addBot("Opponent 1", "vpcab");
    run(300);
    const NetPlayer *bot = ann->player(botId);
    CHECK(botId != kNoPlayer && bot && bot->bot && bot->ready && host->everyoneReady() == false);
    bob->setReady(true);
    ann->setReady(true);
    run(800);
    CHECK(host->everyoneReady() && !host->start(t));
    run(300);
    host->setLoaded();
    ann->setLoaded();
    bob->setLoaded();
    run(800);
    CHECK(host->phase() == NetSession::Phase::kPlaying); // nobody waited for the computer player
    run(3200);
    NetCarState botCar;
    botCar.clock = host->clock(t);
    botCar.position = {7, 0, 7};
    host->sendBotCar(botId, botCar);
    host->sendBotCar(2, botCar); // not a computer player: ignored
    run(300);
    CHECK(ann->cars().count(botId) && ann->cars().at(botId).back().position[0] == 7);
    host->backToLobby();
    run(500);
    CHECK(host->player(botId)->ready && ann->player(botId)->ready);
    host->removeBots();
    run(300);
    CHECK(!host->player(botId) && !ann->player(botId));

    // Bob goes; Ann's ejected; a player whose host goes quiet gives up.
    bob->leave();
    run(300);
    bool left = false;
    for (const NetNotice &n : ann->takeNotices())
        left = left || (n.kind == NetNotice::Kind::kLeft && n.name == "Bob");
    CHECK(left && host->players().size() == 2);
    host->eject(1);
    run(300);
    auto ejected = ann->takeNotices();
    CHECK(ann->closed() && !ejected.empty() && ejected.back().kind == NetNotice::Kind::kEjected &&
          host->players().size() == 1);
    auto dan = NetSession::join(network->open(0), at0, "Dan");
    players = {dan.get()};
    run(300);
    CHECK(dan->myId() == 1);
    for (int i = 0; i < 700; ++i) { // the host stops
        t += 10;
        dan->update(t);
    }
    auto quit = dan->takeNotices();
    CHECK(dan->closed() && !quit.empty() && quit.back().kind == NetNotice::Kind::kHostQuit);
    // And a host whose player goes quiet drops it.
    for (int i = 0; i < 700; ++i) {
        t += 10;
        host->update(t);
    }
    CHECK(host->players().size() == 1);
}

// The gaps closed: a password proved, not sent; joining a game already
// playing; the host's checks; the session handed on when the host goes.
void testNetSessionMore() {
    CHECK(hmacMd5("key", 3, "The quick brown fox jumps over the lazy dog", 43) ==
          (std::array<uint8_t, 16>{0x80, 0x07, 0x07, 0x13, 0x46, 0x3e, 0x77, 0x49, 0xb9, 0x0c, 0x2d, 0xc2, 0x49, 0x11,
                                   0xe2, 0x75})); // RFC 2104's form, as published for MD5

    auto network = net::LoopbackNetwork::create();
    NetSettings settings;
    settings.mode = NetMode::kCruise;
    auto host = NetSession::host(network->open(kNetPort), "Hosting", settings, "swordfish");
    const net::Address at{0x7f000001, kNetPort};
    std::vector<NetSession *> players;
    uint64_t t = 1000;
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 10; ++i) {
            t += 10;
            if (host)
                host->update(t);
            for (NetSession *p : players)
                p->update(t);
        }
    };
    auto wrong = NetSession::join(network->open(0), at, "Eve", "trout");
    players = {wrong.get()};
    run(500);
    auto refusal = wrong->takeNotices();
    CHECK(wrong->closed() && !refusal.empty() && refusal.back().reason == kRefusedPassword);
    auto ann = NetSession::join(network->open(0), at, "Ann", "swordfish");
    auto bob = NetSession::join(network->open(0), at, "Bob", "swordfish");
    players = {ann.get(), bob.get()};
    run(600);
    CHECK(ann->myId() == 1 && bob->myId() == 2 && host->players().size() == 3);

    // Started and playing; Carl joins late and loads straight in.
    ann->setReady(true);
    bob->setReady(true);
    run(500);
    CHECK(!host->start(t));
    run(300);
    host->setLoaded();
    ann->setLoaded();
    bob->setLoaded();
    run(3500);
    CHECK(host->phase() == NetSession::Phase::kPlaying && host->clock(t) > 0.2f);
    auto carl = NetSession::join(network->open(0), at, "Carl", "swordfish");
    players.push_back(carl.get());
    run(600);
    bool started = false;
    for (const NetNotice &n : carl->takeNotices())
        started = started || n.kind == NetNotice::Kind::kStarted;
    CHECK(carl->myId() == 3 && started && carl->phase() == NetSession::Phase::kPlaying && carl->counting() &&
          std::fabs(carl->clock(t) - host->clock(t)) < 0.1f);
    carl->setLoaded();

    // A car is held to its own top speed (with room for a hill or a
    // knock), not only to what no car does; falling isn't held to it.
    host->setTopSpeeds({{"vpbug", 40}});
    carl->setCar("vpbug", 0);
    run(300);
    ann->setCar("vpnotlisted", 0);
    run(300);
    CHECK(std::fabs(host->speedLimit(3) - 60) < 1e-3f && std::fabs(host->speedLimit(1) - 110) < 1e-3f);
    NetCarState c;
    c.position = {100, 0, 100};
    for (const float speed : {45.0f, 55.0f}) { // within the room
        c.clock = carl->clock(t);
        c.velocity = {speed, 0, 0};
        carl->sendCar(c);
        run(100);
        c.position[0] += speed / 10;
    }
    c.clock = carl->clock(t);
    c.velocity = {0, -80, 0}; // falling
    carl->sendCar(c);
    run(100);
    CHECK(host->rejected(3) == 0);
    c.clock = carl->clock(t);
    c.velocity = {75, 0, 0}; // a Beetle at 75 m/s: tuned, not driven
    carl->sendCar(c);
    run(100);
    // (Sent again until the next, so turned down more than once.)
    const int once = host->rejected(3);
    CHECK(once >= 1 && host->cars().at(3).back().velocity[0] < 1);
    c.clock = carl->clock(t);
    c.velocity = {40, 0, 0};
    c.position[0] += 20; // 0.2 s on from the last believed: within what no car does, past what a Beetle does
    carl->sendCar(c);
    run(100);
    CHECK(host->rejected(3) > once && host->cars().at(3).back().velocity[0] < 1);

    // The host's checks: a car that jumps 500 m in a tenth of a second
    // isn't believed, unless it's been put back; five of those and the
    // player's called a cheater.
    NetCarState s;
    s.position = {0, 0, 0};
    s.rpm = 4200;
    s.throttle = 0.5f;
    s.damage = 0.25f;
    s.dents = 1;
    s.dentAt[0] = {1.2f, 0.4f, -2.0f};
    for (int i = 0; i < 3; ++i) {
        s.clock = bob->clock(t);
        s.position[2] += 2;
        bob->sendCar(s);
        run(100);
    }
    CHECK(host->cars().count(2) && host->rejected(2) == 0);
    const NetCarState &got = host->cars().at(2).back();
    CHECK(std::fabs(got.rpm - 4200) < 1 && std::fabs(got.throttle - 0.5f) < 0.01f &&
          std::fabs(got.damage - 0.25f) < 1e-3f && got.dents == 1 && std::fabs(got.dentAt[0][2] + 2) < 0.03f);
    s.position = {500, 0, 0};
    s.clock = bob->clock(t);
    bob->sendCar(s);
    run(100);
    CHECK(host->rejected(2) >= 1 && host->cars().at(2).back().position[0] < 1);
    s.resets = 1; // put back: believed
    s.clock = bob->clock(t);
    bob->sendCar(s);
    run(100);
    CHECK(std::fabs(host->cars().at(2).back().position[0] - 500) < 1e-3f);
    for (int i = 0; i < 6; ++i) { // and on again, each a jump
        s.clock = bob->clock(t);
        s.position[0] += 500;
        bob->sendCar(s);
        run(100);
    }
    run(300);
    bool cheat = false;
    for (const NetNotice &n : ann->takeNotices())
        cheat = cheat || (n.kind == NetNotice::Kind::kSuspect && n.name == "Bob");
    CHECK(host->player(2)->cheater && ann->player(2)->cheater && cheat);

    // The host leaves: Ann (in longest) takes over, the others follow her,
    // and the race clock goes on.
    const float before = host->clock(t);
    host->leave();
    host.reset();
    run(300);
    bool moved = false;
    for (const NetNotice &n : bob->takeNotices())
        moved = moved || (n.kind == NetNotice::Kind::kHostMoved && n.name == "Ann");
    CHECK(moved && ann->isHost() && !bob->isHost() && !bob->closed() && bob->hostId() == 1 &&
          ann->players().size() == 3);
    CHECK(ann->phase() == NetSession::Phase::kPlaying && ann->clock(t) > before);
    bob->say("still here");
    run(1500);
    CHECK(!ann->chat().empty() && ann->chat().back().text == "still here" && carl->chat().back().text == "still here");
    CHECK(std::fabs(bob->clock(t) - ann->clock(t)) < 0.15f && !carl->closed());
    // Ann goes quiet: Bob and Carl play on under Bob.
    players = {bob.get(), carl.get()};
    run(6000);
    CHECK(bob->isHost() && carl->hostId() == 2 && !carl->closed() && bob->players().size() == 2);
    // And when only one would be left, there's no session.
    players = {carl.get()};
    run(6000);
    CHECK(carl->closed());
}

void testGoldGame() {
    std::vector<Waypoint> points(5);
    points[1].position = {500, 0, 0};
    points[2].position = {0, 0, 500};
    points[3].position = {-500, 0, 0};
    points[4].position = {0, 0, -500};
    NetSettings s;
    s.mode = NetMode::kCopsRobbers;
    s.gold = GoldMode::kCopsAndRobbers;
    s.limit = GoldLimit::kPoints;
    s.limitValue = 2;
    GoldGame g;
    g.start(s, points, 7);
    auto flatDist = [](const Vec3 &a, const Vec3 &b) { return std::hypot(a[0] - b[0], a[2] - b[2]); };
    CHECK(flatDist(g.base(0), g.home()) >= 300 && flatDist(g.base(1), g.home()) >= 300 &&
          flatDist(g.base(0), g.base(1)) >= 300);

    // A takes the gold from its home; B rams A and knocks it loose; A
    // can't take it straight back; B can.
    GoldGame::Car a{1, 0, g.home(), {}};
    GoldGame::Car b{2, 1, g.home() + Vec3{100, 0, 0}, {}};
    float clock = 0;
    auto step = [&](float dt) {
        clock += dt;
        g.step(clock, dt, {a, b}, -100);
    };
    step(0.1f);
    CHECK(g.holder() == 1 && g.eventsAfter(0).size() == 1 && g.eventsAfter(0)[0].kind == GoldEvent::Kind::kGot);
    a.position = g.home() + Vec3{40, 0, 0};
    step(2);
    b.position = a.position + Vec3{3, 0, 0};
    b.velocity = {-10, 0, 0};
    step(0.1f);
    CHECK(g.holder() == kNoPlayer && g.eventsAfter(1).size() == 1 &&
          g.eventsAfter(1)[0].kind == GoldEvent::Kind::kDropped && g.eventsAfter(1)[0].player == 1);
    b.position = a.position + Vec3{30, 0, 0};
    step(0.1f);
    CHECK(g.holder() == kNoPlayer); // A's waiting
    b.position = a.position + Vec3{2, 0, 0};
    b.velocity = {};
    a.position[2] += 20;
    step(0.1f);
    CHECK(g.holder() == 2 && g.eventsAfter(2)[0].kind == GoldEvent::Kind::kRecovered);

    // B delivers it to its base: a point, the gold home again; twice is
    // the limit.
    b.position = g.base(1);
    step(0.1f);
    CHECK(g.score(1) == 1 && g.holder() == kNoPlayer && flatDist(g.goldPosition(), g.home()) < 1e-3f);
    b.position = g.home();
    step(0.1f);
    b.position = g.base(1);
    step(0.1f);
    CHECK(g.score(1) == 2 && g.over() && g.eventsAfter(g.lastEvent() - 1)[0].kind == GoldEvent::Kind::kPointLimit);

    // As the players get it.
    GoldGame copy;
    CHECK(copy.decode(g.encode()) && copy.over() && copy.score(1) == 2 && copy.holder() == kNoPlayer &&
          copy.lastEvent() == g.lastEvent() && copy.teams() && flatDist(copy.base(1), g.base(1)) < 1e-3f);
    CHECK(!copy.decode({1, 2}));

    // Free-for-all, against the clock: each for themselves, one hideout.
    s.gold = GoldMode::kFreeForAll;
    s.limit = GoldLimit::kTime;
    s.limitValue = 1;
    g.start(s, points, 3);
    CHECK(!g.teams() && g.sideOf(a) == 1 && g.sideOf(b) == 2);
    g.step(30, 0.1f, {a, b}, -100);
    CHECK(!g.over());
    g.step(61, 0.1f, {a, b}, -100);
    CHECK(g.over() && g.eventsAfter(g.lastEvent() - 1)[0].kind == GoldEvent::Kind::kTimeUp);

    // A computer player's goal: the gold lying, home with it, after the
    // nearest threat to a teammate who has it, after another side's car
    // that has it.
    {
        NetSettings t2 = s;
        t2.gold = GoldMode::kRobberTeams;
        t2.limit = GoldLimit::kNone;
        GoldGame h;
        h.start(t2, points, 5);
        GoldGame::Car me{1, 0, h.base(1), {}};
        GoldGame::Car mate{2, 0, h.base(1) + Vec3{40, 0, 0}, {}};
        GoldGame::Car them{3, 1, h.home(), {5, 0, 0}};
        bool chasing = true;
        CHECK(flatDist(GoldBot::goal(1, 0, h, {me, mate, them}, chasing), h.home()) < 1e-3f && !chasing);
        h.step(1, 0.1f, {me, mate, them}, -100); // them: on the gold
        CHECK(h.holder() == 3);
        them.position = h.home();
        const Vec3 after = GoldBot::goal(1, 0, h, {me, mate, them}, chasing);
        CHECK(chasing && std::fabs(after[0] - (them.position[0] + 5)) < 1e-3f);
        mate.position = h.home(); // a teammate picks it up instead
        GoldGame h2;
        h2.start(t2, points, 5);
        h2.step(1, 0.1f, {mate}, -100);
        CHECK(h2.holder() == 2 && flatDist(GoldBot::goal(1, 0, h2, {me, mate}, chasing), h2.base(0)) < 1e-3f &&
              !chasing); // nobody to stop: home ahead of it
        them.position = mate.position + Vec3{30, 0, 0};
        them.velocity = {};
        CHECK(flatDist(GoldBot::goal(1, 0, h2, {me, mate, them}, chasing), them.position) < 1e-3f &&
              chasing); // after whoever's nearest it
        me.position = h2.home();
        GoldGame h3;
        h3.start(t2, points, 5);
        h3.step(1, 0.1f, {me}, -100);
        CHECK(h3.holder() == 1 && flatDist(GoldBot::goal(1, 0, h3, {me}, chasing), h3.base(0)) < 1e-3f);
    }

    // The announcer's lines.
    const auto lines = parseSpeechTable("Name prefix/type header,end sufix value,sufix add value\r\n"
                                        "BLUETEAMHASGOLD header,,\r\nAS1\\AS1ROBROB ,10,9\r\n"
                                        "ROBGETLOOT header,,,\r\nAL1\\AL1COPS,3,0,3\r\n");
    CHECK(lines.size() == 2 && lines.at("BLUETEAMHASGOLD") == std::vector<std::string>({"AS1/AS1ROBROB10"}));
    CHECK(lines.at("ROBGETLOOT") == std::vector<std::string>({"AL1/AL1COPS01", "AL1/AL1COPS02", "AL1/AL1COPS03"}));
}

} // namespace

// The host's computer players outlive it: the new host drives them on,
// and one player left with them plays on.
void testNetBotsOutliveHost() {
    auto network = net::LoopbackNetwork::create();
    NetSettings settings;
    settings.mode = NetMode::kCruise;
    auto host = NetSession::host(network->open(kNetPort), "Hosting", settings);
    const uint8_t bot = host->addBot("Opponent 1", "vpbug");
    const net::Address at{0x7f000001, kNetPort};
    std::vector<NetSession *> players;
    uint64_t t = 1000;
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 10; ++i) {
            t += 10;
            if (host)
                host->update(t);
            for (NetSession *p : players)
                p->update(t);
        }
    };
    auto ann = NetSession::join(network->open(0), at, "Ann");
    auto bob = NetSession::join(network->open(0), at, "Bob");
    players = {ann.get(), bob.get()};
    run(600);
    ann->setReady(true);
    bob->setReady(true);
    run(500);
    CHECK(!host->start(t));
    run(300);
    host->setLoaded();
    ann->setLoaded();
    bob->setLoaded();
    run(3500);
    CHECK(host->phase() == NetSession::Phase::kPlaying && ann->player(bot) && ann->player(bot)->bot);

    // The host goes: Ann takes over with the computer player, and its car
    // reaches Bob from her now.
    host->leave();
    host.reset();
    run(300);
    CHECK(ann->isHost() && ann->player(bot) && bob->player(bot) && ann->players().size() == 3);
    NetCarState s;
    s.position = {10, 0, 20};
    s.clock = ann->clock(t);
    ann->sendBotCar(bot, s);
    run(200);
    CHECK(bob->cars().count(bot) && std::fabs(bob->cars().at(bot).back().position[2] - 20) < 1e-3f);

    // Bob goes too: Ann plays on alone with it.
    bob->leave();
    players = {ann.get()};
    run(300);
    CHECK(!ann->closed() && ann->isHost() && ann->players().size() == 2 && ann->player(bot));

    // Without computer players, one left alone ends the session, as before.
    auto host2 = NetSession::host(network->open(kNetPort + 1), "Alone", settings);
    auto cal = NetSession::join(network->open(0), {0x7f000001, uint16_t(kNetPort + 1)}, "Cal");
    for (int i = 0; i < 60; ++i) {
        t += 10;
        host2->update(t);
        cal->update(t);
    }
    host2->leave();
    for (int i = 0; i < 30; ++i) {
        t += 10;
        cal->update(t);
    }
    CHECK(cal->closed());
}

static const reluva::test::Suite
    kSuite("age.unit.network",
           {
               {"net session", [](const std::filesystem::path &) { testNetSession(); }},
               {"net session more", [](const std::filesystem::path &) { testNetSessionMore(); }},
               {"bots outlive the host", [](const std::filesystem::path &) { testNetBotsOutliveHost(); }},
               {"gold game", [](const std::filesystem::path &) { testGoldGame(); }},
           });
