// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/games.h"

namespace reluva::tpw {

const std::vector<GameInfo> &games() {
    static const std::vector<GameInfo> kList = {
        {"simcoaster", "SimCoaster"},
    };
    return kList;
}

const GameInfo *findGame(std::string_view gameId) {
    for (const GameInfo &g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// Detection keys on the data files, not Game.exe: the executable is
// SafeDisc's, and only has to exist. ui.wad holds the front end; the
// American string tables tell the US release from the European ones,
// whose data is the same game (docs/engines/tpw/games/simcoaster/README.md).
const std::vector<GameVariant> &detectionTable() {
    static const std::vector<GameVariant> kTable = {
        // US release, CD "Theme Park Inc", data version Beta_21.
        {"simcoaster",
         "SimCoaster (US)",
         "en",
         "windows",
         {{"Data/ui.wad", "f8caf97175f18ef3675b3609b03413f4", 2817221},
          {"Data/Language/American/OBJECT_NAMES.str", "04d5bf6ef8a6d85f15a25a33716fd895", 10200},
          {"Game.exe", "", -1}}},
    };
    return kTable;
}

// Challenges.sam is SimCoaster's own: Theme Park World has no challenges
// (docs/engines/tpw/games/simcoaster/UNCONFIRMED.md).
const std::vector<GameVariant> &fallbackTable() {
    static const std::vector<GameVariant> kTable = {
        {"simcoaster",
         "SimCoaster (unknown release)",
         "",
         "windows",
         {{"Data/ui.wad", "", -1}, {"Data/Challenges.sam", "", -1}, {"Game.exe", "", -1}}},
    };
    return kTable;
}

} // namespace reluva::tpw
