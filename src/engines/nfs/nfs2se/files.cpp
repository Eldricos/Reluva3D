// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/files.h"

#include "common/fs.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/formats/viv.h"

#include <algorithm>

namespace reluva::nfs::nfs2se {

namespace {

namespace stdfs = std::filesystem;

constexpr const char *kTracks = "GameData/Tracks/se";
constexpr const char *kCarData = "GameData/Sim/CarData/cardata.viv";

// The install, then the CD if it's another folder.
std::vector<stdfs::path> roots(const GameFolders &folders) {
    std::vector<stdfs::path> out = {folders.install};
    if (!folders.cd.empty() && folders.cd != folders.install)
        out.push_back(folders.cd);
    return out;
}

} // namespace

std::vector<std::string> tracks(const GameFolders &folders) {
    std::vector<std::string> out;
    for (const stdfs::path &root : roots(folders))
        for (const std::string &file : filesWithExtension(root, kTracks, ".trk")) {
            const std::string name = stdfs::path(file).stem().string();
            if (std::none_of(out.begin(), out.end(),
                             [&](const std::string &n) { return fs::toLower(n) == fs::toLower(name); }))
                out.push_back(name);
        }
    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
    return out;
}

std::vector<std::string> carModels(const GameFolders &folders) {
    for (const stdfs::path &root : roots(folders)) {
        const auto file = fs::findFile(root, kCarData);
        VivArchive viv;
        if (!file || viv.open(*file))
            continue;
        std::vector<std::string> out;
        for (const std::string &name : viv.list())
            if (fs::toLower(stdfs::path(name).extension().string()) == ".geo")
                out.push_back(stdfs::path(name).stem().string());
        std::sort(out.begin(), out.end(),
                  [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
        return out;
    }
    return {};
}

Programs programs(const GameFolders &folders) {
    Programs out;
    for (const stdfs::path &root : roots(folders)) {
        if (out.software.empty())
            if (auto file = fs::findFile(root, "nfs2sen.exe"))
                out.software = *file;
        if (out.glide.empty())
            if (auto file = fs::findFile(root, "nfs2sea.exe"))
                out.glide = *file;
    }
    return out;
}

std::filesystem::path playProgram(const GameFolders &folders, bool glide) {
    const Programs found = programs(folders);
    const std::filesystem::path &wanted = glide ? found.glide : found.software;
    return wanted.empty() ? (glide ? found.software : found.glide) : wanted;
}

std::vector<uint8_t> racePalette(const GameFiles &files) {
    for (const FileRef &ref : files.withExtensions({".fsh"})) {
        // The loose file: the HUD's archive (huddata.viv) holds an empty one.
        if (!ref.inner.empty() || fs::toLower(ref.name).find("dashhud/pc/hud.fsh") == std::string::npos)
            continue;
        std::vector<uint8_t> data;
        ShapeBank bank;
        if (files.read(ref, data) || parseShapeBank(std::move(data), bank))
            continue;
        for (const Shape &s : bank.shapes)
            if (isPaletteType(s.type))
                return s.palette;
    }
    return {};
}

Error checkFolders(const GameFolders &folders) {
    if (!tracks(folders).empty())
        return Error::success();
    if (folders.cd.empty())
        return Error::fail("Need for Speed II SE reads its tracks from its CD, and no copy of the CD's files was "
                           "found: put one beside the install, or set cdpath=<folder> in this game's section of "
                           "reluva.ini");
    return Error::fail("No tracks (" + std::string(kTracks) + "/*.trk) found in " + folders.install.string() + " or " +
                       folders.cd.string());
}

} // namespace reluva::nfs::nfs2se
