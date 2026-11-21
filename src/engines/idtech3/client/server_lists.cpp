// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/server_lists.h"

#include "engines/idtech3/info_string.h"

#include "common/fs.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace reluva::idtech3 {

namespace {

int32_t number(const std::string &info, const char *key) {
    return int32_t(std::atoi(infoValue(info, key).c_str()));
}

} // namespace

std::vector<ServerLists::Server> *ServerLists::list(int32_t source) {
    return source >= 0 && source < int32_t(_lists.size()) ? &_lists[size_t(source)] : nullptr;
}

const std::vector<ServerLists::Server> *ServerLists::list(int32_t source) const {
    return source >= 0 && source < int32_t(_lists.size()) ? &_lists[size_t(source)] : nullptr;
}

void ServerLists::searchLocal(ServerBrowser &browser, uint64_t now) {
    // Slots keep their visibility, the servers in them start afresh.
    _lists[kLocal].clear();
    browser.localServers(now);
}

void ServerLists::searchGlobal(ServerBrowser &browser, const std::vector<net::Address> &masters, int protocol,
                               const std::string &keywords) {
    _lists[kGlobal].clear();
    _globalTaken = 0;
    _globalWaiting = !masters.empty();
    browser.globalServers(masters, protocol, keywords);
}

void ServerLists::update(const ServerBrowser &browser) {
    std::vector<Server> &local = _lists[kLocal];
    for (int i = int(local.size()); i < browser.localServerCount() && local.size() < kMaxOther; ++i) {
        Server s;
        net::parseAddress(browser.localServerAddress(i), defaultPort, s.address);
        local.push_back(s);
    }
    if (_globalWaiting && browser.globalServerCount() >= 0)
        _globalWaiting = false;
    std::vector<Server> &global = _lists[kGlobal];
    for (; int(_globalTaken) < browser.globalServerCount() && global.size() < kMaxGlobal; ++_globalTaken) {
        Server s;
        net::parseAddress(browser.globalServerAddress(int(_globalTaken)), defaultPort, s.address);
        global.push_back(s);
    }
}

int32_t ServerLists::count(int32_t source) const {
    if (source == kGlobal && _globalWaiting)
        return -1;
    const std::vector<Server> *l = list(source);
    return l ? int32_t(l->size()) : 0;
}

const ServerLists::Server *ServerLists::server(int32_t source, int32_t n) const {
    const std::vector<Server> *l = list(source);
    return l && n >= 0 && n < int32_t(l->size()) ? &(*l)[size_t(n)] : nullptr;
}

std::string ServerLists::info(const Server &s, bool jediPatched) {
    std::string out;
    setInfoValue(out, "hostname", s.hostName);
    setInfoValue(out, "mapname", s.mapName);
    setInfoValue(out, "clients", std::to_string(s.clients));
    setInfoValue(out, "sv_maxclients", std::to_string(s.maxClients));
    setInfoValue(out, "ping", std::to_string(s.ping));
    setInfoValue(out, "minping", std::to_string(s.minPing));
    setInfoValue(out, "maxping", std::to_string(s.maxPing));
    setInfoValue(out, "game", s.game);
    setInfoValue(out, "gametype", std::to_string(s.gameType));
    setInfoValue(out, "nettype", std::to_string(s.netType));
    setInfoValue(out, "addr", s.address.toString());
    if (jediPatched) {
        // (jk2mp.exe 1.03's LAN_GetServerInfo.)
        setInfoValue(out, "needpass", std::to_string(s.needPass));
        setInfoValue(out, "truejedi", std::to_string(s.trueJedi));
        setInfoValue(out, "wdisable", std::to_string(s.weaponDisable));
        setInfoValue(out, "fdisable", std::to_string(s.forceDisable));
        return out;
    }
    setInfoValue(out, "sv_allowAnonymous", std::to_string(s.allowAnonymous));
    return out;
}

void ServerLists::setInfo(const net::Address &address, const std::string &info, int32_t ping) {
    // Every list the server is in.
    for (std::vector<Server> &l : _lists)
        for (Server &s : l) {
            if (s.address != address)
                continue;
            if (!info.empty()) {
                s.clients = number(info, "clients");
                s.hostName = infoValue(info, "hostname");
                s.mapName = infoValue(info, "mapname");
                s.maxClients = number(info, "sv_maxclients");
                s.game = infoValue(info, "game");
                s.gameType = number(info, "gametype");
                s.netType = 1;
                s.minPing = number(info, "minping");
                s.maxPing = number(info, "maxping");
                s.allowAnonymous = number(info, "sv_allowAnonymous");
                s.needPass = number(info, "needpass");
                s.trueJedi = number(info, "truejedi");
                s.weaponDisable = number(info, "wdisable");
                s.forceDisable = number(info, "fdisable");
            }
            s.ping = ping;
        }
}

bool ServerLists::updateVisiblePings(ServerBrowser &browser, int32_t source, uint64_t now, int maxPing) {
    std::vector<Server> *l = list(source);
    if (!l)
        return false;
    bool busy = false;
    int slots = browser.pingQueueCount();
    for (size_t n = 0; n < l->size(); ++n) {
        Server &s = (*l)[n];
        if (!_visible[size_t(source)][n] || s.ping != -1 || slots >= ServerBrowser::kMaxPings)
            continue;
        bool queued = false;
        std::string address;
        int time = 0;
        for (int i = 0; i < ServerBrowser::kMaxPings && !queued; ++i) {
            browser.getPing(i, now, maxPing, address, time);
            queued = address == s.address.toString();
        }
        if (!queued) {
            browser.ping(s.address, now);
            ++slots;
            busy = true;
        }
    }
    busy = busy || slots > 0;
    // The answers, and those given up on: an unanswered one counts as no
    // ping (0), so it isn't asked again until the pings are reset.
    for (int i = 0; i < ServerBrowser::kMaxPings; ++i) {
        std::string address;
        int time = 0;
        browser.getPing(i, now, maxPing, address, time);
        if (address.empty())
            continue;
        const std::string info = browser.pingInfo(i);
        net::Address a;
        net::parseAddress(address, defaultPort, a);
        setInfo(a, info, info.empty() ? 0 : time);
        if (time) {
            browser.clearPing(i);
            busy = true;
        }
    }
    return busy;
}

void ServerLists::markVisible(int32_t source, int32_t n, bool visible) {
    if (!list(source))
        return;
    std::vector<bool> &v = _visible[size_t(source)];
    if (n == -1)
        std::fill(v.begin(), v.end(), visible);
    else if (n >= 0 && n < int32_t(v.size()))
        v[size_t(n)] = visible;
}

bool ServerLists::visible(int32_t source, int32_t n) const {
    return list(source) && n >= 0 && n < int32_t(_visible[size_t(source)].size()) &&
           _visible[size_t(source)][size_t(n)];
}

void ServerLists::resetPings(int32_t source) {
    if (std::vector<Server> *l = list(source))
        for (Server &s : *l)
            s.ping = -1;
}

int32_t ServerLists::add(int32_t source, const std::string &name, const std::string &address) {
    std::vector<Server> *l = list(source);
    if (!l || l->size() >= (source == kGlobal ? kMaxGlobal : kMaxOther))
        return -1;
    Server s;
    net::parseAddress(address, defaultPort, s.address);
    if (std::any_of(l->begin(), l->end(), [&](const Server &o) { return o.address == s.address; }))
        return 0;
    s.hostName = name;
    _visible[size_t(source)][l->size()] = true;
    l->push_back(s);
    return 1;
}

void ServerLists::remove(int32_t source, const std::string &address) {
    std::vector<Server> *l = list(source);
    net::Address a;
    if (!l || !net::parseAddress(address, defaultPort, a))
        return;
    const auto it = std::find_if(l->begin(), l->end(), [&](const Server &s) { return s.address == a; });
    if (it != l->end()) {
        // The servers after it move up a slot, their visibility with them.
        std::vector<bool> &v = _visible[size_t(source)];
        v.erase(v.begin() + (it - l->begin()));
        v.push_back(false);
        l->erase(it);
    }
}

int32_t ServerLists::compare(int32_t source, int32_t sortKey, bool descending, int32_t a, int32_t b) const {
    const Server *s1 = server(source, a);
    const Server *s2 = server(source, b);
    if (!s1 || !s2)
        return 0;
    auto order = [](auto x, auto y) { return x < y ? -1 : y < x ? 1 : 0; };
    int32_t r = 0;
    switch (sortKey) {
    case 0:
        r = order(fs::toLower(s1->hostName), fs::toLower(s2->hostName));
        break;
    case 1:
        r = order(fs::toLower(s1->mapName), fs::toLower(s2->mapName));
        break;
    case 2:
        r = order(s1->clients, s2->clients);
        break;
    case 3:
        r = order(s1->gameType, s2->gameType);
        break;
    case 4:
        r = order(s1->ping, s2->ping);
        break;
    default:
        break;
    }
    return descending ? -r : r;
}

void ServerLists::load(const std::filesystem::path &file) {
    _lists[kFavorites].clear();
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t space = line.find(' ');
        if (!line.empty())
            add(kFavorites, space == std::string::npos ? std::string() : line.substr(space + 1), line.substr(0, space));
    }
}

void ServerLists::save(const std::filesystem::path &file) const {
    std::ofstream out(file);
    for (const Server &s : _lists[kFavorites])
        out << s.address.toString() << ' ' << s.hostName << '\n';
}

namespace {

// serverInfo_t: netadr_t (its type, 4 for an IPv4 address, the address's
// bytes, IPX's ten unused, the port in network order), the names 32 bytes
// each, then integers; 1.02a's sv_allowAnonymous at 0x94, the patches'
// four flags there (jk2mp.exe 1.02a's 0x40d236 on, 1.04's 0x40d944 on).
constexpr size_t kEntry = 0x98;
constexpr size_t kPatchedEntry = 0xa4;
constexpr int32_t kAddressIp = 4;
constexpr size_t kMplayerSlots = 128; // a third list, always empty here

void putInt(std::vector<uint8_t> &b, size_t at, int32_t v) {
    for (size_t i = 0; i < 4; ++i)
        b[at + i] = uint8_t(uint32_t(v) >> (8 * i));
}

int32_t getInt(const std::vector<uint8_t> &b, size_t at) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i)
        v |= uint32_t(b[at + i]) << (8 * i);
    return int32_t(v);
}

void putName(std::vector<uint8_t> &b, size_t at, const std::string &s) {
    std::copy_n(s.begin(), std::min<size_t>(s.size(), 31), b.begin() + std::ptrdiff_t(at));
}

std::string getName(const std::vector<uint8_t> &b, size_t at) {
    size_t n = 0;
    while (n < 31 && b[at + n])
        ++n;
    return {reinterpret_cast<const char *>(b.data() + at), n};
}

} // namespace

std::vector<uint8_t> ServerLists::cacheEntries(const std::vector<Server> &servers, const std::vector<bool> &visible,
                                               size_t slots, bool jediPatched) {
    const size_t size = jediPatched ? kPatchedEntry : kEntry;
    std::vector<uint8_t> out(slots * size);
    for (size_t i = 0; i < slots; ++i) {
        const size_t at = i * size;
        if (i < visible.size())
            putInt(out, at + 0x90, visible[i] ? 1 : 0);
        if (i >= servers.size())
            continue;
        const Server &s = servers[i];
        putInt(out, at, kAddressIp);
        for (size_t k = 0; k < 4; ++k)
            out[at + 4 + k] = uint8_t(s.address.ip >> (24 - 8 * k));
        out[at + 0x12] = uint8_t(s.address.port >> 8);
        out[at + 0x13] = uint8_t(s.address.port);
        putName(out, at + 0x14, s.hostName);
        putName(out, at + 0x34, s.mapName);
        putName(out, at + 0x54, s.game);
        const int32_t ints[] = {s.netType, s.gameType, s.clients, s.maxClients, s.minPing, s.maxPing, s.ping};
        for (size_t k = 0; k < std::size(ints); ++k)
            putInt(out, at + 0x74 + 4 * k, ints[k]);
        if (jediPatched) {
            putInt(out, at + 0x94, s.needPass);
            putInt(out, at + 0x98, s.trueJedi);
            putInt(out, at + 0x9c, s.weaponDisable);
            putInt(out, at + 0xa0, s.forceDisable);
        } else {
            putInt(out, at + 0x94, s.allowAnonymous);
        }
    }
    return out;
}

void ServerLists::fromCacheEntries(const std::vector<uint8_t> &bytes, size_t count, bool jediPatched,
                                   std::vector<Server> &servers, std::vector<bool> &visible) {
    const size_t size = jediPatched ? kPatchedEntry : kEntry;
    servers.clear();
    for (size_t i = 0; i < visible.size() && (i + 1) * size <= bytes.size(); ++i)
        visible[i] = getInt(bytes, i * size + 0x90) != 0;
    for (size_t i = 0; i < count && (i + 1) * size <= bytes.size(); ++i) {
        const size_t at = i * size;
        Server s;
        s.address.ip = uint32_t(bytes[at + 4]) << 24 | uint32_t(bytes[at + 5]) << 16 | uint32_t(bytes[at + 6]) << 8 |
                       bytes[at + 7];
        s.address.port = uint16_t(bytes[at + 0x12] << 8 | bytes[at + 0x13]);
        s.hostName = getName(bytes, at + 0x14);
        s.mapName = getName(bytes, at + 0x34);
        s.game = getName(bytes, at + 0x54);
        int32_t *ints[] = {&s.netType, &s.gameType, &s.clients, &s.maxClients, &s.minPing, &s.maxPing, &s.ping};
        for (size_t k = 0; k < std::size(ints); ++k)
            *ints[k] = getInt(bytes, at + 0x74 + 4 * k);
        if (jediPatched) {
            s.needPass = getInt(bytes, at + 0x94);
            s.trueJedi = getInt(bytes, at + 0x98);
            s.weaponDisable = getInt(bytes, at + 0x9c);
            s.forceDisable = getInt(bytes, at + 0xa0);
        } else {
            s.allowAnonymous = getInt(bytes, at + 0x94);
        }
        servers.push_back(std::move(s));
    }
}

void ServerLists::saveCache(const std::filesystem::path &file, bool jediPatched) const {
    // The three counts, the arrays' size, then the arrays whole.
    const std::vector<uint8_t> global = cacheEntries(_lists[kGlobal], _visible[kGlobal], kMaxGlobal, jediPatched);
    const std::vector<uint8_t> favorites =
        cacheEntries(_lists[kFavorites], _visible[kFavorites], kMaxOther, jediPatched);
    const std::vector<uint8_t> mplayer = cacheEntries({}, {}, kMplayerSlots, jediPatched);
    std::vector<uint8_t> head(16);
    putInt(head, 0, int32_t(_lists[kGlobal].size()));
    putInt(head, 4, int32_t(_lists[kFavorites].size()));
    putInt(head, 8, 0);
    putInt(head, 12, int32_t(global.size() + favorites.size() + mplayer.size()));
    std::ofstream out(file, std::ios::binary);
    for (const std::vector<uint8_t> *part :
         std::array<const std::vector<uint8_t> *, 4>{&head, &global, &favorites, &mplayer})
        out.write(reinterpret_cast<const char *>(part->data()), std::streamsize(part->size()));
}

void ServerLists::loadCache(const std::filesystem::path &file, bool jediPatched) {
    const size_t size = jediPatched ? kPatchedEntry : kEntry;
    const size_t globalBytes = kMaxGlobal * size;
    const size_t otherBytes = kMaxOther * size;
    _lists[kGlobal].clear();
    _lists[kFavorites].clear();
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 16 || size_t(getInt(data, 12)) != globalBytes + 2 * otherBytes ||
        data.size() < 16 + globalBytes + 2 * otherBytes)
        return;
    const auto counted = [&](size_t at, size_t most) {
        return std::min<size_t>(size_t(std::max(0, getInt(data, at))), most);
    };
    const std::vector<uint8_t> global(data.begin() + 16, data.begin() + std::ptrdiff_t(16 + globalBytes));
    const std::vector<uint8_t> favorites(data.begin() + std::ptrdiff_t(16 + globalBytes),
                                         data.begin() + std::ptrdiff_t(16 + globalBytes + otherBytes));
    fromCacheEntries(global, counted(0, kMaxGlobal), jediPatched, _lists[kGlobal], _visible[kGlobal]);
    fromCacheEntries(favorites, counted(4, kMaxOther), jediPatched, _lists[kFavorites], _visible[kFavorites]);
}

} // namespace reluva::idtech3
