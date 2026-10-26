// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/games.h"

namespace reluva::nfs {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        {"nfs3", "Need for Speed III: Hot Pursuit", "nfs3.exe"},
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
// The first track's geometry is the same in the install and on the CD, so
// either folder matches; the executable tells releases apart.
//
// TODO: add the original 1998 retail release, other languages and patched
// executables from owned copies. Until then they're found by
// fallbackTable().
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        // US "Need for Speed III Classics" CD and its install (October 2026).
        {"nfs3", "Need for Speed III: Hot Pursuit (Classics)", "en", "windows",
         {{"nfs3.exe", "5cbf7d80f6b993926f7b41a0aa3ffc99", 1589248},
          {"GameData/Tracks/trk000/tr00.frd", "42ac1185877f2c7fbaa0de8c7b0d6351", 1422739}}},
    };
    return table;
}

const std::vector<GameVariant>& fallbackTable() {
    static const std::vector<GameVariant> table = {
        {"nfs3", "Need for Speed III: Hot Pursuit (unknown release)", "", "windows",
         {{"nfs3.exe", "", -1}, {"GameData/Tracks/trk000/tr00.frd", "", -1}}},
    };
    return table;
}

} // namespace reluva::nfs
