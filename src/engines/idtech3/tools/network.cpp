// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for network play.

#include "devtools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "backends/networking/sockets/socket_net.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/eliteforce/net_protocol.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/client/server_browser.h"
#include "engines/idtech3/jk2/net_fields.h"
#include "emulation/x86/cpu.h"

#include "common/pe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

// Network play from a terminal (protocol 24, docs/engines/idtech3/games/stvef/systems/network.md):
// hosting a level as a server with no player of its own, for others to
// join for `seconds`; or, with -join, joining a server and reporting
// what it sends while walking forward.
// A player joining the server in memory on fixed 50 ms steps, running
// forward once in the level: what each side reports, for a record that
// repeats. With `changeLevel`, the server changes levels halfway.
int loopbackPlayer(reluva::idtech3::eliteforce::GameHost &game, reluva::idtech3::eliteforce::NetServer &server,
                   reluva::net::LoopbackNetwork &memory, const reluva::idtech3::eliteforce::NetProtocol &protocol,
                   const std::string &userinfo, const std::vector<std::string> &clientCommands, int frames,
                   const std::function<reluva::Error()> &changeLevel = {}) {
    namespace ef = reluva::idtech3::eliteforce;
    server.print = [](const std::string &text) { std::printf("server: %s", text.c_str()); };
    ef::NetClient client(memory.open(0), {0x7f000001u, server.port()}, userinfo, 77, protocol);
    uint64_t now = 1000;
    auto state = client.state();
    size_t snapshots = 0;
    size_t commandsSeen = 0;
    float firstX = 0;
    float lastX = 0;
    size_t entities = 0;
    int gamestates = 0;
    int mapChanges = 0;
    for (int frame = 0; frame < frames; ++frame) {
        if (changeLevel && frame == frames / 2) {
            // As the Play page's: the players told, then the level loaded
            // and the game started on it, and the players sent it anew.
            server.levelChanging();
            if (reluva::Error e = changeLevel()) {
                std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
                return 1;
            }
            server.levelStarted();
            std::printf("frame %d: the server changes levels\n", frame);
        }
        client.frame(now);
        if (reluva::Error e = server.receive(now); e || (e = game.runFrame()) || (e = server.frame(now))) {
            std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
            return 1;
        }
        now += 50;
        if (client.state() != state) {
            state = client.state();
            std::printf("frame %d: client state %d%s%s\n", frame, int(state), client.message().empty() ? "" : ": ",
                        client.message().c_str());
            if (state == ef::NetClient::State::kDisconnected)
                return 1;
        }
        if (client.takeMapChange()) {
            if (!mapChanges)
                std::printf("frame %d: the server says it's changing maps\n", frame);
            ++mapChanges;
        }
        if (client.takeNewGamestate()) {
            ++gamestates;
            std::printf("frame %d: game state: %zu configstrings, player %d, checksum feed given\n", frame,
                        client.configstrings().size(), client.clientNum());
            client.setLoaded();
            for (const std::string &c : clientCommands)
                client.addClientCommand(c);
        }
        for (const auto &snap : client.takeSnapshots()) {
            float x = 0;
            if (snap.playerState.size() >= 24)
                std::memcpy(&x, snap.playerState.data() + 20, 4);
            if (!snapshots++)
                firstX = x;
            lastX = x;
            entities = snap.entities.size();
        }
        for (; commandsSeen < client.serverCommands().size(); ++commandsSeen)
            if (!client.serverCommands()[commandsSeen].empty() &&
                client.serverCommands()[commandsSeen].rfind("cs ", 0) != 0)
                std::printf("server command %zu: %.60s\n", commandsSeen + 1,
                            client.serverCommands()[commandsSeen].c_str());
        if (client.hasSnapshot()) {
            ef::GameHost::UserCommand cmd;
            cmd.serverTime = client.serverTime();
            cmd.forward = 127;
            client.addCommand(cmd);
        }
    }
    std::printf("%zu snapshots, the last with %zu entities; the player went from x %.0f to %.0f\n", snapshots, entities,
                double(firstX), double(lastX));
    if (changeLevel)
        std::printf("frames with a map change notice: %d; game states: %d\n", mapChanges, gamestates);
    const bool changed = !changeLevel || (gamestates == 2 && (mapChanges > 0) == protocol.keyed);
    return snapshots && changed && client.state() == ef::NetClient::State::kActive ? 0 : 1;
}

int cmdNetRun(int argc, char **argv) {
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    if (argc < 4) {
        std::fprintf(
            stderr,
            "usage: reluva-tool idtech3 net-run <game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot <name>]...\n"
            "       reluva-tool idtech3 net-run <game folder> <map> -loopback [frames] [-set name value]... "
            "[-clientcmd text]... [-mapchange <map>]\n"
            "       reluva-tool idtech3 net-run <game folder> -join <host[:port]> [seconds] [-name <name>]\n");
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
    int seconds = 60;
    int port = gameOfFolder(argv[2]).serverPort;
    std::string name = "ReluvaTool";
    std::vector<std::string> bots;
    bool publicServer = false; // dedicated 2: listed with the master servers
    bool pure = false;         // sv_pure 1: players' paks checked
    bool loopback = false;     // a player joining in memory, on fixed steps
    std::vector<std::pair<std::string, std::string>> sets;
    std::vector<std::string> clientCommands;
    std::string nextMap; // -mapchange: the loopback server's second level
    for (int i = join ? 5 : 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-loopback") == 0)
            loopback = true;
        else if (std::strcmp(argv[i], "-mapchange") == 0 && i + 1 < argc)
            nextMap = argv[++i];
        else if (std::strcmp(argv[i], "-set") == 0 && i + 2 < argc) {
            sets.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        } else if (std::strcmp(argv[i], "-clientcmd") == 0 && i + 1 < argc)
            clientCommands.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-port") == 0 && i + 1 < argc)
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
    auto nowMs = [&] {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()) + 1;
    };
    std::string err;
    q3::CvarStore cvars;
    // The game's own protocol, and the settings its players send.
    const q3::GameInfo info = gameOfFolder(argv[2]);
    const q3::GameCode code =
        info.code == q3::GameCode::kJediOutcast ? q3::GameCode::kJediOutcast : q3::GameCode::kEliteForce;
    const ef::NetProtocol &protocol = ef::netProtocol(code, info.jediRelease);
    auto userinfoFor = [&](const std::string &player) {
        std::string userinfo = "\\name\\" + player + "\\model\\munro\\handicap\\100\\snaps\\20\\rate\\25000";
        if (!info.clientCvars.empty()) {
            userinfo.clear();
            for (const q3::GameInfo::StartCvar &c : info.clientCvars)
                if (c.flags & q3::CvarStore::kUserInfo)
                    q3::setInfoValue(userinfo, c.name, c.name == "name" ? player : c.value);
            q3::setInfoValue(userinfo, "snaps", "20");
            q3::setInfoValue(userinfo, "rate", "25000");
        }
        return userinfo;
    };

    if (join) {
        const auto [host, p] = reluva::net::splitHostPort(argv[4], info.serverPort);
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
        ef::NetClient client(std::move(socket), server, userinfoFor(name), 4321, protocol);
        auto state = client.state();
        size_t snapshots = 0;
        size_t entities = 0;
        size_t commandsSeen = 0;
        int32_t lastTime = 0;
        while (nowMs() < uint64_t(seconds) * 1000) {
            const uint64_t now = nowMs();
            client.frame(now);
            if (client.state() != state) {
                state = client.state();
                std::printf("%6.2f s: state %d%s%s\n", double(now) / 1000, int(state),
                            client.message().empty() ? "" : ": ", client.message().c_str());
                if (state == ef::NetClient::State::kDisconnected)
                    return 1;
            }
            if (client.takeNewGamestate()) {
                const auto &cs = client.configstrings();
                std::printf("game state: %zu configstrings, map %s, commands from %d\n", cs.size(),
                            cs.count(0) ? q3::infoValue(cs.at(0), "mapname").c_str() : "?",
                            client.gamestateCommandSequence());
                client.setLoaded();
            }
            for (const auto &snap : client.takeSnapshots()) {
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
                    std::printf("server command %zu: %s\n", commandsSeen + 1,
                                client.serverCommands()[commandsSeen].c_str());
            if (client.hasSnapshot()) {
                ef::GameHost::UserCommand cmd;
                cmd.serverTime = client.serverTime();
                cmd.forward = 127;
                client.addCommand(cmd);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("%zu snapshots (the last at server time %d, %zu entities); leaving\n", snapshots, lastTime,
                    entities);
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
    for (const auto &[n, v] : sets)
        cvars.set(n, v);
    ef::GameHost game(set, cvars, code, info.jediRelease);
    game.print = [](const std::string &text) {
        std::printf("game: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    if (reluva::Error e = game.load();
        e || (e = game.startLevel(map, bsp, loopback ? 1234 : int32_t(start.time_since_epoch().count())))) {
        std::fprintf(stderr, "net-run: %s\n", e.message.c_str());
        return 1;
    }
    if (code == q3::GameCode::kJediOutcast)
        std::printf("game code: Jedi Outcast's, protocol %d\n", protocol.version);
    else
        std::printf("game code: Elite Force %s layout\n", game.layout().version == 12 ? "1.2" : "1.0");
    if (code == q3::GameCode::kEliteForce && game.layout().version != 12)
        std::printf(
            "warning: the protocol carries patch 1.2's layouts; players joining 1.0's game won't see it right\n");
    for (const std::string &bot : bots)
        if (reluva::Error e = game.serverCommand("addbot " + bot))
            std::fprintf(stderr, "net-run: addbot: %s\n", e.message.c_str());
    std::shared_ptr<reluva::net::LoopbackNetwork> memory;
    std::unique_ptr<reluva::net::UdpSocket> socket;
    if (loopback) {
        memory = reluva::net::LoopbackNetwork::create();
        socket = memory->open(uint16_t(port));
    } else {
        socket = reluva::net::openSystemUdp(uint16_t(port), err);
    }
    if (!socket) {
        std::fprintf(stderr, "net-run: %s\n", err.c_str());
        return 1;
    }
    ef::NetServer server(game, cvars, std::move(socket), protocol);
    server.print = [](const std::string &text) { std::printf("server: %s", text.c_str()); };
    server.resolveHost = [](const std::string &host, uint32_t &ip) { return reluva::net::resolveSystemHost(host, ip); };
    server.setFiles(&set);
    if (pure)
        cvars.set("sv_pure", "1");
    server.levelStarted();
    std::printf("hosting %s on port %u for %d s%s\n", map.c_str(), server.port(), seconds,
                server.pure() ? ", pure" : "");
    std::printf("the level's paks: %s\n", cvars.string("sv_referencedPakNames").c_str());
    std::function<reluva::Error()> change;
    if (loopback && !nextMap.empty())
        change = [&]() -> reluva::Error {
            auto next = std::make_shared<q3::Bsp>();
            Bytes nextData;
            if (reluva::Error e = set.read("maps/" + nextMap + ".bsp", nextData);
                e || (e = q3::parseBsp(nextData, *next)))
                return e;
            std::vector<int32_t> players;
            return game.changeLevel(nextMap, next, 4321, players);
        };
    if (loopback)
        return loopbackPlayer(game, server, *memory, protocol, userinfoFor("Remote"), clientCommands,
                              seconds == 60 ? 200 : seconds, change);
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
    for (const auto &[number, who] : server.players())
        std::printf(" %d: %s", number, who.c_str());
    std::printf("\n");
    return 0;
}

// Looking for servers as the menus' list does (getinfo, broadcast on the
// four server ports), or asking one (getinfo and getstatus), printing
// every answer as it comes, for when the list stays empty.
int cmdNetFind(int argc, char **argv) {
    std::string err;
    auto socket = reluva::net::openSystemUdp(0, err);
    if (!socket) {
        std::fprintf(stderr, "net-find: %s\n", err.c_str());
        return 1;
    }
    auto send = [&](const reluva::net::Address &to, const std::string &text) {
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
int cmdNetMaster(int argc, char **argv) {
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
    for (const std::string &name : names) {
        const auto [host, port] = reluva::net::splitHostPort(name, q3::ServerBrowser::kMasterPort);
        uint32_t ip = 0;
        if (!reluva::net::resolveSystemHost(host, ip)) {
            std::printf("%s: can't find it\n", name.c_str());
            continue;
        }
        const reluva::net::Address master{ip, port};
        std::printf("%s (%s):\n", name.c_str(), master.toString().c_str());
        for (const char *query : {"getservers 24", "getservers 24 full empty", "getservers EliteForce 24 full empty"}) {
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

// Jedi Outcast's message code against jk2mp.exe's own (1.02a's): its
// routine that builds the tree from its byte counts (0x4326f0) run in the
// x86 interpreter, then each byte's path down the tree it built (the
// compressor's, at 0xad1488) compared with Reluva's code.
int cmdHuffmanCheck(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool idtech3 huffman-check <jk2mp.exe>\n");
        return 2;
    }
    constexpr uint32_t kBuildTree = 0x4326f0;
    constexpr uint32_t kCompressor = 0xad1488;
    constexpr uint32_t kMemory = 32u << 20;
    // node_t: left, right, parent, next, prev, head, weight, symbol.
    constexpr uint32_t kLeft = 0;
    constexpr uint32_t kRight = 4;
    constexpr uint32_t kSymbol = 28;
    constexpr uint32_t kInternal = 257;
    Bytes file;
    if (!readFile(argv[2], file))
        return 1;
    reluva::PeImage pe;
    if (reluva::Error err = reluva::parsePe(file, pe)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    reluva::x86::Cpu cpu(kMemory);
    for (const auto &sec : pe.sections)
        for (uint32_t i = 0; i < sec.rawSize && i < sec.virtualSize && sec.rawOffset + i < file.size(); ++i)
            cpu.write8(uint32_t(pe.imageBase) + sec.virtualAddress + i, file[sec.rawOffset + i]);
    // Its tree-building routine names the table of byte counts.
    if (cpu.read32(kBuildTree + 0x1d) != 0x4f6310) {
        std::fprintf(stderr, "%s: not 1.02a's jk2mp.exe (no tree-building routine at 0x%x)\n", argv[2], kBuildTree);
        return 1;
    }
    cpu.setStack(kMemory - 4096);
    uint32_t result = 0;
    if (reluva::Error err = cpu.call(kBuildTree, {}, result)) {
        std::fprintf(stderr, "running jk2mp.exe's routine: %s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::vector<uint8_t>> paths(256);
    std::vector<int> found(256);
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> stack{{cpu.read32(kCompressor + 8), {}}};
    while (!stack.empty()) {
        auto [node, path] = stack.back();
        stack.pop_back();
        if (!cpu.valid(node, 32) || path.size() > 64)
            continue;
        const uint32_t symbol = cpu.read32(node + kSymbol);
        if (symbol != kInternal) {
            if (symbol < 256) {
                paths[symbol] = path;
                ++found[symbol];
            }
            continue;
        }
        auto left = path;
        left.push_back(0);
        auto right = path;
        right.push_back(1);
        stack.push_back({cpu.read32(node + kLeft), left});
        stack.push_back({cpu.read32(node + kRight), right});
    }
    const reluva::idtech3::wire::Huffman &mine = reluva::idtech3::jk2::messageHuffman();
    int differ = 0;
    size_t longest = 0;
    for (int s = 0; s < 256; ++s) {
        std::vector<uint8_t> bytes;
        size_t bit = 0;
        mine.writeCode(uint8_t(s), bytes, bit);
        bool same = found[size_t(s)] == 1 && paths[size_t(s)].size() == bit;
        for (size_t i = 0; same && i < bit; ++i)
            same = paths[size_t(s)][i] == ((bytes[i / 8] >> (i % 8)) & 1);
        longest = std::max(longest, bit);
        if (!same) {
            if (differ < 8)
                std::printf("byte %d: jk2mp.exe's code has %zu bits, Reluva's %zu\n", s, paths[size_t(s)].size(), bit);
            ++differ;
        }
    }
    std::printf("256 bytes' codes (the longest %zu bits): %d differ from jk2mp.exe's\n", longest, differ);
    // The connect packet's compression (0x430450, on a msg_t: data at 12,
    // its length at 20), on connect texts with made-up user settings.
    constexpr uint32_t kCompress = 0x430450;
    constexpr uint32_t kMsg = kMemory - (2u << 20);
    constexpr uint32_t kData = kMsg + 64;
    int compressDiffer = 0;
    uint32_t seed = 1;
    const int kTexts = 64;
    for (int t = 0; t < kTexts; ++t) {
        std::string text = "\xff\xff\xff\xff"
                           "connect \"\\name\\Padawan";
        for (int k = 0; k < t * 3; ++k) {
            seed = seed * 1103515245u + 12345u;
            text += char(t % 2 ? 'a' + int(seed >> 16) % 26 : 32 + int(seed >> 16) % 95);
        }
        text += "\\protocol\\15\"";
        const std::vector<uint8_t> in(text.begin(), text.end());
        for (size_t i = 0; i < in.size() + 16; ++i)
            cpu.write8(kData + uint32_t(i), i < in.size() ? in[i] : 0);
        cpu.write32(kMsg + 12, kData);
        cpu.write32(kMsg + 16, 1 << 16);
        cpu.write32(kMsg + 20, uint32_t(in.size()));
        if (reluva::Error err = cpu.call(kCompress, {kMsg, 12}, result)) {
            std::fprintf(stderr, "running jk2mp.exe's compression: %s\n", err.message.c_str());
            return 1;
        }
        const uint32_t size = cpu.read32(kMsg + 20);
        const std::vector<uint8_t> mine2 = reluva::idtech3::wire::Huffman::compress(in, 12);
        // The last byte may hold what was in its buffer before; the rest must agree.
        bool same = mine2.size() == size;
        for (uint32_t i = 0; same && i + 1 < size; ++i)
            same = mine2[i] == cpu.read8(kData + i);
        if (!same)
            ++compressDiffer;
        if (reluva::idtech3::wire::Huffman::decompress(mine2, 12) != in)
            ++compressDiffer;
    }
    std::printf("%d connect texts compressed: %d differ from jk2mp.exe's, or don't come back\n", kTexts,
                compressDiffer);
    return differ || compressDiffer ? 1 : 0;
}

// Jedi Outcast's entity and player-state deltas, keyed user commands and
// the client's XOR against jk2mp.exe's own (1.02a's, 1.03's or 1.04's),
// run in the x86 interpreter on made-up states that use every field of its
// tables: what each writes must be the same bits, and what each reads back
// from the other's the same state.
int cmdDeltaCheck(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool idtech3 delta-check <jk2mp.exe> [pairs]\n");
        return 2;
    }
    namespace jk2 = reluva::idtech3::jk2;
    // Each release's routines and tables (its tree builder's Huffman
    // counts and its entity reader's cl_shownet tell them apart).
    struct Release {
        const char *name;
        bool patched;
        uint32_t buildTree;
        uint32_t writeEntity;
        uint32_t readEntity;
        uint32_t writePlayer;
        uint32_t readPlayer;
        uint32_t readBits;
        uint32_t writeCommand;
        uint32_t readCommand;
        uint32_t entityFields;
        uint32_t playerFields;
        uint32_t counts;
        uint32_t showNet;
    };
    constexpr std::array<Release, 3> kReleases = {{
        {"1.02a", false, 0x4326f0, 0x431aa0, 0x431cd0, 0x431f60, 0x432340, 0x4311b0, 0x431750, 0x431900, 0x4f5ab0,
         0x4f5e20, 0x4f6310, 0x8ca3d0},
        {"1.03", true, 0x433070, 0x432420, 0x432650, 0x4328e0, 0x432cc0, 0x431b30, 0x4320d0, 0x432280, 0x4fce48,
         0x4fd1b8, 0x4fd6c8, 0x8d8bd8},
        {"1.04", true, 0x433090, 0x432440, 0x432670, 0x432900, 0x432ce0, 0x431b50, 0x4320f0, 0x4322a0, 0x4fce48,
         0x4fd1b8, 0x4fd6c8, 0x8d8be8},
    }};
    constexpr uint32_t kMemory = 32u << 20;
    constexpr uint32_t kScratch = kMemory - (4u << 20);
    constexpr uint32_t kCvar = kScratch;
    constexpr uint32_t kMsg = kScratch + 0x100;
    constexpr uint32_t kData = kScratch + 0x1000;
    constexpr uint32_t kDataSize = 0x10000;
    constexpr uint32_t kFrom = kScratch + 0x20000;
    constexpr uint32_t kTo = kScratch + 0x22000;
    constexpr uint32_t kOut = kScratch + 0x24000;
    // msg_t: data, maxsize, cursize, readcount, bit.
    constexpr uint32_t kMsgData = 12;
    constexpr uint32_t kMsgMaxSize = 16;
    constexpr uint32_t kMsgSize = 20;
    constexpr uint32_t kMsgBit = 28;
    const int pairs = argc == 4 ? std::max(1, std::atoi(argv[3])) : 500;
    Bytes file;
    if (!readFile(argv[2], file))
        return 1;
    reluva::PeImage pe;
    if (reluva::Error err = reluva::parsePe(file, pe)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    reluva::x86::Cpu cpu(kMemory);
    for (const auto &sec : pe.sections)
        for (uint32_t i = 0; i < sec.rawSize && i < sec.virtualSize && sec.rawOffset + i < file.size(); ++i)
            cpu.write8(uint32_t(pe.imageBase) + sec.virtualAddress + i, file[sec.rawOffset + i]);
    const Release *found = nullptr;
    for (const Release &r : kReleases)
        if (cpu.read32(r.buildTree + 0x1d) == r.counts && cpu.read32(r.readEntity + 0x76) == r.showNet)
            found = &r;
    if (!found) {
        std::fprintf(stderr, "%s: not 1.02a's, 1.03's or 1.04's jk2mp.exe\n", argv[2]);
        return 1;
    }
    const Release &rel = *found;
    std::printf("jk2mp.exe %s\n", rel.name);
    const uint32_t kBuildTree = rel.buildTree;
    const uint32_t kWriteEntity = rel.writeEntity;
    const uint32_t kReadEntity = rel.readEntity;
    const uint32_t kWritePlayer = rel.writePlayer;
    const uint32_t kReadPlayer = rel.readPlayer;
    const uint32_t kReadBits = rel.readBits;
    const uint32_t kWriteCommand = rel.writeCommand;
    const uint32_t kReadCommand = rel.readCommand;
    const uint32_t kShowNet = rel.showNet;
    const size_t playerSize = rel.patched ? jk2::kNetPlayerStateSizePatched : jk2::kNetPlayerStateSize;
    auto writeEntity = rel.patched ? jk2::writeEntityDeltaPatched : jk2::writeEntityDelta;
    auto readEntity = rel.patched ? jk2::readEntityDeltaPatched : jk2::readEntityDelta;
    auto writePlayer = rel.patched ? jk2::writePlayerDeltaPatched : jk2::writePlayerDelta;
    auto readPlayer = rel.patched ? jk2::readPlayerDeltaPatched : jk2::readPlayerDelta;
    cpu.setStack(kMemory - 4096);
    uint32_t result = 0;
    auto run = [&](uint32_t at, std::initializer_list<uint32_t> args) {
        if (reluva::Error err = cpu.call(at, args, result)) {
            std::fprintf(stderr, "running jk2mp.exe's routine at 0x%x: %s\n", at, err.message.c_str());
            std::exit(1);
        }
        return result;
    };
    run(kBuildTree, {});
    cpu.write32(kShowNet, kCvar); // its integer (at 32) 0
    // A table's fields: name, offset, bits (0: a float), until the names end.
    struct Field {
        uint32_t offset;
        int32_t bits;
    };
    auto fields = [&](uint32_t table, uint32_t stateSize) {
        std::vector<Field> out;
        for (uint32_t at = table;; at += 12) {
            const uint32_t name = cpu.read32(at);
            const uint32_t offset = cpu.read32(at + 4);
            if (name < 0x4e4000 || name >= 0x511000 || offset + 4 > stateSize)
                break;
            out.push_back({offset, int32_t(cpu.read32(at + 8))});
        }
        return out;
    };
    const std::vector<Field> entityFields = fields(rel.entityFields, jk2::kNetEntityStateSize);
    const std::vector<Field> playerFields = fields(rel.playerFields, uint32_t(playerSize));
    uint32_t seed = 7;
    auto random = [&] {
        seed = seed * 1103515245u + 12345u;
        return seed >> 8;
    };
    auto put = [](std::vector<uint8_t> &state, uint32_t at, uint32_t v) { std::memcpy(state.data() + at, &v, 4); };
    // A field's value: floats whole (small or not) or not, ints any of their bits.
    auto value = [&](const Field &f) {
        if (f.bits == 0) {
            float v = 0;
            switch (random() % 4) {
            case 1:
                v = float(int(random() % 8192) - 4096);
                break;
            case 2:
                v = float(int(random() % 200000) - 100000);
                break;
            case 3:
                v = float(int(random() % 20000) - 10000) / 7.0f;
                break;
            default:
                break;
            }
            uint32_t bits = 0;
            std::memcpy(&bits, &v, 4);
            return bits;
        }
        // Negative counts are signed: read back extended from the top bit.
        const int n = std::abs(f.bits);
        uint32_t v = random() ^ (random() << 16);
        if (n < 32) {
            v &= (1u << n) - 1;
            if (f.bits < 0 && (v >> (n - 1)) & 1)
                v |= ~((1u << n) - 1);
        }
        return v;
    };
    // A pair of states: "from" empty a quarter of the time, each field of
    // "to" changed half of the time. Words outside the tables stay 0.
    auto makePair = [&](const std::vector<Field> &table, size_t size, std::vector<uint8_t> &from,
                        std::vector<uint8_t> &to) {
        from.assign(size, 0);
        to.assign(size, 0);
        const bool empty = random() % 4 == 0;
        for (const Field &f : table) {
            const uint32_t was = empty ? 0 : value(f);
            put(from, f.offset, was);
            put(to, f.offset, random() % 2 ? value(f) : was);
        }
    };
    // Player states' 16-bit arrays (stats, persistant, ammo), sent after
    // the fields by bit masks.
    auto put16 = [&](std::vector<uint8_t> &state, uint32_t first, int count) {
        for (int i = 0; i < count; ++i)
            if (random() % 3 == 0)
                put(state, first + 4 * uint32_t(i), random() % 30000);
    };
    auto load = [&](uint32_t at, const std::vector<uint8_t> &bytes) {
        for (size_t i = 0; i < bytes.size(); ++i)
            cpu.write8(at + uint32_t(i), bytes[i]);
    };
    auto fetch = [&](uint32_t at, size_t size) {
        std::vector<uint8_t> out(size);
        for (size_t i = 0; i < size; ++i)
            out[i] = cpu.read8(at + uint32_t(i));
        return out;
    };
    auto newMsg = [&](const std::vector<uint8_t> &bytes) {
        for (uint32_t i = 0; i < 32; i += 4)
            cpu.write32(kMsg + i, 0);
        for (uint32_t i = 0; i < kDataSize; ++i)
            cpu.write8(kData + i, i < bytes.size() ? bytes[i] : 0);
        cpu.write32(kMsg + kMsgData, kData);
        cpu.write32(kMsg + kMsgMaxSize, kDataSize);
        cpu.write32(kMsg + kMsgSize, uint32_t(bytes.size()));
    };
    // The written bits and the message's size the same.
    auto sameBits = [&](const reluva::idtech3::wire::MessageWriter &w) {
        const uint32_t bits = cpu.read32(kMsg + kMsgBit);
        if (bits != w.bitCount() || cpu.read32(kMsg + kMsgSize) != w.bytes().size())
            return false;
        for (uint32_t i = 0; i < (bits + 7) / 8; ++i)
            if (cpu.read8(kData + i) != w.bytes()[i])
                return false;
        return true;
    };
    const auto *code = &jk2::messageHuffman();
    int entityWriteDiffer = 0;
    int entityReadDiffer = 0;
    int playerWriteDiffer = 0;
    int playerReadDiffer = 0;
    size_t entityBits = 0;
    size_t playerBits = 0;
    std::vector<uint8_t> from;
    std::vector<uint8_t> to;
    for (int n = 0; n < pairs; ++n) {
        makePair(entityFields, jk2::kNetEntityStateSize, from, to);
        const uint32_t number = random() % 1022;
        put(from, 0, number);
        put(to, 0, number);
        load(kFrom, from);
        load(kTo, to);
        newMsg({});
        run(kWriteEntity, {kMsg, kFrom, kTo, uint32_t(n % 8 == 0)});
        reluva::idtech3::wire::MessageWriter w(code);
        writeEntity(w, from, to, n % 8 == 0);
        entityBits += w.bitCount();
        if (!sameBits(w)) {
            if (entityWriteDiffer < 4)
                std::printf("entity pair %d: jk2mp.exe writes %u bits, Reluva %zu\n", n, cpu.read32(kMsg + kMsgBit),
                            w.bitCount());
            ++entityWriteDiffer;
            continue;
        }
        if (w.bitCount() == 0)
            continue;
        // Each reads the other's (the same bits), from the number on.
        newMsg(w.bytes());
        const uint32_t readNumber = run(kReadBits, {kMsg, 10});
        load(kOut, std::vector<uint8_t>(jk2::kNetEntityStateSize, 0));
        run(kReadEntity, {kMsg, kFrom, kOut, readNumber});
        const std::vector<uint8_t> theirs = fetch(kOut, jk2::kNetEntityStateSize);
        reluva::idtech3::wire::MessageReader r(w.bytes(), code);
        std::vector<uint8_t> mine;
        const int32_t myNumber = int32_t(r.bits(10));
        const bool removed = readEntity(r, myNumber, from, mine);
        // An entity gone from the snapshot: the original gives its state no number (1023).
        if (removed ? cpu.read32(kOut) != 1023 : (theirs != mine || mine != to || uint32_t(myNumber) != readNumber)) {
            if (entityReadDiffer < 4) {
                size_t at = 0;
                while (at + 4 < mine.size() && std::memcmp(&theirs[at], &mine[at], 4) == 0 &&
                       std::memcmp(&mine[at], &to[at], 4) == 0)
                    at += 4;
                uint32_t t = 0;
                uint32_t m = 0;
                uint32_t e = 0;
                std::memcpy(&t, &theirs[at], 4);
                std::memcpy(&m, &mine[at], 4);
                std::memcpy(&e, &to[at], 4);
                std::printf("entity pair %d: read back differently at %zu (jk2mp.exe 0x%x, Reluva 0x%x, sent 0x%x)\n",
                            n, at, t, m, e);
            }
            ++entityReadDiffer;
        }
    }
    for (int n = 0; n < pairs; ++n) {
        makePair(playerFields, playerSize, from, to);
        // stats at 216, persistant at 280, ammo at 408 (as the writer reads
        // them, 16 bits sent), powerups at 344 (32 bits); 16 each.
        for (const uint32_t at : {216u, 280u, 408u})
            put16(to, at, 16);
        for (uint32_t i = 0; i < 16; ++i)
            if (random() % 3 == 0)
                put(to, 344 + 4 * i, random() ^ (random() << 16));
        load(kFrom, from);
        load(kTo, to);
        newMsg({});
        run(kWritePlayer, {kMsg, kFrom, kTo});
        reluva::idtech3::wire::MessageWriter w(code);
        writePlayer(w, from, to);
        playerBits += w.bitCount();
        if (!sameBits(w)) {
            if (playerWriteDiffer < 4)
                std::printf("player pair %d: jk2mp.exe writes %u bits, Reluva %zu\n", n, cpu.read32(kMsg + kMsgBit),
                            w.bitCount());
            ++playerWriteDiffer;
            continue;
        }
        newMsg(w.bytes());
        load(kOut, std::vector<uint8_t>(playerSize, 0));
        run(kReadPlayer, {kMsg, kFrom, kOut});
        const std::vector<uint8_t> theirs = fetch(kOut, playerSize);
        reluva::idtech3::wire::MessageReader r(w.bytes(), code);
        std::vector<uint8_t> mine;
        readPlayer(r, from, mine);
        if (theirs != mine || mine != to) {
            if (playerReadDiffer < 4) {
                size_t at = 0;
                while (at + 4 < mine.size() && std::memcmp(&theirs[at], &mine[at], 4) == 0 &&
                       std::memcmp(&mine[at], &to[at], 4) == 0)
                    at += 4;
                uint32_t t = 0;
                uint32_t m = 0;
                uint32_t e = 0;
                std::memcpy(&t, &theirs[at], 4);
                std::memcpy(&m, &mine[at], 4);
                std::memcpy(&e, &to[at], 4);
                std::printf("player pair %d: read back differently at %zu (jk2mp.exe 0x%x, Reluva 0x%x, sent 0x%x)\n",
                            n, at, t, m, e);
            }
            ++playerReadDiffer;
        }
    }
    // User commands, keyed: usercmd_t is serverTime, angles[3], buttons,
    // then bytes weapon, forceSelect, invenSelect, generic, forward,
    // right, up (28 bytes).
    using Command = reluva::idtech3::eliteforce::GameHost::UserCommand;
    auto commandBytes = [](const Command &c) {
        std::vector<uint8_t> b(28, 0);
        std::memcpy(b.data(), &c.serverTime, 4);
        std::memcpy(b.data() + 4, c.angles.data(), 12);
        std::memcpy(b.data() + 16, &c.buttons, 4);
        b[20] = c.weapon;
        b[21] = c.forceSelect;
        b[22] = c.inventorySelect;
        b[23] = c.genericCommand;
        b[24] = uint8_t(c.forward);
        b[25] = uint8_t(c.right);
        b[26] = uint8_t(c.up);
        return b;
    };
    auto randomCommand = [&](const Command &before) {
        Command c = before;
        c.serverTime = before.serverTime + int32_t(random() % 3 ? random() % 100 : random() % 5000);
        if (random() % 4) {
            for (int32_t &a : c.angles)
                a = random() % 2 ? int32_t(random() % 65536) : a;
            c.buttons = random() % 2 ? int32_t(random() % 65536) : c.buttons;
            c.weapon = uint8_t(random() % 2 ? random() % 20 : c.weapon);
            c.forward = int8_t(random() % 2 ? int(random() % 255) - 127 : c.forward);
            c.right = int8_t(random() % 2 ? int(random() % 255) - 127 : c.right);
            c.up = int8_t(random() % 2 ? int(random() % 255) - 127 : c.up);
            c.forceSelect = uint8_t(random() % 3 ? c.forceSelect : random() % 18);
            c.inventorySelect = uint8_t(random() % 3 ? c.inventorySelect : random() % 16);
            c.genericCommand = uint8_t(random() % 3 ? c.genericCommand : random() % 30);
        }
        return c;
    };
    int commandWriteDiffer = 0;
    int commandReadDiffer = 0;
    Command previous;
    for (int n = 0; n < pairs; ++n) {
        const Command next = randomCommand(previous);
        const auto key = int32_t(random() ^ (random() << 16));
        load(kFrom, commandBytes(previous));
        load(kTo, commandBytes(next));
        newMsg({});
        run(kWriteCommand, {kMsg, uint32_t(key), kFrom, kTo});
        reluva::idtech3::wire::MessageWriter w(code);
        jk2::writeUserCommand(w, key, previous, next);
        if (!sameBits(w)) {
            if (commandWriteDiffer < 4)
                std::printf("command %d: jk2mp.exe writes %u bits, Reluva %zu\n", n, cpu.read32(kMsg + kMsgBit),
                            w.bitCount());
            ++commandWriteDiffer;
        } else {
            newMsg(w.bytes());
            load(kOut, std::vector<uint8_t>(28, 0));
            run(kReadCommand, {kMsg, uint32_t(key), kFrom, kOut});
            reluva::idtech3::wire::MessageReader r(w.bytes(), code);
            const Command mine = jk2::readUserCommand(r, key, previous);
            // The 16-bit fields (angles, buttons) read back with the key's
            // bits above 16 left in, in both: only their low 16 are sent.
            auto low16 = [](std::vector<uint8_t> v) {
                for (size_t at = 6; at < 20; at += 4)
                    v[at] = v[at + 1] = 0;
                return v;
            };
            if (fetch(kOut, 28) != commandBytes(mine) || low16(commandBytes(mine)) != low16(commandBytes(next))) {
                if (commandReadDiffer < 4) {
                    auto hex = [](const std::vector<uint8_t> &v) {
                        std::string out;
                        for (uint8_t c : v) {
                            char t[4];
                            std::snprintf(t, sizeof t, "%02x", c);
                            out += t;
                        }
                        return out;
                    };
                    std::printf("command %d: read back differently\n  jk2mp.exe %s\n  Reluva    %s\n  sent      %s\n",
                                n, hex(fetch(kOut, 28)).c_str(), hex(commandBytes(mine)).c_str(),
                                hex(commandBytes(next)).c_str());
                }
                ++commandReadDiffer;
            }
        }
        previous = next;
    }
    // The client's XOR (1.02a's 0x40e0c0): from byte 12, keyed by the challenge
    // (0x821330) and the message's server id and acknowledgement (its
    // first longs), the text the server command its third names (slots of
    // 1,024 bytes from 0x84134c).
    constexpr uint32_t kClientEncode = 0x40e0c0;
    constexpr uint32_t kChallenge = 0x821330;
    constexpr uint32_t kServerCommands = 0x84134c;
    // Only 1.02a's: the patches' globals weren't looked for (their XOR's
    // code is 1.02a's, 0x40e7c0 and 0x40e7d0).
    int xorDiffer = 0;
    const int xorPairs = rel.patched ? 0 : pairs;
    for (int n = 0; n < xorPairs; ++n) {
        reluva::idtech3::wire::MessageWriter w(code);
        const auto serverId = int32_t(random());
        const auto acknowledge = int32_t(random());
        const auto reliable = int32_t(random() % 1000);
        w.longValue(serverId);
        w.longValue(acknowledge);
        w.longValue(reliable);
        std::vector<uint8_t> message = w.bytes();
        const size_t extra = random() % 200;
        for (size_t i = 0; i < extra; ++i)
            message.push_back(uint8_t(random()));
        std::string text;
        const size_t length = random() % 4 ? random() % 60 : 0;
        for (size_t i = 0; i < length; ++i)
            text += char(random() % 5 ? 32 + random() % 95 : (random() % 2 ? '%' : 128 + random() % 128));
        const auto challenge = uint8_t(random());
        cpu.write8(kChallenge, challenge);
        const uint32_t slot = kServerCommands + 1024 * (uint32_t(reliable) & 127);
        for (uint32_t i = 0; i < 1024; ++i)
            cpu.write8(slot + i, i < text.size() ? uint8_t(text[i]) : 0);
        newMsg(message);
        run(kClientEncode, {kMsg});
        const auto key = uint8_t(challenge ^ uint8_t(serverId) ^ uint8_t(acknowledge));
        std::vector<uint8_t> mine = message;
        reluva::idtech3::eliteforce::xorMessage(mine, 12, key, text);
        if (fetch(kData, mine.size()) != mine) {
            if (xorDiffer < 4)
                std::printf("XOR %d: differs from jk2mp.exe's\n", n);
            ++xorDiffer;
        }
    }
    std::printf("%zu entity fields, %zu player state fields (jk2mp.exe's tables)\n", entityFields.size(),
                playerFields.size());
    std::printf("%d entity deltas (%zu bits): %d written differently from jk2mp.exe, %d read back differently\n", pairs,
                entityBits, entityWriteDiffer, entityReadDiffer);
    std::printf("%d player deltas (%zu bits): %d written differently from jk2mp.exe, %d read back differently\n", pairs,
                playerBits, playerWriteDiffer, playerReadDiffer);
    std::printf("%d keyed user commands: %d written differently from jk2mp.exe, %d read back differently\n", pairs,
                commandWriteDiffer, commandReadDiffer);
    std::printf("%d client messages XORed: %d differ from jk2mp.exe's\n", xorPairs, xorDiffer);
    return entityWriteDiffer || entityReadDiffer || playerWriteDiffer || playerReadDiffer || commandWriteDiffer ||
                   commandReadDiffer || xorDiffer
               ? 1
               : 0;
}

const Commands kCommands(
    "idtech3",
    {
        {"net-run",
         "<game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot name]... | -join <host[:port]> [seconds] [-name name] "
         "| <map> -loopback [frames] [-set name value]... [-clientcmd text]... [-mapchange map]",
         "host a level as a server, with no player of its own, for others to join; or (-join) join a "
         "server and report what it sends; or (-loopback) both, a player joining in memory on fixed steps",
         cmdNetRun},
        {"huffman-check", "<jk2mp.exe>",
         "build Jedi Outcast's message code with jk2mp.exe's own routine in the x86 interpreter and compare "
         "every byte's code with Reluva's",
         cmdHuffmanCheck},
        {"delta-check", "<jk2mp.exe> [pairs]",
         "write and read entity and player-state deltas with jk2mp.exe's own routines in the x86 interpreter "
         "and with Reluva's, on made-up states, comparing the bits and the states read back",
         cmdDeltaCheck},
        {"net-find", "[host[:port]]",
         "look for servers on the network as the menus' list does, or ask one, printing every answer", cmdNetFind},
        {"net-master", "[master[:port]]...", "ask master servers for their lists, as the menus' Internet list does",
         cmdNetMaster},
    });

} // namespace
