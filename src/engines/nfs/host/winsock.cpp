// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WSOCK32 for nfs3.exe: the shared stand-in (x86::Winsock) on the network
// the host is given (Options::sockets), with this engine's own: an address
// for each copy on one machine, broadcasts sent where it's told, a log of
// the traffic and packets lost on purpose, to see the game's resends.

#include "engines/nfs/host/game_host.h"

#include <cstdio>

namespace reluva::nfs {

void GameHost::logTraffic(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                          bool dropped) {
    if (!_netLog || !size)
        return;
    const uint32_t ms = _process->milliseconds ? _process->milliseconds() : 0;
    std::fprintf(_netLog, "%u %c %u %s %zu ", ms,
                 dropped ? 'x'
                 : sent  ? '>'
                         : '<',
                 handle, peer.toString().c_str(), size);
    for (size_t i = 0; i < size; ++i)
        std::fprintf(_netLog, "%02x", data[i]);
    std::fprintf(_netLog, "\n");
    std::fflush(_netLog);
}

bool GameHost::dropOutgoing() {
    const uint32_t ms = _process->milliseconds ? _process->milliseconds() : 0;
    if (!_options.netDrop || ms < _options.netDropFrom || (_options.netDropTo && ms >= _options.netDropTo))
        return false;
    return ++_netSent % _options.netDrop == 0;
}

void GameHost::addNetwork() {
    if (!_options.netLog.empty() && !_netLog)
        _netLog = std::fopen(_options.netLog.c_str(), "w");
    x86::Winsock::Options o;
    o.sockets = _options.sockets;
    o.localAddress = _options.netAddress;
    o.broadcastTo = _options.broadcastTo;
    o.dropOutgoing = [this] { return dropOutgoing(); };
    o.traffic = [this](bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                       bool dropped) { logTraffic(sent, handle, peer, data, size, dropped); };
    o.log = [this](const std::string &line) {
        if (_options.callLog)
            say("  winsock: " + line);
    };
    // As the engine's own, so a LoadLibraryA of WSOCK32 finds them.
    o.provide = [this](const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn) {
        provide(dll, name, args, std::move(fn));
    };
    _winsock = std::make_unique<x86::Winsock>(*_process, std::move(o));
}

} // namespace reluva::nfs
