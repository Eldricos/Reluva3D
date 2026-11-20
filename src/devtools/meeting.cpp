// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool meet-server: a meeting server for network play, for any
// engine's games that use one (common/meeting.h), run until stopped.

#include "backends/networking/sockets/socket_net.h"
#include "common/meeting.h"
#include "devtools/tool.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

using namespace reluva::tool;

uint64_t nowMs() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

int cmdMeetServer(int argc, char **argv) {
    long port = reluva::net::kMeetingPort;
    long seconds = 0; // 0: until stopped
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-for") == 0 && i + 1 < argc)
            seconds = std::strtol(argv[++i], nullptr, 10);
        else
            port = std::strtol(argv[i], nullptr, 10);
    }
    if (port <= 0 || port > 65535 || seconds < 0) {
        std::fprintf(stderr, "usage: reluva-tool meet-server [port] [-for seconds]\n");
        return 2;
    }
    std::string err;
    auto socket = reluva::net::openSystemUdp(uint16_t(port), err);
    if (!socket) {
        std::fprintf(stderr, "meet-server: %s\n", err.c_str());
        return 1;
    }
    reluva::net::MeetingServer server(std::move(socket));
    std::printf("meeting server on UDP port %ld: players need to reach this machine on it (pass it on at the "
                "router if this machine is behind one); Ctrl+C stops it\n",
                port);
    std::fflush(stdout);
    const uint64_t start = nowMs();
    size_t sessions = 0;
    size_t connections = 0;
    while (!seconds || nowMs() < start + uint64_t(seconds) * 1000) {
        server.update(nowMs());
        // What's listed and connected, whenever it changes.
        if (server.sessions().size() != sessions || server.connections().size() != connections) {
            sessions = server.sessions().size();
            connections = server.connections().size();
            std::printf("%zu session%s, %zu connection%s\n", sessions, sessions == 1 ? "" : "s", connections,
                        connections == 1 ? "" : "s");
            for (const auto &[id, s] : server.sessions())
                std::printf("  %s: \"%s\" hosted from %s\n", s.game.c_str(), s.name.c_str(), s.host.toString().c_str());
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 0;
}

const Commands kCommands("", {
                                 {"meet-server", "[port] [-for seconds]",
                                  "run a meeting server for network play (UDP port 28530 unless given): players who "
                                  "can't reach each other meet through it, and it passes on what their routers won't",
                                  cmdMeetServer},
                             });

} // namespace
