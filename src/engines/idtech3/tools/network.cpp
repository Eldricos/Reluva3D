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
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/client/server_browser.h"
#include "engines/idtech3/jk2/net_fields.h"
#include "emulation/x86/cpu.h"

#include "common/pe.h"

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

// Network play from a terminal (protocol 24, docs/engines/idtech3/games/stvef/systems/network.md):
// hosting a level as a server with no player of its own, for others to
// join for `seconds`; or, with -join, joining a server and reporting
// what it sends while walking forward.
// A player joining the server in memory on fixed 50 ms steps, running
// forward once in the level: what each side reports, for a record that
// repeats.
int loopbackPlayer(reluva::idtech3::eliteforce::GameHost &game, reluva::idtech3::eliteforce::NetServer &server,
                   reluva::net::LoopbackNetwork &memory, const reluva::idtech3::eliteforce::NetProtocol &protocol,
                   const std::string &userinfo, const std::vector<std::string> &clientCommands, int frames) {
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
    for (int frame = 0; frame < frames; ++frame) {
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
        if (client.takeNewGamestate()) {
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
    return snapshots && client.state() == ef::NetClient::State::kActive ? 0 : 1;
}

int cmdNetRun(int argc, char **argv) {
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    if (argc < 4) {
        std::fprintf(
            stderr,
            "usage: reluva-tool idtech3 net-run <game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot <name>]...\n"
            "       reluva-tool idtech3 net-run <game folder> <map> -loopback [frames] [-set name value]... "
            "[-clientcmd text]...\n"
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
    for (int i = join ? 5 : 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-loopback") == 0)
            loopback = true;
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
    const q3::GameInfo &info = gameOfFolder(argv[2]);
    const q3::GameCode code =
        info.code == q3::GameCode::kJediOutcast ? q3::GameCode::kJediOutcast : q3::GameCode::kEliteForce;
    const ef::NetProtocol &protocol = ef::netProtocol(code);
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
    ef::GameHost game(set, cvars, code);
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
    if (loopback)
        return loopbackPlayer(game, server, *memory, protocol, userinfoFor("Remote"), clientCommands,
                              seconds == 60 ? 200 : seconds);
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

const Commands kCommands(
    "idtech3",
    {
        {"net-run",
         "<game folder> <map> [seconds] [-port N] [-public] [-pure] [-addbot name]... | -join <host[:port]> [seconds] [-name name] "
         "| <map> -loopback [frames] [-set name value]... [-clientcmd text]...",
         "host a level as a server, with no player of its own, for others to join; or (-join) join a "
         "server and report what it sends; or (-loopback) both, a player joining in memory on fixed steps",
         cmdNetRun},
        {"huffman-check", "<jk2mp.exe>",
         "build Jedi Outcast's message code with jk2mp.exe's own routine in the x86 interpreter and compare "
         "every byte's code with Reluva's",
         cmdHuffmanCheck},
        {"net-find", "[host[:port]]",
         "look for servers on the network as the menus' list does, or ask one, printing every answer", cmdNetFind},
        {"net-master", "[master[:port]]...", "ask master servers for their lists, as the menus' Internet list does",
         cmdNetMaster},
    });

} // namespace
