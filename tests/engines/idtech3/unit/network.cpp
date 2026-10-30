// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: Network play: messages and deltas, pure checks, the server browser, downloads, snapshots, a game played over the network.

#include "common/fs.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/server_browser.h"
#include "backends/sockets/socket_net.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/pages/page.h"
#include "common/audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

// Downloads: a pak the level uses (here the one with its map, not one of
// Raven's) sent to a player who asks, block by block, when
// sv_allowDownload is on; refused when it's off. A pak holding only the
// server's game code isn't one the level uses, as the original counts.
static void testDownloads(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    // Over 48 KB, so the window and its acknowledgements matter.
    std::vector<uint8_t> filler(70000);
    for (size_t i = 0; i < filler.size(); ++i)
        filler[i] = uint8_t(i * 7 + i / 300);
    test::writeFile(tmp / "BaseEF" / "zz_custom.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}},
                                    {"maps/borg2.bsp", {1, 2, 3, 4}, {}},
                                    {"filler.dat", filler, {}}}));
    test::writeFile(tmp / "BaseEF" / "zz_code.pk3", test::buildZip({{"vm/qagame.qvm", {5, 6, 7}, {}}}));
    std::vector<uint8_t> pakBytes;
    {
        std::ifstream in(tmp / "BaseEF" / "zz_custom.pk3", std::ios::binary);
        pakBytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    auto map = collisionMap();
    map->entities = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n"
                    "\"origin\" \"0 0 300\"\n}\n";
    if (game.startLevel("borg2", map, 5)) {
        CHECK(false);
        return;
    }
    auto network = net::LoopbackNetwork::create();
    ef::NetServer server(game, cvars, network->open(27960));
    server.setFiles(&set);
    server.levelStarted();
    CHECK(cvars.string("sv_referencedPakNames") == "BaseEF/zz_custom ");
    uint64_t now = 1000;
    auto tick = [&](ef::NetClient& client) {
        client.frame(now);
        CHECK(!server.receive(now));
        CHECK(!game.runFrame());
        CHECK(!server.frame(now));
        now += 50;
    };
    // Refused while downloads are off.
    {
        ef::NetClient client(network->open(0), {0x7f000001u, 27960}, "\\name\\One", 81);
        for (int i = 0; i < 40 && client.state() != ef::NetClient::State::Primed; ++i)
            tick(client);
        client.download("BaseEF/zz_custom.pk3");
        for (int i = 0; i < 20 && client.state() != ef::NetClient::State::Disconnected; ++i)
            tick(client);
        CHECK(client.state() == ef::NetClient::State::Disconnected);
        client.disconnect();
        for (int i = 0; i < 4; ++i)
            tick(client);
    }
    // Sent when they're on, and a Raven pak never.
    cvars.set("sv_allowDownload", "1");
    // (A port of its own: the first's is still the server's to send to.)
    ef::NetClient client(network->open(30000), {0x7f000001u, 27960}, "\\name\\Two", 82);
    for (int i = 0; i < 40 && client.state() != ef::NetClient::State::Primed; ++i)
        tick(client);
    CHECK(client.state() == ef::NetClient::State::Primed && client.takeNewGamestate());
    client.download("BaseEF/zz_custom.pk3");
    std::string name;
    std::vector<uint8_t> data;
    bool done = false;
    for (int i = 0; i < 400 && !done; ++i) {
        tick(client);
        done = client.takeDownload(name, data);
    }
    CHECK(done && name == "BaseEF/zz_custom.pk3" && data == pakBytes);
    // Done: the game state again.
    client.addClientCommand("donedl");
    bool again = false;
    for (int i = 0; i < 40 && !again; ++i) { // the client sends a packet a second while loading
        tick(client);
        again = client.takeNewGamestate();
    }
    CHECK(again);
    std::filesystem::remove_all(tmp);
}

// A pure server (sv_pure 1): it lists its paks, holds a player's moves
// until its paks are checked (cp), lets in one that answers rightly and
// drops one that doesn't.
static void testPureServer(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}},
                                    {"vm/cgame.qvm", {1, 2, 3}, {}},
                                    {"vm/ui.qvm", {4, 5, 6}, {}}}));
    test::writeFile(tmp / "BaseEF" / "pak1.pk3", test::buildZip({{"maps/borg2.bsp", {7, 8, 9}, {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    auto map = collisionMap();
    map->entities = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n"
                    "\"origin\" \"0 0 300\"\n}\n";
    if (game.startLevel("borg2", map, 9)) {
        CHECK(false);
        return;
    }
    cvars.set("sv_pure", "1");
    auto network = net::LoopbackNetwork::create();
    ef::NetServer server(game, cvars, network->open(27960));
    server.setFiles(&set);
    server.levelStarted();
    CHECK(!game.runFrame()); // the system info again, with the pak lists
    const std::string sysInfo = game.configstrings().at(1);
    std::istringstream listed(infoValue(sysInfo, "sv_paks"));
    int paks = 0;
    for (std::string w; listed >> w;)
        ++paks;
    CHECK(paks == 2 && infoValue(sysInfo, "sv_pakNames").find("BaseEF/pak1") != std::string::npos);
    uint64_t now = 1000;
    auto tick = [&](ef::NetClient& client) {
        client.frame(now);
        CHECK(!server.receive(now));
        CHECK(!game.runFrame());
        CHECK(!server.frame(now));
        now += 50;
    };
    auto play = [&](ef::NetClient& client, int frames) {
        for (int i = 0; i < frames; ++i) {
            ef::GameHost::UserCommand cmd;
            cmd.serverTime = game.levelTime();
            client.addCommand(cmd);
            tick(client);
            client.takeSnapshots();
        }
    };
    // In the level: the fake game links the player's body when the
    // server begins it.
    const size_t linkedAtStart = game.linked().size();
    auto inLevel = [&] { return game.linked().size() > linkedAtStart; };
    ef::NetClient good(network->open(0), {0x7f000001u, 27960}, "\\name\\Good", 71);
    for (int i = 0; i < 40 && good.state() != ef::NetClient::State::Primed; ++i)
        tick(good);
    CHECK(good.state() == ef::NetClient::State::Primed);
    good.setLoaded();
    play(good, 20);
    CHECK(!inLevel()); // no answer, not in the level
    const std::string paksSent = infoValue(good.configstrings().at(1), "sv_paks");
    good.addClientCommand(pureCommand(set, good.checksumFeed(), paksSent));
    for (int i = 0; i < 40 && !inLevel(); ++i)
        play(good, 1);
    CHECK(inLevel());

    ef::NetClient bad(network->open(30001), {0x7f000001u, 27960}, "\\name\\Bad", 72);
    for (int i = 0; i < 40 && bad.state() != ef::NetClient::State::Primed; ++i)
        tick(bad);
    bad.addClientCommand("cp 1 2 @ 3 4");
    bad.setLoaded();
    for (int i = 0; i < 40 && bad.state() != ef::NetClient::State::Disconnected; ++i)
        play(bad, 1);
    CHECK(bad.state() == ef::NetClient::State::Disconnected);
    CHECK(server.pure());
    std::filesystem::remove_all(tmp);
}

// What a snapshot holds: the fake game's level on a map cut in two at
// x = 0, each half its own cluster and area; the player on the +x side.
static void testSnapshotCulling(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    auto map = collisionMap();
    BspPlane cut;
    cut.normal = {1, 0, 0};
    cut.dist = 0;
    map->planes.push_back(cut);
    map->nodes[0].plane = int(map->planes.size()) - 1; // front (+x) leaf 0, back leaf 1
    map->leafs[1] = map->leafs[0];
    map->leafs[0].cluster = 0;
    map->leafs[0].area = 0;
    map->leafs[1].cluster = 1;
    map->leafs[1].area = 1;
    map->visClusters = 2;
    map->visClusterBytes = 1;
    map->visData = {0x1, 0x2}; // each half sees only itself
    map->entities = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                    "\"origin\" \"200 0 0\"\n}\n{\n\"classname\" \"item_health\"\n\"origin\" \"-200 0 16\"\n}\n"
                    "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"0 0 300\"\n}\n";
    std::string refused;
    if (game.startLevel("borg2", map) || game.connectClient(0, "\\name\\Reluva", refused) || game.beginClient(0)) {
        CHECK(false);
        return;
    }
    // The player's body moved to the +x half (the eye follows it).
    const int32_t body = game.gameData().entities + 4 * game.gameData().entitySize;
    game.vm().writeInt(body + 272, Qvm::fromFloat(300));
    auto numbers = [&] {
        std::set<int32_t> out;
        for (const auto& e : game.snapshot(0).entities)
            out.insert(int32_t(e[0]));
        return out;
    };
    // The door (1) on its side, the spawn point (3) on the line, the
    // body (4: the stand-in's player isn't entity 0); not the item (2) in
    // the half it can't see.
    CHECK((numbers() == std::set<int32_t>{1, 3, 4}));
    // Seeing each other but in two areas no open portal joins: still not.
    map->visData = {0x3, 0x3};
    game.runFrame();
    CHECK((numbers() == std::set<int32_t>{1, 3, 4}));
    // With every area counted as joined (cm_noAreas), everything.
    cvars.set("cm_noAreas", "1");
    game.runFrame();
    CHECK((numbers() == std::set<int32_t>{1, 2, 3, 4}));
    std::filesystem::remove_all(tmp);
}

// A player joining over the network: the fake game's server, and a
// client talking to it through sockets in memory (some datagrams lost).
static void testNetworkPlay(const std::filesystem::path& tmp, bool systemSockets, bool limitRate = false) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    auto map = collisionMap();
    map->entities = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                    "\"origin\" \"200 0 0\"\n}\n{\n\"classname\" \"item_health\"\n\"origin\" \"-200 0 16\"\n}\n"
                    "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"0 0 300\"\n}\n";
    if (game.startLevel("borg2", map, 1234)) {
        CHECK(false);
        return;
    }
    // In memory, or through the system's own sockets on this machine.
    auto network = net::LoopbackNetwork::create();
    // Players on the same machine are held to a rate only if sv_lanForceRate is 0.
    cvars.create("sv_lanForceRate", "1", 0);
    cvars.set("sv_lanForceRate", limitRate ? "0" : "1");
    std::string socketError;
    std::unique_ptr<net::UdpSocket> serverSocket =
        systemSockets ? net::openSystemUdp(0, socketError) : network->open(27960);
    std::unique_ptr<net::UdpSocket> clientSocket = systemSockets ? net::openSystemUdp(0, socketError) : network->open(0);
    if (!serverSocket || !clientSocket) {
        std::printf("(network play over system sockets not tested: %s)\n", socketError.c_str());
        return;
    }
    const uint16_t serverPort = serverSocket->port();
    auto serverOwned = std::make_unique<ef::NetServer>(game, cvars, std::move(serverSocket));
    ef::NetServer& server = *serverOwned;
    server.levelStarted();
    ef::NetClient client(std::move(clientSocket), {0x7f000001u, serverPort}, "\\name\\Remote\\snaps\\20", 77);
    uint64_t now = 1000;
    auto tick = [&] {
        if (systemSockets)
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); // for datagrams to arrive
        client.frame(now);
        CHECK(!server.receive(now));
        CHECK(!game.runFrame());
        CHECK(!server.frame(now));
        now += 50;
    };
    for (int i = 0; i < 40 && client.state() != ef::NetClient::State::Primed; ++i)
        tick();
    CHECK(client.state() == ef::NetClient::State::Primed && client.takeNewGamestate());
    CHECK(client.configstrings().count(12) && client.configstrings().at(12) == "borg2");
    CHECK(client.configstrings().at(0).find("\\protocol\\24") != std::string::npos);
    CHECK(server.players().size() == 1 && server.players()[0].second == "Remote");
    // Loaded: its moves put it in the level and move it along.
    client.setLoaded();
    // From now on every fourth datagram each way is lost (in memory).
    network->setDropFilter([](const net::Address&, const net::Address&, size_t index) { return index % 4 == 3; });
    std::vector<ef::GameHost::Snapshot> got;
    for (int i = 0; i < 60; ++i) {
        ef::GameHost::UserCommand cmd;
        cmd.serverTime = game.levelTime();
        cmd.forward = 127;
        client.addCommand(cmd);
        tick();
        for (auto& snap : client.takeSnapshots())
            got.push_back(std::move(snap));
    }
    CHECK(client.state() == ef::NetClient::State::Active && got.size() > 20);
    CHECK(game.linked().count(4) && game.entityVec(4, 272)[0] > 100); // moved by its commands
    // What it has is what the server has (the door's state, every byte).
    if (!got.empty()) {
        const auto& last = got.back();
        bool foundDoor = false;
        for (const auto& e : last.entities)
            if (e[0] == 1)
                foundDoor = e == game.entityState(1);
        CHECK(foundDoor);
        CHECK(last.serverTime <= game.levelTime() && last.serverTime > game.levelTime() - 500);
    }
    // The game's announcement reached it.
    bool announced = false;
    for (const std::string& c : client.serverCommands())
        announced = announced || c.find("entered the game") != std::string::npos;
    CHECK(announced);
    // Snapshots while loading are flagged not active; in the game, not.
    CHECK(!got.empty() && (got.back().snapFlags & 2) == 0);
    // At 3000 bytes a second (no rate given), some snapshots are held back
    // and the next flagged so; unlimited, none.
    bool delayed = false;
    for (const auto& snap : got)
        delayed = delayed || (snap.snapFlags & 1);
    CHECK(delayed == limitRate);
    if (!systemSockets) {
        network->setDropFilter(nullptr);
        auto play = [&](int ticks) {
            for (int i = 0; i < ticks; ++i) {
                ef::GameHost::UserCommand cmd;
                cmd.serverTime = game.levelTime();
                cmd.forward = 127;
                client.addCommand(cmd);
                tick();
                for (auto& snap : client.takeSnapshots())
                    got.push_back(std::move(snap));
            }
        };
        // map_restart: the game starts over in place, the player is told
        // and is back in the level; the snapshots' level flag changes.
        const int32_t flagBefore = got.back().snapFlags & 4;
        bool handled = false;
        CHECK(!game.serverCommand("map_restart 0", &handled) && handled && game.restarts() == 1);
        CHECK(game.linked().count(4)); // the player back at the spawn point
        got.clear();
        play(10);
        bool restartSent = false;
        for (const std::string& c : client.serverCommands())
            restartSent = restartSent || c == "map_restart";
        CHECK(restartSent && client.state() == ef::NetClient::State::Active && !got.empty() &&
              (got.back().snapFlags & 4) != flagBefore);
        // Its moves still count: it has the level's new id.
        CHECK(client.configstrings().count(1) &&
              q3::infoValue(client.configstrings().at(1), "sv_serverid") == cvars.string("sv_serverid"));
        CHECK(game.entityVec(4, 272)[0] > 100);
        // A delayed restart: the warmup configstring says when, then it happens.
        CHECK(!game.serverCommand("map_restart 1") && game.restarts() == 1);
        CHECK(game.configstrings().count(5) && std::atoi(game.configstrings().at(5).c_str()) > game.levelTime());
        play(25);
        CHECK(game.restarts() == 2);
        // map: a new level asked for, loaded by whoever runs the server;
        // the player is sent its game state and joins it.
        CHECK(!game.serverCommand("map borg2") && game.levelRequested());
        std::vector<int32_t> players;
        CHECK(!game.changeLevel(game.takeRequestedMap(), map, 99, players) && players == std::vector<int32_t>{0});
        server.levelStarted();
        bool newLevel = false;
        for (int i = 0; i < 20 && !newLevel; ++i) {
            tick();
            newLevel = client.takeNewGamestate();
        }
        CHECK(newLevel && client.configstrings().count(12) && client.configstrings().at(12) == "borg2");
        client.setLoaded();
        got.clear();
        play(10);
        CHECK(!got.empty() && game.linked().count(4) && client.state() == ef::NetClient::State::Active);
        // A public server (dedicated 2) tells the masters it's there.
        auto master = network->open(27953);
        cvars.set("sv_master1", "127.0.0.1");
        cvars.set("sv_master2", "");
        cvars.create("dedicated", "0", 0);
        cvars.set("dedicated", "2", true); // (latched: forced, as at a new level)
        server.resolveHost = [](const std::string& host, uint32_t& ip) {
            net::Address a;
            if (!net::parseAddress(host, 0, a))
                return false;
            ip = a.ip;
            return true;
        };
        CHECK(!server.frame(now));
        net::Address from;
        std::vector<uint8_t> beat;
        CHECK(master->receive(from, beat) &&
              std::string(beat.begin() + 4, beat.end()) == "\\heartbeat\\27960\\gamename\\STEF1\\");
        CHECK(!server.frame(now + 1000) && !master->receive(from, beat)); // not again for 5 minutes
        cvars.set("dedicated", "0");
    }
    // Leaving.
    network->setDropFilter(nullptr);
    client.disconnect();
    for (int i = 0; i < 4; ++i)
        tick();
    CHECK(server.players().empty() && game.clientSlotFree(0));
    // Another joins, and the server shuts down: it's told so.
    if (!systemSockets) {
        ef::NetClient second(network->open(0), {0x7f000001u, serverPort}, "\\name\\Second", 78);
        for (int i = 0; i < 20 && second.state() != ef::NetClient::State::Primed; ++i) {
            second.frame(now);
            CHECK(!server.receive(now));
            CHECK(!game.runFrame());
            CHECK(!server.frame(now));
            now += 50;
        }
        CHECK(second.state() == ef::NetClient::State::Primed);
        serverOwned.reset();
        second.frame(now);
        CHECK(second.state() == ef::NetClient::State::Disconnected && second.message() == "Server shut down");
    }
    std::filesystem::remove_all(tmp);
}

static void testNetMessages() {
    using reluva::idtech3::wire::MessageReader;
    using reluva::idtech3::wire::MessageWriter;
    // Bits fill each byte from the bottom, values lowest bit first.
    MessageWriter w;
    w.bits(5, 3);
    w.bits(0x1ff, 9);
    CHECK(w.bytes() == std::vector<uint8_t>({0xfd, 0x0f}));
    // A string inside a byte starts at the byte after the next.
    w.string("hi");
    CHECK(w.bytes().size() == 6 && w.bytes()[3] == 'h' && w.bytes()[5] == 0);
    w.longValue(-2);
    w.smallFloat(-12);
    w.smallFloat(0.5f);
    w.shortValue(-300);
    MessageReader r(w.bytes());
    CHECK(r.bits(3) == 5 && r.bits(9) == 0x1ff && r.string() == "hi" && r.longValue() == -2);
    CHECK(r.smallFloat() == -12 && r.smallFloat() == 0.5f && r.shortValue() == -300 && !r.overflowed());
    CHECK(r.bits(8) == 0 && r.byte() == -1 && r.overflowed());
    // Small floats: a whole number in 14 bits, anything else in 33.
    MessageWriter f;
    f.smallFloat(100);
    CHECK(f.bitCount() == 14);
    f.smallFloat(5000);
    CHECK(f.bitCount() == 14 + 33);

    // The netchan: scrambling undone, pieces joined, old ones dropped.
    std::vector<uint8_t> d;
    for (int i = 0; i < 40; ++i)
        d.push_back(uint8_t(i * 7));
    std::vector<uint8_t> scrambled = d;
    reluva::idtech3::wire::Netchan::scramble(scrambled);
    CHECK(scrambled != d && std::equal(d.begin(), d.begin() + 6, scrambled.begin()));
    reluva::idtech3::wire::Netchan::unscramble(scrambled);
    CHECK(scrambled == d);
    reluva::idtech3::wire::Netchan client(true, 1234), server(false, 0);
    std::vector<uint8_t> big(2700);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = uint8_t(i * 31);
    const auto pieces = client.transmit(big);
    CHECK(pieces.size() == 3 && pieces[0].size() == 10 + 1300);
    uint16_t qport = 0;
    CHECK(reluva::idtech3::wire::Netchan::clientQport(pieces[0], qport) && qport == 1234);
    std::vector<uint8_t> got;
    CHECK(!server.process(pieces[0], got) && !server.process(pieces[1], got) && server.process(pieces[2], got));
    CHECK(got == big && server.incomingSequence() == 1);
    const auto small = client.transmit({1, 2, 3});
    CHECK(small.size() == 1 && server.process(small[0], got) && got == std::vector<uint8_t>({1, 2, 3}));
    CHECK(!server.process(small[0], got)); // a repeat
    const auto reply = server.transmit({9, 8, 7, 6, 5});
    CHECK(reply[0].size() == 4 + 5 && client.process(reply[0], got) && got == std::vector<uint8_t>({9, 8, 7, 6, 5}));
}

static void testNetDeltas() {
    using namespace reluva::idtech3::eliteforce;
    using reluva::idtech3::wire::MessageReader;
    using reluva::idtech3::wire::MessageWriter;
    auto put = [](std::vector<uint8_t>& s, int32_t at, int32_t v) { std::memcpy(s.data() + at, &v, 4); };
    auto putF = [&](std::vector<uint8_t>& s, int32_t at, float v) {
        int32_t i;
        std::memcpy(&i, &v, 4);
        put(s, at, i);
    };
    std::vector<uint8_t> a(kNetEntityStateSize, 0), b;
    put(a, 0, 17);
    put(a, 4, 1);
    putF(a, 24, 100.25f);
    putF(a, 28, -64);
    put(a, 176, 0xffffff);
    b = a;
    putF(b, 24, 101);
    putF(b, 28, -60);
    // Moving on x and y: predefined vector 2, so 10 + 1 + 1 + 5 bits and the values.
    MessageWriter w;
    writeEntityDelta(w, a, b, false);
    CHECK(w.bitCount() == 17 + 14 + 14);
    writeEntityDelta(w, a, a, false); // unchanged: nothing
    CHECK(w.bitCount() == 17 + 28);
    std::vector<uint8_t> zero;
    writeEntityDelta(w, zero, a, true); // a baseline: an explicit vector
    writeEntityRemoved(w, 17);
    MessageReader r(w.bytes());
    std::vector<uint8_t> out;
    CHECK(r.bits(10) == 17 && !readEntityDelta(r, 17, a, out) && out == b);
    CHECK(r.bits(10) == 17 && !readEntityDelta(r, 17, zero, out) && out == a);
    CHECK(r.bits(10) == 17 && readEntityDelta(r, 17, a, out) && out.empty());

    std::vector<uint8_t> p(kNetPlayerStateSize, 0), q;
    put(p, 0, 123456);
    putF(p, 20, 12.5f);
    put(p, 16, -5);    // pm_time, signed
    put(p, 180, -10);  // viewheight, signed
    put(p, 204, -3);   // stats[0]
    put(p, 396 + 8, 40); // ammo[2]
    put(p, 332 + 4, 99999); // powerups[1]
    put(p, 56, 777);   // introTime, last
    put(p, 48, 200);   // not sent
    MessageWriter pw;
    writePlayerDelta(pw, {}, p);
    CHECK(pw.bitCount() > 48 + 4);
    MessageReader pr(pw.bytes());
    readPlayerDelta(pr, {}, q);
    put(p, 48, 0);
    CHECK(q == p && !pr.overflowed());

    GameHost::UserCommand c0, c1;
    c1.serverTime = 5000;
    c1.angles = {-1000, 16384, 0};
    c1.forward = 127;
    c1.right = -127;
    c1.buttons = 1 | 32;
    c1.weapon = 4;
    GameHost::UserCommand c2 = c1;
    c2.serverTime = 5016;
    c2.forward = -128;
    MessageWriter cw;
    writeUserCommand(cw, c0, c1);
    writeUserCommand(cw, c1, c2);
    MessageReader cr(cw.bytes());
    const GameHost::UserCommand r1 = readUserCommand(cr, c0), r2 = readUserCommand(cr, r1);
    CHECK(r1.serverTime == 5000 && r1.angles[0] == (-1000 & 0xffff) && r1.angles[1] == 16384 && r1.forward == 127 &&
          r1.right == -127 && r1.buttons == 33 && r1.weapon == 4);
    CHECK(r2.serverTime == 5016 && r2.forward == -127 && r2.right == -127);
}

static void testPureCheck(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    // Two paks: the game code in one, a map in the other; an empty file
    // doesn't count towards a checksum.
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/cgame.qvm", {1, 2, 3}, {}}, {"vm/ui.qvm", {4, 5}, {}}, {"empty.txt", {}, {}}}));
    test::writeFile(tmp / "BaseEF" / "pak1.pk3", test::buildZip({{"maps/x.bsp", {9, 9, 9, 9}, {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    std::vector<const ZipArchive*> paks;
    for (const Archive* a : set.archives())
        if (const auto* z = dynamic_cast<const ZipArchive*>(a))
            paks.push_back(z);
    CHECK(paks.size() == 2);
    if (paks.size() != 2)
        return;
    const ZipArchive* code = paks[0]->has("vm/cgame.qvm") ? paks[0] : paks[1];
    const ZipArchive* map = code == paks[0] ? paks[1] : paks[0];
    // The checksum: MD4 of the CRCs, folded; the pure one with the feed first.
    std::vector<uint8_t> words;
    for (const auto& e : map->entries())
        if (e.size > 0)
            for (int i = 0; i < 4; ++i)
                words.push_back(uint8_t(e.crc >> (8 * i)));
    const auto d = md4(words.data(), words.size());
    uint32_t folded = 0;
    for (int w = 0; w < 4; ++w)
        folded ^= uint32_t(d[size_t(4 * w)]) | uint32_t(d[size_t(4 * w + 1)]) << 8 |
                  uint32_t(d[size_t(4 * w + 2)]) << 16 | uint32_t(d[size_t(4 * w + 3)]) << 24;
    CHECK(uint32_t(pakChecksum(*map)) == folded);
    CHECK(pakChecksum(*map) != pakChecksum(*map, 77) && pakChecksum(*code) != pakChecksum(*map));
    // cp: the client game's and menus' pak, @, the paks, and the check.
    const int32_t feed = 12345;
    const std::string cp = pureCommand(set, feed, "");
    const std::vector<std::string> w = splitCommand(cp);
    CHECK(w.size() == 7 && w[0] == "cp" && w[3] == "@");
    if (w.size() == 7) {
        CHECK(std::atoi(w[1].c_str()) == pakChecksum(*code, feed) && w[1] == w[2]);
        const int32_t a = std::atoi(w[4].c_str()), b = std::atoi(w[5].c_str());
        CHECK(std::atoi(w[6].c_str()) == (feed ^ a ^ b ^ 2));
    }
    // Only the paks the server says it has.
    const std::string only = pureCommand(set, feed, std::to_string(uint32_t(pakChecksum(*code))));
    const std::vector<std::string> o = splitCommand(only);
    CHECK(o.size() == 6 && std::atoi(o[5].c_str()) == (feed ^ pakChecksum(*code, feed) ^ 1));

    // A pure server takes that answer: the right client game and menus,
    // paks it has, the right check; with a level id first too.
    const int32_t cg = pakChecksum(*code, feed);
    const std::set<int32_t> serverPaks{pakChecksum(*code, feed), pakChecksum(*map, feed)};
    CHECK(pureAnswerValid(w, cg, cg, serverPaks, feed));
    CHECK(pureAnswerValid(o, cg, cg, serverPaks, feed));
    std::vector<std::string> withId = w;
    withId.insert(withId.begin() + 1, "42");
    CHECK(pureAnswerValid(withId, cg, cg, serverPaks, feed));
    // Not: another feed, another client game, a pak it lacks, one twice,
    // a wrong check, too short.
    CHECK(!pureAnswerValid(w, cg, cg, serverPaks, feed + 1));
    CHECK(!pureAnswerValid(w, cg + 1, cg, serverPaks, feed));
    CHECK(!pureAnswerValid(w, cg, cg, {pakChecksum(*code, feed)}, feed));
    if (w.size() == 7) {
        std::vector<std::string> twice = w;
        twice[5] = twice[4];
        twice[6] = std::to_string(feed ^ 2);
        CHECK(!pureAnswerValid(twice, cg, cg, serverPaks, feed));
        std::vector<std::string> wrong = w;
        wrong[6] = std::to_string(std::atoi(w[6].c_str()) ^ 1);
        CHECK(!pureAnswerValid(wrong, cg, cg, serverPaks, feed));
    }
    CHECK(!pureAnswerValid({"cp", "1", "2", "@"}, 1, 2, serverPaks, feed));

    // What counts as using a pak: not the files read from every pak, nor
    // the server's game code.
    CHECK(referencesPak("maps/hm_borg1.bsp") && referencesPak("botfiles/chars.h") &&
          referencesPak("vm/cgame.qvm"));
    CHECK(!referencesPak("scripts/borg.shader") && !referencesPak("scripts/arenas.txt") &&
          !referencesPak("botfiles/bots/munro.bot") && !referencesPak("Scripts/X.ARENA") &&
          !referencesPak("vm/qagame.qvm") && !referencesPak("levelshots/hm_borg1.jpg"));
    std::filesystem::remove_all(tmp);
}

static void testServerBrowser() {
    using reluva::idtech3::ServerBrowser;
    auto network = net::LoopbackNetwork::create();
    auto server = network->open(27961);
    ServerBrowser browser(network->open(0));
    browser.localServers(500);
    net::Address from;
    std::vector<uint8_t> data;
    int asked = 0;
    while (server->receive(from, data))
        asked += std::string(data.begin() + 4, data.end()) == "getinfo xxx";
    CHECK(asked == 2); // twice on its port
    auto reply = [&](const std::string& text) {
        std::vector<uint8_t> d(4, 0xff);
        d.insert(d.end(), text.begin(), text.end());
        server->send(from, d);
    };
    reply("infoResponse \"\\hostname\\Test\\mapname\\hm_borg1\"");
    browser.update(1000);
    CHECK(browser.localServerCount() == 1 && browser.localServerAddress(0) == "127.0.0.1:27961");
    // A ping: waiting, then answered.
    browser.ping({0x7f000001u, 27961}, 2000);
    std::string address;
    int time = -1;
    browser.getPing(0, 2010, 800, address, time);
    CHECK(browser.pingQueueCount() == 1 && address == "127.0.0.1:27961" && time == 0);
    reply("infoResponse \"\\hostname\\Test\"");
    browser.update(2042);
    browser.getPing(0, 2050, 800, address, time);
    CHECK(time == 42 && browser.pingInfo(0) == "\\hostname\\Test");
    browser.clearPing(0);
    CHECK(browser.pingQueueCount() == 0);
    // Not answered: given up on after cl_maxPing.
    browser.ping({0x7f000001u, 27999}, 3000);
    browser.getPing(0, 3900, 800, address, time);
    CHECK(time == 900);
    // The Internet list: a master asked, its answer (Elite Force's: each
    // address in hexadecimal) taken.
    auto master = network->open(27953);
    browser.globalServers({{0x7f000001u, 27953}}, 24, "");
    CHECK(browser.globalServerCount() == -1); // waiting for an answer
    CHECK(master->receive(from, data) && std::string(data.begin() + 4, data.end()) == "getservers 24 full empty");
    const std::string list = "getserversResponse\\C0A80105" "6D38\\0A000002" "6D39\\EOT";
    std::vector<uint8_t> answer(4, 0xff);
    answer.insert(answer.end(), list.begin(), list.end());
    master->send(from, answer);
    browser.update(5000);
    CHECK(browser.globalServerCount() == 2 && browser.globalServerAddress(0) == "192.168.1.5:27960" &&
          browser.globalServerAddress(1) == "10.0.0.2:27961");
    // Quake III's form, on the next line.
    std::vector<uint8_t> q3(4, 0xff);
    const std::string text = "infoResponse\n\\hostname\\Q3\n";
    q3.insert(q3.end(), text.begin(), text.end());
    CHECK(ServerBrowser::infoFromResponse(q3) == "\\hostname\\Q3");
}

static const reluva::test::Suite suite("idtech3.unit.network", {
    {"messages", [](const std::filesystem::path&) { testNetMessages(); }},
    {"deltas", [](const std::filesystem::path&) { testNetDeltas(); }},
    {"pure check", [](const std::filesystem::path& tmp) { testPureCheck(tmp / "pure"); }},
    {"server browser", [](const std::filesystem::path&) { testServerBrowser(); }},
    {"downloads", [](const std::filesystem::path& tmp) { testDownloads(tmp / "downloads"); }},
    {"pure server", [](const std::filesystem::path& tmp) { testPureServer(tmp / "pure"); }},
    {"snapshot culling", [](const std::filesystem::path& tmp) { testSnapshotCulling(tmp / "culling"); }},
    {"network play", [](const std::filesystem::path& tmp) { testNetworkPlay(tmp / "netplay", false); }},
    {"network play over sockets", [](const std::filesystem::path& tmp) { testNetworkPlay(tmp / "netplay-udp", true); }},
    {"network play at a rate", [](const std::filesystem::path& tmp) { testNetworkPlay(tmp / "netplay-rate", false, true); }},
});
