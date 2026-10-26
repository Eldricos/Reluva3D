// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/server_browser.h"

#include <algorithm>
#include <cstring>

namespace reluva::idtech3 {

void ServerBrowser::send(const net::Address& to, const std::string& text) {
    std::vector<uint8_t> data(4, 0xff);
    data.insert(data.end(), text.begin(), text.end());
    socket_->send(to, data);
}

void ServerBrowser::localServers(uint64_t now) {
    local_.clear();
    scanStart_ = now;
    // Twice, in case one is lost; the 'xxx' is a challenge servers echo.
    // To 255.255.255.255, as the original, which leaves by one network
    // only, and to each network's own broadcast address (a virtual
    // machine's network...).
    for (int i = 0; i < 2; ++i)
        for (int p = 0; p < kServerPorts; ++p) {
            send(net::Address::broadcast(uint16_t(kServerPort + p)), "getinfo xxx");
            for (uint32_t b : broadcasts_)
                send({b, uint16_t(kServerPort + p)}, "getinfo xxx");
        }
}

void ServerBrowser::globalServers(const std::vector<net::Address>& masters, int protocol, const std::string& keywords) {
    global_.clear();
    masters_ = masters;
    // Raven's master listed every server; the community's (dpmaster) leave
    // out empty and full ones unless asked for them, so they're asked.
    std::string words = keywords;
    for (const char* w : {"full", "empty"})
        if ((" " + words + " ").find(std::string(" ") + w + " ") == std::string::npos)
            words += (words.empty() ? "" : " ") + std::string(w);
    for (const net::Address& m : masters)
        send(m, "getservers " + std::to_string(protocol) + " " + words);
}

std::string ServerBrowser::globalServerAddress(int n) const {
    return n >= 0 && n < int(global_.size()) ? global_[size_t(n)].toString() : std::string();
}

std::vector<net::Address> ServerBrowser::serversFromResponse(const std::vector<uint8_t>& d) {
    static const char kWord[] = "getserversResponse";
    const size_t wordLength = sizeof kWord - 1;
    std::vector<net::Address> out;
    if (d.size() < 4 + wordLength || std::memcmp(d.data(), "\xff\xff\xff\xff", 4) != 0 ||
        std::memcmp(d.data() + 4, kWord, wordLength) != 0)
        return out;
    auto hex = [](uint8_t c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    size_t i = 4 + wordLength;
    while (i < d.size() && d[i] != '\\')
        ++i;
    // Entries until one doesn't parse (the end, however the master marks it).
    while (i + 13 <= d.size() && d[i] == '\\') {
        uint8_t bytes[6];
        bool ok = true;
        for (int b = 0; b < 6 && ok; ++b) {
            const int hi = hex(d[i + 1 + 2 * size_t(b)]), lo = hex(d[i + 2 + 2 * size_t(b)]);
            ok = hi >= 0 && lo >= 0;
            bytes[b] = uint8_t(hi << 4 | lo);
        }
        if (!ok)
            break;
        net::Address a{uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3],
                       uint16_t(bytes[4] << 8 | bytes[5])};
        if (a.ip && a.port)
            out.push_back(a);
        i += 13;
    }
    return out;
}

void ServerBrowser::ping(const net::Address& address, uint64_t now) {
    // A free slot, else one answered or given up on, else the oldest.
    Ping* slot = nullptr;
    for (Ping& p : pings_)
        if (!p.used) {
            slot = &p;
            break;
        }
    if (!slot)
        for (Ping& p : pings_)
            if (p.time || now - p.start >= 500) {
                slot = &p;
                break;
            }
    if (!slot)
        slot = &*std::min_element(std::begin(pings_), std::end(pings_),
                                  [](const Ping& a, const Ping& b) { return a.start < b.start; });
    *slot = Ping{address, true, now, 0, {}};
    send(address, "getinfo xxx");
}

std::string ServerBrowser::infoFromResponse(const std::vector<uint8_t>& datagram) {
    static const char kWord[] = "infoResponse";
    const size_t wordLength = sizeof kWord - 1;
    if (datagram.size() < 4 + wordLength || std::memcmp(datagram.data(), "\xff\xff\xff\xff", 4) != 0 ||
        std::memcmp(datagram.data() + 4, kWord, wordLength) != 0)
        return {};
    std::string rest(datagram.begin() + 4 + std::ptrdiff_t(wordLength), datagram.end());
    // Elite Force's servers quote it on the same line; Quake III's put it
    // on the next.
    if (const size_t open = rest.find('"'); open != std::string::npos) {
        const size_t close = rest.find('"', open + 1);
        return rest.substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1);
    }
    const size_t start = rest.find_first_not_of(" \n");
    if (start == std::string::npos)
        return {};
    rest = rest.substr(start);
    if (const size_t end = rest.find_first_of("\n\0", 0, 2); end != std::string::npos)
        rest.resize(end);
    return rest;
}

void ServerBrowser::update(uint64_t now) {
    net::Address from;
    std::vector<uint8_t> data;
    while (socket_->receive(from, data)) {
        if (std::find(masters_.begin(), masters_.end(), from) != masters_.end()) {
            for (const net::Address& a : serversFromResponse(data))
                if (std::find(global_.begin(), global_.end(), a) == global_.end() && global_.size() < kMaxGlobalServers)
                    global_.push_back(a);
            continue;
        }
        const std::string info = infoFromResponse(data);
        if (info.empty())
            continue;
        for (Ping& p : pings_)
            if (p.used && !p.time && p.address == from) {
                p.time = std::max(1, int(now - p.start));
                p.info = info;
            }
        // Answers while the local network is being asked make its list
        // (whatever their address: not every network uses the private ranges).
        if (scanStart_ && now - scanStart_ < kScanMs && std::find(local_.begin(), local_.end(), from) == local_.end() &&
            int(local_.size()) < kMaxLocalServers)
            local_.push_back(from);
    }
}

std::string ServerBrowser::localServerAddress(int n) const {
    return n >= 0 && n < int(local_.size()) ? local_[size_t(n)].toString() : std::string();
}

int ServerBrowser::pingQueueCount() const {
    return int(std::count_if(std::begin(pings_), std::end(pings_), [](const Ping& p) { return p.used; }));
}

void ServerBrowser::clearPing(int n) {
    if (n >= 0 && n < kMaxPings)
        pings_[n].used = false;
}

void ServerBrowser::getPing(int n, uint64_t now, int maxPing, std::string& address, int& time) const {
    address.clear();
    time = 0;
    if (n < 0 || n >= kMaxPings || !pings_[n].used)
        return;
    const Ping& p = pings_[n];
    address = p.address.toString();
    time = p.time;
    if (!time && int(now - p.start) >= std::max(maxPing, 100))
        time = int(now - p.start);
}

std::string ServerBrowser::pingInfo(int n) const {
    return n >= 0 && n < kMaxPings && pings_[n].used ? pings_[n].info : std::string();
}

std::vector<std::string> masterServerNames(CvarStore& cvars) {
    // Raven's first, as the original; then the community's (those the GPL
    // engine and community guides list, October 2026).
    cvars.create("sv_master1", "master.stef1.ravensoft.com", 0);
    cvars.create("sv_master2", "efmaster.tjps.eu", CvarStore::Archive);
    cvars.create("sv_master3", "master.stvef.org", CvarStore::Archive);
    cvars.create("sv_master4", "master.stef1.daggolin.de", CvarStore::Archive);
    std::vector<std::string> out;
    for (int i = 1; i <= 5; ++i) {
        const std::string name = cvars.string("sv_master" + std::to_string(i));
        if (!name.empty())
            out.push_back(name);
    }
    return out;
}

} // namespace reluva::idtech3
