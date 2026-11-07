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

constexpr KnownFile kSpUiDlls[] = {
    {"efuix86.dll", "821a6c2a96d7007ffcd531d00a1ddc56", 376904, "1.0"},
    {"efuix86.dll", "b491cd5d22b76b87e9dcddcdf7ea8c7a", 376904, "1.1"},
};
// The Expansion Pack's (its CD's Setup/baseef and Setup/, the same in
// an install): its pak and its single-player code.
constexpr KnownFile kExpansionPakFile = {"baseEF/pak3.pk3", "3b400326c2d07ec0a4234c6b293d19f5", 88885710, ""};
constexpr KnownFile kExpansionDlls[] = {
    {"efgamex86.dll", "f8f997ef96a09755f0d1c920c15ca773", 999500, ""},
    {"efuix86.dll", "ec037144abf33d861093784b8b62be90", 458824, ""},
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

const std::vector<PatchFile>& patchFiles(const GameInfo& game) {
    static const std::vector<PatchFile> none;
    static const std::vector<PatchFile> eliteForce = [] {
        std::vector<PatchFile> files;
        for (const KnownFile& f : kPatchPaks)
            files.push_back({f.name, f.md5, f.size, f.patch});
        files.push_back({kSpGameDlls[1].name, kSpGameDlls[1].md5, kSpGameDlls[1].size, ""});
        files.push_back({kSpUiDlls[1].name, kSpUiDlls[1].md5, kSpUiDlls[1].size, ""});
        return files;
    }();
    return game.gameId == std::string("stvef") ? eliteForce : none;
}

bool isExpansionPak(const std::filesystem::path& file) {
    KnownFile f = kExpansionPakFile;
    f.name = kExpansionPak;
    return fs::toLower(file.filename().string()) == f.name && matches(file.parent_path(), f);
}

bool isExpansionDll(const std::filesystem::path& file) {
    for (const KnownFile& f : kExpansionDlls)
        if (fs::toLower(file.filename().string()) == f.name && matches(file.parent_path(), f))
            return true;
    return false;
}

std::filesystem::path singlePlayerDir(const std::filesystem::path& dir, const GameInfo& game, bool expansion) {
    if (game.spGameDll.empty())
        return {};
    auto has = [&](const std::filesystem::path& d) {
        return fs::findFile(d, game.spGameDll) && (game.spUiDll.empty() || fs::findFile(d, game.spUiDll));
    };
    const auto own = fs::findDirectory(dir, "expansion");
    const auto beside = fs::findFile(dir, game.spGameDll);
    const bool besideIsExpansion = beside && isExpansionDll(*beside);
    if (expansion && own && has(*own))
        return *own;
    if (has(dir) && besideIsExpansion == expansion)
        return dir;
    // Only the other kind's: the Expansion Pack's code needs its pak, and
    // without it there's nothing to run.
    return expansion && has(dir) ? dir : std::filesystem::path();
}

Release describeRelease(const std::filesystem::path& dir, const GameInfo& game) {
    Release r;
    r.patch = "1.0";
    for (const KnownFile& f : kPatchPaks)
        if (matches(dir, f)) {
            r.patch = f.patch;
            break;
        }
    r.expansion = game.gameId == "stvef" && matches(dir, kExpansionPakFile);
    const std::filesystem::path sp = singlePlayerDir(dir, game, false);
    r.singlePlayer = !sp.empty();
    if (r.singlePlayer)
        for (const KnownFile& f : kSpGameDlls)
            if (matches(sp, f))
                r.singlePlayerPatch = f.patch;
    const std::filesystem::path xp = singlePlayerDir(dir, game, true);
    r.expansionSinglePlayer = r.expansion && !xp.empty() && fs::findFile(xp, game.spGameDll) &&
                              isExpansionDll(*fs::findFile(xp, game.spGameDll));
    r.summary = r.patch + (r.expansion ? " + Expansion Pack" : "");
    if (!game.spGameDll.empty() && !r.singlePlayer && !r.expansionSinglePlayer)
        r.summary += ", no single player";
    else if (r.expansion && !r.expansionSinglePlayer)
        r.summary += ", no Expansion Pack single player";
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
