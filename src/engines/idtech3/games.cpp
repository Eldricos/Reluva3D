// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/games.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "common/fs.h"

namespace reluva::idtech3 {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        eliteforce::gameInfo(),
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
// Detection keys on baseEF/pak0.pk3, which holds the game's data. The
// Windows programs aren't needed (Reluva never runs them; a folder made by
// importing the game has none), and the patches are told apart afterwards
// (describeRelease).
//
// TODO: add the other releases (Expansion Pack, Mac, other languages) from
// owned copies. Until then they're found by fallbackTable().
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        // The English Windows CD's (its SETUP/BaseEF/pak0.pk3 and an install's).
        {"stvef", "Star Trek: Voyager - Elite Force", "en", "windows",
         {{"baseEF/pak0.pk3", "f31871f2891b1e9d6e8d1654ea93352d", 566234769}}},
    };
    return table;
}

namespace {

// The patches' paks and the single-player code of each release, by their
// signatures (as detectionTable()'s).
struct KnownFile {
    const char* name;
    const char* md5;
    int64_t size;
    const char* patch;
};
constexpr KnownFile kPatchPaks[] = {
    {"baseEF/pak2.pk3", "cf93390547bc232c5bc79c7a197dac3f", 12408486, "1.2"},
    {"baseEF/pak1.pk3", "54ba694bd5ee74629eebf348d4fe4e19", 6492912, "1.1"},
};
// The game DLL: the CD's (1.0), and the one patch 1.1 brought, which 1.2
// keeps ("Single Player will be upgraded to 1.1", its readme).
constexpr KnownFile kSpGameDlls[] = {
    {"efgamex86.dll", "d7a1187d8295ba1086c9f7de9a221f7b", 966732, "1.0"},
    {"efgamex86.dll", "030b262b7d9026e891aa404985e65d93", 974924, "1.1"},
};

bool matches(const std::filesystem::path& dir, const KnownFile& f) {
    const auto path = fs::findFile(dir, f.name);
    if (!path)
        return false;
    std::error_code ec;
    if (int64_t(std::filesystem::file_size(*path, ec)) != f.size || ec)
        return false;
    const auto md5 = fs::md5File(*path, kDetectionHashBytes);
    return md5 && *md5 == f.md5;
}

} // namespace

Release describeRelease(const std::filesystem::path& dir, const GameInfo& game) {
    Release r;
    r.patch = "1.0";
    for (const KnownFile& f : kPatchPaks)
        if (matches(dir, f)) {
            r.patch = f.patch;
            break;
        }
    r.singlePlayer = !game.spGameDll.empty() && fs::findFile(dir, game.spGameDll) &&
                     (game.spUiDll.empty() || fs::findFile(dir, game.spUiDll));
    if (r.singlePlayer)
        for (const KnownFile& f : kSpGameDlls)
            if (matches(dir, f))
                r.singlePlayerPatch = f.patch;
    r.summary = r.patch;
    if (!game.spGameDll.empty() && !r.singlePlayer)
        r.summary += ", no single player";
    return r;
}

// The same games by file names alone, for releases whose hashes aren't in
// detectionTable() yet. Used only when nothing there matches.
const std::vector<GameVariant>& fallbackTable() {
    static const std::vector<GameVariant> table = {
        {"stvef", "Star Trek: Voyager - Elite Force (unknown release)", "", "windows",
         {{"baseEF/pak0.pk3", "", -1}}},
    };
    return table;
}

} // namespace reluva::idtech3
