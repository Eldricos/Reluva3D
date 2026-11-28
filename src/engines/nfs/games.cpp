// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/games.h"

#include "engines/nfs/nfs2se/files.h"

namespace reluva::nfs {

const std::vector<GameInfo> &games() {
    static const std::vector<GameInfo> kList = {
        {Game::kNfs3, "nfs3", "Need for Speed III: Hot Pursuit", "nfs3.exe"},
        // Its software-rendering program, which every install has; the CD
        // also has nfs2sea.exe, for 3dfx cards.
        {Game::kNfs2Se, "nfs2se", "Need for Speed II SE", "nfs2sen.exe"},
    };
    return kList;
}

const GameInfo *findGame(std::string_view gameId) {
    for (const GameInfo &g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// Built with `reluva --hash`: each entry needs the size and the MD5 of the
// first kDetectionHashBytes bytes.
//
// Each game's files are the same in its install and on its CD, so either
// folder matches; the executable tells releases apart.
//
// TODO: add NFS III's original 1998 retail release, other languages and
// patched executables from owned copies. Until then they're found by
// fallbackTable().
const std::vector<GameVariant> &detectionTable() {
    static const std::vector<GameVariant> kTable = {
        // US "Need for Speed III Classics" CD and its install (October 2026).
        {"nfs3",
         "Need for Speed III: Hot Pursuit (Classics)",
         "en",
         "windows",
         {{"nfs3.exe", "5cbf7d80f6b993926f7b41a0aa3ffc99", 1589248},
          {"GameData/Tracks/trk000/tr00.frd", "42ac1185877f2c7fbaa0de8c7b0d6351", 1422739}}},
        // US CD, with English and Spanish set-up, and its install (November
        // 2026). The install leaves the tracks on the CD, so the cars' data
        // stands for the game.
        {"nfs2se",
         "Need for Speed II SE",
         "en",
         "windows",
         {{"nfs2sen.exe", "0b44f8449fa8889ecd5c4d17b90a3a40", 900096},
          {"GameData/Sim/CarData/cardata.viv", "f7cf339beaa69552581e6e1a91a82ab0", 242816}}},
    };
    return kTable;
}

const std::vector<GameVariant> &fallbackTable() {
    static const std::vector<GameVariant> kTable = {
        {"nfs3",
         "Need for Speed III: Hot Pursuit (unknown release)",
         "",
         "windows",
         {{"nfs3.exe", "", -1}, {"GameData/Tracks/trk000/tr00.frd", "", -1}}},
        // By either program: an install may have only the one for 3dfx
        // cards (guessed; the copy studied has the other).
        {"nfs2se",
         "Need for Speed II SE (unknown release)",
         "",
         "windows",
         {{"nfs2sen.exe", "", -1}, {"GameData/Sim/CarData/cardata.viv", "", -1}}},
        {"nfs2se",
         "Need for Speed II SE (unknown release)",
         "",
         "windows",
         {{"nfs2sea.exe", "", -1}, {"GameData/Sim/CarData/cardata.viv", "", -1}}},
    };
    return kTable;
}

Error checkFolders(const GameInfo &game, const GameFolders &folders) {
    switch (game.game) {
    case Game::kNfs3:
        if (trackFolders(folders.install).empty())
            return Error::fail("No tracks (GameData/Tracks/trkNNN) found in " + folders.install.string());
        return Error::success();
    case Game::kNfs2Se:
        return nfs2se::checkFolders(folders);
    }
    return Error::success();
}

} // namespace reluva::nfs
