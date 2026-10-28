// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/games.h"

namespace reluva::age {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        {"mm2", "Midtown Madness 2"},
    };
    return list;
}

const GameInfo* findGame(std::string_view gameId) {
    for (const GameInfo& g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// Built with `reluva --hash`: each entry needs the size and the MD5 of the
// first kDetectionHashBytes bytes.
//
// Detection keys on mm2core.ar, which holds the game's models, tuning and
// cities; the executable is only required to exist. The CD's GAME folder
// holds the same files with upper-case names, which detection ignores.
//
// TODO: add the other releases (other languages, the demo) from owned
// copies. Until then they're found by fallbackTable().
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        // English Windows release, Midtown2.exe 1.00.3200 (October 2026).
        {"mm2", "Midtown Madness 2", "en", "windows",
         {{"mm2core.ar", "8ba3c06f784f6f7abff1d4b902ef3440", 21430272}, {"Midtown2.exe", "", -1}}},
    };
    return table;
}

const std::vector<GameVariant>& fallbackTable() {
    static const std::vector<GameVariant> table = {
        {"mm2", "Midtown Madness 2 (unknown release)", "", "windows",
         {{"mm2core.ar", "", -1}, {"Midtown2.exe", "", -1}}},
    };
    return table;
}

} // namespace reluva::age
