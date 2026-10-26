// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/folders.h"

#include "common/fs.h"

#include <algorithm>

namespace reluva::nfs {

namespace {

namespace stdfs = std::filesystem;

// The names of a folder's entries that are folders (or not), sorted.
std::vector<std::string> entries(const stdfs::path& dir, bool folders) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : stdfs::directory_iterator(dir, ec))
        if (e.is_directory(ec) == folders)
            names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

bool hasCdFiles(const stdfs::path& dir) {
    return !filesWithExtension(dir, "GameData/Audio/pc", ".mus").empty();
}

GameFolders findFolders(const stdfs::path& install, const std::map<std::string, std::string>& settings,
                        const char* executable) {
    GameFolders out;
    out.install = install;

    auto setting = settings.find("cdpath");
    if (setting != settings.end() && !setting->second.empty()) {
        stdfs::path cd = setting->second;
        if (cd.is_relative())
            cd = install / cd;
        if (hasCdFiles(cd)) {
            out.cd = cd;
            out.cdSource = "cdpath setting";
        }
        // A cdpath without the CD's files isn't used, but neither is a
        // folder found otherwise: the user asked for that one.
        return out;
    }

    if (hasCdFiles(install)) {
        out.cd = install;
        out.cdSource = "install folder";
        return out;
    }

    std::error_code ec;
    const stdfs::path self = stdfs::weakly_canonical(install, ec);
    const stdfs::path parent = (ec ? install : self).parent_path();
    for (const std::string& name : entries(parent, true)) {
        const stdfs::path dir = parent / name;
        if (stdfs::equivalent(dir, install, ec))
            continue;
        if (fs::findFile(dir, executable) && hasCdFiles(dir)) {
            out.cd = dir;
            out.cdSource = "folder beside the install";
            return out;
        }
    }
    return out;
}

std::vector<std::string> trackFolders(const stdfs::path& install) {
    std::vector<std::string> out;
    if (auto dir = fs::findDirectory(install, "GameData/Tracks"))
        for (const std::string& name : entries(*dir, true)) {
            const std::string lower = fs::toLower(name);
            if (lower.size() == 6 && lower.compare(0, 3, "trk") == 0 &&
                std::all_of(lower.begin() + 3, lower.end(), [](char c) { return c >= '0' && c <= '9'; }))
                out.push_back(name);
        }
    return out;
}

std::vector<std::string> carFolders(const stdfs::path& install) {
    std::vector<std::string> out;
    if (auto dir = fs::findDirectory(install, "GameData/CarModel"))
        for (const std::string& name : entries(*dir, true))
            if (fs::toLower(name) != "traffic" && fs::findFile(*dir / name, "car.viv"))
                out.push_back(name);
    return out;
}

std::vector<std::string> filesWithExtension(const stdfs::path& dir, const char* relative, const char* extension) {
    std::vector<std::string> out;
    if (dir.empty())
        return out;
    if (auto folder = fs::findDirectory(dir, relative))
        for (const std::string& name : entries(*folder, false))
            if (fs::toLower(stdfs::path(name).extension().string()) == extension)
                out.push_back(name);
    return out;
}

} // namespace reluva::nfs
