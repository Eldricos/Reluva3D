// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/server_lists.h"

#include "engines/idtech3/info_string.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

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
        net::parseAddress(browser.localServerAddress(i), ServerBrowser::kServerPort, s.address);
        local.push_back(s);
    }
    if (_globalWaiting && browser.globalServerCount() >= 0)
        _globalWaiting = false;
    std::vector<Server> &global = _lists[kGlobal];
    for (; int(_globalTaken) < browser.globalServerCount() && global.size() < kMaxGlobal; ++_globalTaken) {
        Server s;
        net::parseAddress(browser.globalServerAddress(int(_globalTaken)), ServerBrowser::kServerPort, s.address);
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

std::string ServerLists::info(const Server &s) {
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
        net::parseAddress(address, ServerBrowser::kServerPort, a);
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
    net::parseAddress(address, ServerBrowser::kServerPort, s.address);
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
    if (!l || !net::parseAddress(address, ServerBrowser::kServerPort, a))
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

} // namespace reluva::idtech3
