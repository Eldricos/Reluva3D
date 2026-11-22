// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/games.h"

namespace reluva::sith {

namespace {

constexpr const char *kEngineId = "sith";

} // namespace

const std::vector<GameInfo> &games() {
    static const std::vector<GameInfo> kList = {
        {"jk", "Jedi Knight: Dark Forces II"},
    };
    return kList;
}

const GameInfo *findGame(std::string_view gameId) {
    for (const GameInfo &g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// The executable tells releases apart; the two archives make sure the data
// is the one it shipped with (they're the same in the install and on both
// CDs: docs/engines/sith/games/jk/README.md).
const std::vector<GameVariant> &detectionTable() {
    static const std::vector<GameVariant> kTable = {
        // US retail, two CDs, unpatched (JK.EXE linked 11 September 1997).
        {"jk",
         "Jedi Knight: Dark Forces II (US, 1.0)",
         "en",
         "windows",
         {{"JK.EXE", "6c5bc033d5f00c44df5e7107183b567d", 1387520},
          {"Resource/Res2.gob", "d3c4718f2e013021e2b80c94fb6641a9", 56069075},
          {"Episode/JK1.GOB", "6dea81c8e0dad25194d5008539e4eb34", 34861164}}},
    };
    return kTable;
}

const std::vector<GameVariant> &fallbackTable() {
    static const std::vector<GameVariant> kTable = {
        {"jk",
         "Jedi Knight: Dark Forces II (unknown release)",
         "",
         "windows",
         {{"JK.EXE", "", -1}, {"Resource/Res2.gob", "", -1}, {"Episode/JK1.GOB", "", -1}}},
    };
    return kTable;
}

std::vector<DetectedGame> detectCd(const std::filesystem::path &dir) {
    // The installer copies these three unchanged from CD 1's GAMEDATA, the
    // archives from folders of the same names; names match in any case.
    auto onCd = [](const std::vector<GameVariant> &table) {
        std::vector<GameVariant> out = table;
        for (GameVariant &v : out)
            for (FileSignature &f : v.files)
                f.name = f.name == "JK.EXE" ? "GAMEDATA/EXE/JK.EXE" : "GAMEDATA/" + f.name;
        return out;
    };
    static const std::vector<GameVariant> kTable = onCd(detectionTable());
    static const std::vector<GameVariant> kFallback = onCd(fallbackTable());
    std::vector<DetectedGame> found = detectFromTable(kEngineId, kTable, dir);
    return found.empty() ? detectFromTable(kEngineId, kFallback, dir) : found;
}

std::vector<DetectedGame> detectGames(const std::filesystem::path &dir) {
    std::vector<DetectedGame> found = detectFromTable(kEngineId, detectionTable(), dir);
    return found.empty() ? detectFromTable(kEngineId, fallbackTable(), dir) : found;
}

} // namespace reluva::sith
