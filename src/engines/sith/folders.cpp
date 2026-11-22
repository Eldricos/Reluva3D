// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/folders.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace reluva::sith {

namespace {

namespace stdfs = std::filesystem;

// JK_.CD's four bytes on each CD of the US release. What they mean isn't
// known; they only tell the CDs apart
// (docs/engines/sith/games/jk/formats/jk-cd.md).
constexpr std::array<std::array<uint8_t, 4>, kCdCount> kCdMarks = {{
    {0xc4, 0x32, 0x92, 0x69},
    {0x84, 0x33, 0x92, 0x69},
}};

// The folders in `dir`, sorted, so the first match is the same everywhere.
std::vector<stdfs::path> subfolders(const stdfs::path &dir) {
    std::vector<stdfs::path> out;
    std::error_code ec;
    for (const auto &e : stdfs::directory_iterator(dir, ec))
        if (e.is_directory(ec))
            out.push_back(e.path());
    std::sort(out.begin(), out.end(), [](const stdfs::path &a, const stdfs::path &b) {
        return fs::windowsNameLess(a.filename().string(), b.filename().string());
    });
    return out;
}

} // namespace

int cdNumber(const stdfs::path &dir) {
    const auto mark = fs::findFile(dir, "GAMEDATA/RESOURCE/JK_.CD");
    if (!mark)
        return 0;
    std::ifstream in(*mark, std::ios::binary);
    std::array<uint8_t, 5> bytes{};
    in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    if (in.gcount() != 4)
        return 0;
    for (int i = 0; i < kCdCount; ++i)
        if (std::equal(kCdMarks[i].begin(), kCdMarks[i].end(), bytes.begin()))
            return i + 1;
    return 0;
}

GameFolders findFolders(const stdfs::path &install, const std::map<std::string, std::string> &settings) {
    GameFolders out;
    out.install = install;

    std::array<bool, kCdCount> settled{};
    for (int i = 0; i < kCdCount; ++i) {
        auto setting = settings.find("cd" + std::to_string(i + 1) + "path");
        if (setting == settings.end() || setting->second.empty())
            continue;
        stdfs::path cd = setting->second;
        if (cd.is_relative())
            cd = install / cd;
        // A setting naming something else isn't used, but neither is a
        // folder found otherwise: the user asked for that one.
        settled[i] = true;
        if (cdNumber(cd) == i + 1) {
            out.cds[i] = cd;
            out.cdSources[i] = "cd" + std::to_string(i + 1) + "path setting";
        }
    }

    auto consider = [&](const stdfs::path &dir, const char *source) {
        const int n = cdNumber(dir);
        if (n == 0 || settled[n - 1])
            return;
        out.cds[n - 1] = dir;
        out.cdSources[n - 1] = source;
        settled[n - 1] = true;
    };

    consider(install, "install folder");
    for (const stdfs::path &dir : subfolders(install))
        consider(dir, "folder in the install");

    // The parent as the install's own path names it, so a relative install
    // finds its CDs by relative paths too; only a path ending in "." or ".."
    // needs absolute().
    std::error_code ec;
    auto tidy = [](stdfs::path p) {
        p = p.lexically_normal();
        return p.has_filename() ? p : p.parent_path();
    };
    stdfs::path self = tidy(install);
    if (self.empty() || self == "." || self.filename() == "..")
        self = tidy(stdfs::absolute(self.empty() ? stdfs::path(".") : self, ec));
    const stdfs::path parent = self.has_parent_path() ? self.parent_path() : stdfs::path(".");
    for (const stdfs::path &dir : subfolders(parent))
        if (!stdfs::equivalent(dir, install, ec))
            consider(dir, "folder beside the install");
    return out;
}

std::vector<CutScene> findCutScenes(const GameFolders &folders) {
    std::vector<CutScene> out;
    for (int i = 0; i < kCdCount; ++i) {
        if (folders.cds[i].empty())
            continue;
        const auto video = fs::findDirectory(folders.cds[i], "GAMEDATA/RESOURCE/VIDEO");
        if (!video)
            continue;
        std::vector<CutScene> found;
        std::error_code ec;
        for (const auto &e : stdfs::directory_iterator(*video, ec))
            if (e.is_regular_file(ec) && fs::toLower(e.path().extension().string()) == ".smk")
                found.push_back({e.path().filename().string(), i + 1, e.path()});
        std::sort(found.begin(), found.end(),
                  [](const CutScene &a, const CutScene &b) { return fs::windowsNameLess(a.name, b.name); });
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

} // namespace reluva::sith
