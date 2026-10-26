// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"
#include "engines/idtech3/cvars.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// Finding servers, as the original's client does for its menus' server
// list: the "localservers" command asks the local network (getinfo,
// broadcast twice to each of the four server ports from 27960), and the
// servers that answer (infoResponse) make the local list; "ping <address>"
// asks one server, and the menus read the answer from the ping queue
// (32 slots: its address, the time it took, its info string).
class ServerBrowser {
public:
    static constexpr uint16_t kServerPort = 27960;
    static constexpr int kServerPorts = 4;
    static constexpr int kMaxPings = 32;
    static constexpr int kMaxLocalServers = 128;
    static constexpr size_t kMaxGlobalServers = 4096;

    // `socket`: where the requests go from and answers come to.
    explicit ServerBrowser(std::unique_ptr<net::UdpSocket> socket) : socket_(std::move(socket)) {}

    // Besides 255.255.255.255, the broadcast address of each network the
    // machine is on: the local search goes to each.
    void setBroadcasts(std::vector<uint32_t> addresses) { broadcasts_ = std::move(addresses); }
    // The two commands; `now` in ms.
    void localServers(uint64_t now);
    // `address`: a.b.c.d[:port] (host names already resolved by the caller).
    void ping(const net::Address& address, uint64_t now);
    // The Internet list (globalservers): `masters` asked for the servers
    // they know of that speak `protocol` (getservers, as Elite Force's
    // client asks: no game name), the list they answer replacing the last.
    static constexpr uint16_t kMasterPort = 27953;
    void globalServers(const std::vector<net::Address>& masters, int protocol, const std::string& keywords);
    int globalServerCount() const { return int(global_.size()); }
    std::string globalServerAddress(int n) const;
    // The addresses in a master's getserversResponse (after its four 0xff
    // bytes): each a backslash and 12 hexadecimal characters, the IPv4
    // address's 4 bytes and the port's 2 in network order, as Elite Force's
    // masters write them; empty if it isn't one.
    static std::vector<net::Address> serversFromResponse(const std::vector<uint8_t>& datagram);
    // Reads the answers that have come; call each frame.
    void update(uint64_t now);

    // The menus' calls (46, 47, 50 to 53). Pings not answered within
    // `maxPing` ms (cl_maxPing, at least 100) count as having taken that
    // long, so the menus give up on them.
    int localServerCount() const { return int(local_.size()); }
    std::string localServerAddress(int n) const;
    int pingQueueCount() const;
    void clearPing(int n);
    // The address (empty for a free slot) and the time: 0 while waiting.
    void getPing(int n, uint64_t now, int maxPing, std::string& address, int& time) const;
    std::string pingInfo(int n) const;

    // An infoResponse's info string, from the datagram after its four
    // 0xff bytes; empty if it isn't one.
    static std::string infoFromResponse(const std::vector<uint8_t>& datagram);

private:
    std::unique_ptr<net::UdpSocket> socket_;
    std::vector<net::Address> local_, global_;
    std::vector<net::Address> masters_; // asked last (their answers are taken)
    uint64_t scanStart_ = 0; // when the local network was last asked
    std::vector<uint32_t> broadcasts_;
    static constexpr uint64_t kScanMs = 5000;
    struct Ping {
        net::Address address;
        bool used = false;
        uint64_t start = 0;
        int time = 0; // 0 until answered
        std::string info;
    };
    Ping pings_[kMaxPings];
    void send(const net::Address& to, const std::string& text);
};

// The master servers (sv_master1 to sv_master5), created with defaults
// if not set: Raven's (long gone), then three of the community's.
std::vector<std::string> masterServerNames(CvarStore& cvars);

} // namespace reluva::idtech3
