// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for network play.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "backends/sockets/socket_net.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/server_browser.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

// Network play from a terminal (protocol 24, eliteforce-network.md):
// hosting a level as a server with no player of its own, for others to
// join for `seconds`; or, with -join, joining a server and reporting
// what it sends while walking forward.
int cmdNetRun(int argc, char** argv) {
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool net-run <game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot <name>]...\n"
                             "       reluva-tool net-run <game folder> -join <host[:port]> [seconds] [-name <name>]\n");
        return 1;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const bool join = std::strcmp(argv[3], "-join") == 0;
    if (join && argc < 5) {
        std::fprintf(stderr, "net-run: -join needs an address\n");
        return 1;
    }
    int seconds = 60, port = 27960;
    std::string name = "ReluvaTool";
    std::vector<std::string> bots;
    bool publicServer = false; // dedicated 2: listed with the master servers
    bool pure = false;         // sv_pure 1: players' paks checked
    for (int i = join ? 5 : 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-port") == 0 && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-name") == 0 && i + 1 < argc)
            name = argv[++i];
        else if (std::strcmp(argv[i], "-addbot") == 0 && i + 1 < argc)
            bots.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-public") == 0)
            publicServer = true;
        else if (std::strcmp(argv[i], "-pure") == 0)
            pure = true;
        else if (argv[i][0] != '-')
            seconds = std::max(1, std::atoi(argv[i]));
        else
            std::fprintf(stderr, "net-run: ignoring %s (not an option it knows)\n", argv[i]);
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto nowMs = [&] { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()) + 1; };
    std::string err;
    q3::CvarStore cvars;

    if (join) {
        const auto [host, p] = reluva::net::splitHostPort(argv[4], 27960);
        uint32_t ip = 0;
        if (!reluva::net::resolveSystemHost(host, ip)) {
            std::fprintf(stderr, "net-run: can't find %s\n", host.c_str());
            return 1;
        }
        auto socket = reluva::net::openSystemUdp(0, err);
        if (!socket) {
            std::fprintf(stderr, "net-run: %s\n", err.c_str());
            return 1;
        }
        const reluva::net::Address server{ip, p};
        std::printf("joining %s from port %u\n", server.toString().c_str(), socket->port());
        ef::NetClient client(std::move(socket), server,
                             "\\name\\" + name + "\\model\\munro\\handicap\\100\\snaps\\20\\rate\\25000", 4321);
        auto state = client.state();
        size_t snapshots = 0, entities = 0, commandsSeen = 0;
        int32_t lastTime = 0;
        while (nowMs() < uint64_t(seconds) * 1000) {
            const uint64_t now = nowMs();
            client.frame(now);
            if (client.state() != state) {
                state = client.state();
                std::printf("%6.2f s: state %d%s%s\n", double(now) / 1000, int(state),
                            client.message().empty() ? "" : ": ", client.message().c_str());
                if (state == ef::NetClient::State::Disconnected)
                    return 1;
            }
            if (client.takeNewGamestate()) {
                const auto& cs = client.configstrings();
                std::printf("game state: %zu configstrings, map %s, commands from %d\n", cs.size(),
                            cs.count(0) ? q3::infoValue(cs.at(0), "mapname").c_str() : "?",
                            client.gamestateCommandSequence());
                client.setLoaded();
            }
            for (const auto& snap : client.takeSnapshots()) {
                ++snapshots;
                entities = snap.entities.size();
                lastTime = snap.serverTime;
                if (snapshots == 1 || snapshots % 100 == 0) {
                    float origin[3] = {};
                    if (snap.playerState.size() >= 32)
                        std::memcpy(origin, snap.playerState.data() + 20, 12);
                    std::printf("snapshot %zu: server time %d, %zu entities, player at (%.0f %.0f %.0f)\n", snapshots,
                                snap.serverTime, entities, double(origin[0]), double(origin[1]), double(origin[2]));
                }
            }
            for (; commandsSeen < client.serverCommands().size(); ++commandsSeen)
                if (!client.serverCommands()[commandsSeen].empty())
                    std::printf("server command %zu: %s\n", commandsSeen + 1, client.serverCommands()[commandsSeen].c_str());
            if (client.hasSnapshot()) {
                ef::GameHost::UserCommand cmd;
                cmd.serverTime = client.serverTime();
                cmd.forward = 127;
                client.addCommand(cmd);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("%zu snapshots (the last at server time %d, %zu entities); leaving\n", snapshots, lastTime, entities);
        client.disconnect();
        return snapshots ? 0 : 1;
    }

    std::string map = argv[3];
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    Bytes data;
    auto bsp = std::make_shared<q3::Bsp>();
    if (reluva::Error e = set.read("maps/" + map + ".bsp", data); e || (e = q3::parseBsp(data, *bsp))) {
        std::fprintf(stderr, "maps/%s.bsp: %s\n", map.c_str(), e.message.c_str());
        return 1;
    }
    cvars.set("g_holoIntro", "0");
    cvars.create("dedicated", "1", 0);
    if (publicServer)
        cvars.set("dedicated", "2");
    ef::GameHost game(set, cvars);
    game.print = [](const std::string& text) {
        std::printf("game: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    if (reluva::Error e = game.load(); e || (e = game.startLevel(map, bsp, int32_t(start.time_since_epoch().count())))) {
        std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("game code: Elite Force %s layout\n", game.layout().version == 12 ? "1.2" : "1.0");
    if (game.layout().version != 12)
        std::printf("warning: the protocol carries patch 1.2's layouts; players joining 1.0's game won't see it right\n");
    for (const std::string& bot : bots)
        if (reluva::Error e = game.serverCommand("addbot " + bot))
            std::fprintf(stderr, "net-run: addbot: %s\n", e.message.c_str());
    auto socket = reluva::net::openSystemUdp(uint16_t(port), err);
    if (!socket) {
        std::fprintf(stderr, "net-run: %s\n", err.c_str());
        return 1;
    }
    ef::NetServer server(game, cvars, std::move(socket));
    server.print = [](const std::string& text) { std::printf("server: %s", text.c_str()); };
    server.resolveHost = [](const std::string& host, uint32_t& ip) { return reluva::net::resolveSystemHost(host, ip); };
    server.setFiles(&set);
    if (pure)
        cvars.set("sv_pure", "1");
    server.levelStarted();
    std::printf("hosting %s on port %u for %d s%s\n", map.c_str(), server.port(), seconds,
                server.pure() ? ", pure" : "");
    std::printf("the level's paks: %s\n", cvars.string("sv_referencedPakNames").c_str());
    const int32_t startTime = game.levelTime();
    while (nowMs() < uint64_t(seconds) * 1000) {
        const uint64_t now = nowMs();
        if (reluva::Error e = server.receive(now)) {
            std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
            return 1;
        }
        while (game.levelTime() - startTime < int32_t(now)) {
            if (reluva::Error e = game.runFrame(); e || (e = server.frame(now))) {
                std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
                return 1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("done; players at the end:");
    for (const auto& [number, who] : server.players())
        std::printf(" %d: %s", number, who.c_str());
    std::printf("\n");
    return 0;
}

// Looking for servers as the menus' list does (getinfo, broadcast on the
// four server ports), or asking one (getinfo and getstatus), printing
// every answer as it comes, for when the list stays empty.
int cmdNetFind(int argc, char** argv) {
    std::string err;
    auto socket = reluva::net::openSystemUdp(0, err);
    if (!socket) {
        std::fprintf(stderr, "net-find: %s\n", err.c_str());
        return 1;
    }
    auto send = [&](const reluva::net::Address& to, const std::string& text) {
        std::vector<uint8_t> d(4, 0xff);
        d.insert(d.end(), text.begin(), text.end());
        const bool sent = socket->send(to, d);
        std::printf("sent \"%s\" to %s%s\n", text.c_str(), to.toString().c_str(), sent ? "" : " (failed)");
    };
    if (argc >= 3) {
        const auto [host, port] = reluva::net::splitHostPort(argv[2], 27960);
        uint32_t ip = 0;
        if (!reluva::net::resolveSystemHost(host, ip)) {
            std::fprintf(stderr, "net-find: can't find %s\n", host.c_str());
            return 1;
        }
        send({ip, port}, "getinfo xxx");
        send({ip, port}, "getstatus xxx");
    } else {
        const std::vector<uint32_t> broadcasts = reluva::net::systemBroadcastAddresses();
        for (uint16_t p = 27960; p < 27964; ++p) {
            send(reluva::net::Address::broadcast(p), "getinfo xxx");
            for (uint32_t b : broadcasts)
                send({b, p}, "getinfo xxx");
        }
    }
    int answers = 0;
    using Clock = std::chrono::steady_clock;
    const auto until = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < until) {
        reluva::net::Address from;
        std::vector<uint8_t> d;
        while (socket->receive(from, d)) {
            ++answers;
            std::string text;
            for (uint8_t c : d)
                text += c == 0xff ? std::string("\\xff") : c == '\n' ? std::string("\\n") : std::string(1, char(c));
            std::printf("from %s (%zu bytes): %s\n", from.toString().c_str(), d.size(), text.c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("%d answer(s)\n", answers);
    return answers ? 0 : 1;
}

// Asking master servers for their lists, as the menus' Internet list
// does (and in dpmaster's form too), printing what each answers.
int cmdNetMaster(int argc, char** argv) {
    namespace q3 = reluva::idtech3;
    std::vector<std::string> names;
    for (int i = 2; i < argc; ++i)
        names.emplace_back(argv[i]);
    if (names.empty()) {
        q3::CvarStore cvars;
        names = q3::masterServerNames(cvars, q3::eliteforce::masterServers());
    }
    std::string err;
    auto socket = reluva::net::openSystemUdp(0, err);
    if (!socket) {
        std::fprintf(stderr, "net-master: %s\n", err.c_str());
        return 1;
    }
    int answers = 0;
    for (const std::string& name : names) {
        const auto [host, port] = reluva::net::splitHostPort(name, q3::ServerBrowser::kMasterPort);
        uint32_t ip = 0;
        if (!reluva::net::resolveSystemHost(host, ip)) {
            std::printf("%s: can't find it\n", name.c_str());
            continue;
        }
        const reluva::net::Address master{ip, port};
        std::printf("%s (%s):\n", name.c_str(), master.toString().c_str());
        for (const char* query : {"getservers 24", "getservers 24 full empty", "getservers EliteForce 24 full empty"}) {
            std::vector<uint8_t> d(4, 0xff);
            d.insert(d.end(), query, query + std::strlen(query));
            socket->send(master, d);
            using Clock = std::chrono::steady_clock;
            const auto until = Clock::now() + std::chrono::seconds(3);
            int got = 0;
            while (Clock::now() < until) {
                reluva::net::Address from;
                std::vector<uint8_t> reply;
                while (socket->receive(from, reply)) {
                    ++got;
                    ++answers;
                    const auto servers = q3::ServerBrowser::serversFromResponse(reply);
                    std::string head;
                    for (size_t i = 4; i < reply.size() && head.size() < 60; ++i)
                        head += reply[i] >= 32 && reply[i] < 127 ? char(reply[i]) : '.';
                    std::printf("  \"%s\": %zu bytes from %s, %zu servers: %s\n", query, reply.size(),
                                from.toString().c_str(), servers.size(), head.c_str());
                    for (size_t i = 0; i < servers.size() && i < 5; ++i)
                        std::printf("    %s\n", servers[i].toString().c_str());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!got)
                std::printf("  \"%s\": no answer\n", query);
        }
    }
    return answers ? 0 : 1;
}

const Commands commands("idtech3", {
    {"net-run", "<game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot name]... | -join <host[:port]> [seconds] [-name name]",
     "host a level as a server, with no player of its own, for others to join; or (-join) join a "
     "server and report what it sends",
     cmdNetRun},
    {"net-find", "[host[:port]]",
     "look for servers on the network as the menus' list does, or ask one, printing every answer",
     cmdNetFind},
    {"net-master", "[master[:port]]...",
     "ask master servers for their lists, as the menus' Internet list does",
     cmdNetMaster},
});

} // namespace
