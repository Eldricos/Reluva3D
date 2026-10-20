// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/games.h"

namespace reluva::idtech3 {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        {"stvef", "Star Trek: Voyager - Elite Force", "baseEF"},
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
// TODO: the Elite Force entry only checks that the files exist. Add the
// size and MD5 of baseEF/pak0.pk3 for each release (original, patched,
// Expansion Pack, Mac) from owned copies; pak0.pk3 is the file patches
// leave alone, like New Worlds' glblgame.cat.
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        {"stvef", "Star Trek: Voyager - Elite Force", "en", "windows",
         {{"baseEF/pak0.pk3", "", -1}, {"stvoy.exe", "", -1}}},
    };
    return table;
}

} // namespace reluva::idtech3
