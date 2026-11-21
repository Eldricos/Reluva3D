// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/server_browser.h"

#include "common/net.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * The server lists Team Arena's engines keep for their menus (Jedi
 * Outcast's among them): one per source, each server with what its last
 * answer said, which the menus page through by number, mark visible and
 * have pinged. The searching and pinging are ServerBrowser's.
 */
class ServerLists {
public:
    // The sources, as the menus number them (3, the old mplayer list, is
    // always empty).
    enum Source : int32_t { kLocal = 0, kGlobal = 1, kFavorites = 2 };
    static constexpr size_t kMaxOther = 128;   // local and favourite servers
    static constexpr size_t kMaxGlobal = 2048; // Internet servers

    // The port of an address given without one (the game's servers').
    uint16_t defaultPort = 27960;

    /** One server, as its last infoResponse described it. */
    struct Server {
        net::Address address;
        std::string hostName;
        std::string mapName;
        std::string game;
        int32_t netType = 0; // 1: UDP
        int32_t gameType = 0;
        int32_t clients = 0;
        int32_t maxClients = 0;
        int32_t minPing = 0;
        int32_t maxPing = 0;
        int32_t ping = -1; // -1: not asked yet; 0: no answer (or not yet)
        int32_t allowAnonymous = 0;
        // Jedi Outcast 1.03's: a password needed, Jedi versus mercenaries,
        // the weapons and Force powers turned off (bits).
        int32_t needPass = 0;
        int32_t trueJedi = 0;
        int32_t weaponDisable = 0;
        int32_t forceDisable = 0;
    };

    // The local search (the "localservers" command): the list starts again,
    // each server keeping its slot's visibility, as the original's does.
    void searchLocal(ServerBrowser &browser, uint64_t now);
    // The Internet list ("globalservers"): asked of `masters`.
    void searchGlobal(ServerBrowser &browser, const std::vector<net::Address> &masters, int protocol,
                      const std::string &keywords);
    // Takes in what the browser has found since; call each frame.
    void update(const ServerBrowser &browser);

    // -1 for the Internet list while the masters haven't answered.
    int32_t count(int32_t source) const;
    const Server *server(int32_t source, int32_t n) const;
    // What LAN_GetServerInfo gives the menus: the server's fields as an
    // info string (hostname, mapname, clients, sv_maxclients, ping...).
    // `jediPatched`: as Jedi Outcast 1.03's engine gives it, these four in
    // place of sv_allowAnonymous.
    static std::string info(const Server &s, bool jediPatched = false);
    // Pings every visible server not asked yet, and takes in the answers
    // (and the servers given up on, after `maxPing` ms, cl_maxPing);
    // true while any is waiting or has just answered.
    bool updateVisiblePings(ServerBrowser &browser, int32_t source, uint64_t now, int maxPing);
    // Whether the menus show slot `n` (and want its ping). Kept by slot,
    // whether a server is in it or not, as the original's lists are fixed
    // arrays: the menus mark every slot (-1 for `n`) before a search fills
    // them.
    void markVisible(int32_t source, int32_t n, bool visible);
    bool visible(int32_t source, int32_t n) const;
    void resetPings(int32_t source);
    // 1 added, 0 already there, -1 the list is full.
    int32_t add(int32_t source, const std::string &name, const std::string &address);
    void remove(int32_t source, const std::string &address);
    // Two servers by `sortKey` (0 name, 1 map, 2 players, 3 game type,
    // 4 ping), the other way round if `descending`.
    int32_t compare(int32_t source, int32_t sortKey, bool descending, int32_t a, int32_t b) const;

    // The favourites, kept between runs: one per line, address then name.
    void load(const std::filesystem::path &file);
    void save(const std::filesystem::path &file) const;

    // Jedi Outcast's servercache.dat (LAN_LoadCachedServers, jk2mp.exe's
    // 0x40f990; LAN_SaveServersToCache, 0x40faa0): the Internet list and
    // the favourites, entries as the engine keeps them, 1.03's and 1.04's
    // (`jediPatched`) larger. Loading a file of another size or none
    // empties them, as the original's does.
    void loadCache(const std::filesystem::path &file, bool jediPatched);
    void saveCache(const std::filesystem::path &file, bool jediPatched) const;
    // One list as the cache keeps it (`slots` entries, those past the
    // list's zero), and back.
    static std::vector<uint8_t> cacheEntries(const std::vector<Server> &servers, const std::vector<bool> &visible,
                                             size_t slots, bool jediPatched);
    static void fromCacheEntries(const std::vector<uint8_t> &bytes, size_t count, bool jediPatched,
                                 std::vector<Server> &servers, std::vector<bool> &visible);

private:
    std::vector<Server> *list(int32_t source);
    const std::vector<Server> *list(int32_t source) const;
    void setInfo(const net::Address &address, const std::string &info, int32_t ping);

    std::array<std::vector<Server>, 3> _lists;
    std::array<std::vector<bool>, 3> _visible{std::vector<bool>(kMaxOther), std::vector<bool>(kMaxGlobal),
                                              std::vector<bool>(kMaxOther)};
    bool _globalWaiting = false;
    size_t _globalTaken = 0; // the browser's Internet addresses already listed
};

} // namespace reluva::idtech3
