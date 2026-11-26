// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/games.h"

#include "common/fs.h"

namespace reluva::sith {

namespace {

constexpr const char *kEngineId = "sith";

} // namespace

const std::vector<GameInfo> &games() {
    // JK_.CD's bytes tell each CD apart (docs/engines/sith/games/jk/formats/jk-cd.md);
    // the labels are the volumes' names a CD drive shows the game.
    static const std::vector<GameInfo> kList = {
        {"jk",
         "Jedi Knight: Dark Forces II",
         "JK.EXE",
         "Software\\LucasArts Entertainment Company\\JediKnight\\v1.0",
         {{{0xc4, 0x32, 0x92, 0x69}, {0x84, 0x33, 0x92, 0x69}}},
         {"JEDI_KNIGHT_1", "JEDI_KNIGHT_2"}},
        // Its own CD, then Jedi Knight's CD 2, which it asks for once
        // (docs/engines/sith/games/mots/README.md).
        {"mots",
         "Jedi Knight: Mysteries of the Sith",
         "JKM.EXE",
         "Software\\LucasArts Entertainment Company LLC\\Mysteries of the Sith\\v1.0",
         {{{0x29, 0x69, 0x42, 0x3b}, {0x84, 0x33, 0x92, 0x69}}},
         {"SITH", "JEDI_KNIGHT_2"}},
    };
    return kList;
}

const GameInfo *findGame(std::string_view gameId) {
    for (const GameInfo &g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

const GameInfo *gameInstalled(const std::filesystem::path &install) {
    for (const GameInfo &g : games())
        if (fs::findFile(install, g.program))
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
        // US retail, one CD (JKM.EXE linked 16 January 1998).
        {"mots",
         "Jedi Knight: Mysteries of the Sith (US, 1.0)",
         "en",
         "windows",
         {{"JKM.EXE", "1ef9c284fb4fa9df3490b767061aed8b", 1759744},
          {"Resource/JKMRES.GOO", "d3c788a2e4afb4a5fc124adf2481cb3a", 86091024},
          {"Episode/JKM.GOO", "1cc4e2488f355352b70f2102f0f1546b", 36273376}}},
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
        {"mots",
         "Jedi Knight: Mysteries of the Sith (unknown release)",
         "",
         "windows",
         {{"JKM.EXE", "", -1}, {"Resource/JKMRES.GOO", "", -1}, {"Episode/JKM.GOO", "", -1}}},
    };
    return kTable;
}

std::vector<DetectedGame> detectCd(const FileLookup &files) {
    // The installer copies these unchanged from the CD's GAMEDATA, the
    // program from GAMEDATA/EXE and the archives from folders of the same
    // names; names match in any case.
    auto onCd = [](const std::vector<GameVariant> &table) {
        std::vector<GameVariant> out = table;
        for (GameVariant &v : out)
            for (FileSignature &f : v.files)
                f.name = f.name.find('/') == std::string::npos ? "GAMEDATA/EXE/" + f.name : "GAMEDATA/" + f.name;
        return out;
    };
    static const std::vector<GameVariant> kTable = onCd(detectionTable());
    static const std::vector<GameVariant> kFallback = onCd(fallbackTable());
    std::vector<DetectedGame> found = detectFromTable(kEngineId, kTable, files);
    return found.empty() ? detectFromTable(kEngineId, kFallback, files) : found;
}

std::vector<DetectedGame> detectGames(const std::filesystem::path &dir) {
    std::vector<DetectedGame> found = detectFromTable(kEngineId, detectionTable(), dir);
    return found.empty() ? detectFromTable(kEngineId, fallbackTable(), dir) : found;
}

} // namespace reluva::sith
