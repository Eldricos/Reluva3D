// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/players.h"

#include <fstream>
#include <sstream>

namespace reluva::tpw {

// A line a value: "player <slot>" starts a player, then "<key> <value>"
// lines; "world <theme> <open> <won>" for each world. Lines it doesn't
// know are passed over, so a later Reluva can add to it.

PlayerSlots readPlayers(const std::filesystem::path &folder, const PlayerFile &fresh) {
    PlayerSlots players;
    for (PlayerFile &p : players) {
        p = fresh;
        p.name.clear();
    }
    if (folder.empty())
        return players;
    std::ifstream in(folder / kPlayersFile, std::ios::binary);
    PlayerFile *p = nullptr;
    for (std::string line; in && std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "player") {
            int slot = -1;
            words >> slot;
            p = slot >= 0 && slot < kPlayerSlots ? &players[size_t(slot)] : nullptr;
        } else if (!p) {
            continue;
        } else if (key == "name") {
            // The rest of the line, spaces and all.
            p->name = line.size() > 5 ? line.substr(5) : std::string();
        } else if (key == "rank") {
            words >> p->rank;
        } else if (key == "golden-tickets") {
            words >> p->goldenTickets;
        } else if (key == "challenges-won") {
            p->challengesWon.clear();
            for (int n; words >> n;)
                p->challengesWon.push_back(n);
        } else if (key == "challenges-failed") {
            words >> p->challengesFailed;
        } else if (key == "rides-activated") {
            p->ridesActivated.clear();
            for (int n; words >> n;)
                p->ridesActivated.push_back(n);
        } else if (key == "shares-owned") {
            words >> p->sharesOwned;
        } else if (key == "share-price") {
            words >> p->sharePrice;
        } else if (key == "objective") {
            words >> p->objective >> p->objectiveTheme;
        } else if (key == "world") {
            int theme = -1;
            int open = 0;
            int won = 0;
            words >> theme >> open >> won;
            for (PlayerFile::World &w : p->worlds)
                if (w.theme == theme) {
                    w.open = open != 0;
                    w.won = won;
                }
        }
    }
    return players;
}

Error writePlayers(const std::filesystem::path &folder, const PlayerSlots &players) {
    if (folder.empty())
        return Error::fail("No save folder");
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::ofstream out(folder / kPlayersFile, std::ios::binary);
    out << "# Reluva's SimCoaster players: docs/engines/tpw/games/simcoaster/systems/players.md\n";
    for (size_t slot = 0; slot < players.size(); ++slot) {
        const PlayerFile &p = players[slot];
        if (!p.exists())
            continue;
        out << "player " << slot << '\n';
        out << "name " << p.name << '\n';
        out << "rank " << p.rank << '\n';
        out << "golden-tickets " << p.goldenTickets << '\n';
        out << "challenges-won";
        for (int n : p.challengesWon)
            out << ' ' << n;
        out << '\n';
        out << "challenges-failed " << p.challengesFailed << '\n';
        out << "rides-activated";
        for (int n : p.ridesActivated)
            out << ' ' << n;
        out << '\n';
        out << "shares-owned " << p.sharesOwned << '\n';
        out << "share-price " << p.sharePrice << '\n';
        for (const PlayerFile::World &w : p.worlds)
            out << "world " << w.theme << ' ' << (w.open ? 1 : 0) << ' ' << w.won << '\n';
        out << "objective " << p.objective << ' ' << p.objectiveTheme << '\n';
    }
    return out ? Error::success() : Error::fail("Can't write " + (folder / kPlayersFile).string());
}

} // namespace reluva::tpw
