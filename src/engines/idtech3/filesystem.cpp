// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/filesystem.h"

#include "common/fs.h"
#include "common/zip.h"

#include <algorithm>
#include <fstream>
#include <memory>

namespace reluva::idtech3 {

Error mountGameDir(SearchSet& set, const std::filesystem::path& install, std::string_view gameDir, int layer,
                   std::vector<std::string>& warnings, const std::vector<std::string>& skip) {
    const auto dir = fs::findDirectory(install, gameDir);
    if (!dir)
        return Error::fail("No " + std::string(gameDir) + " folder in " + install.string());

    std::vector<std::filesystem::path> paks;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(*dir, ec), end; !ec && it != end; it.increment(ec))
        if (fs::toLower(it->path().extension().string()) == ".pk3" && it->is_regular_file(ec) &&
            std::find(skip.begin(), skip.end(), fs::toLower(it->path().filename().string())) == skip.end())
            paks.push_back(it->path());
    // Compared in upper case (fs::windowsNameLess): that puts '_' after the
    // letters, unlike lower case, and decides which pak overrides which.
    auto name = [](const std::filesystem::path& p) {
        const std::u8string u8 = p.filename().u8string();
        return std::string(u8.begin(), u8.end());
    };
    std::sort(paks.begin(), paks.end(), [&](const auto& a, const auto& b) { return fs::windowsNameLess(name(a), name(b)); });
    if (paks.size() >= kPrioritiesPerGameDir)
        return Error::fail("Too many .pk3 files in " + dir->string());

    const int base = layer * kPrioritiesPerGameDir;
    int priority = base;
    for (const auto& p : paks) {
        auto zip = std::make_unique<ZipArchive>();
        if (Error err = zip->open(p)) {
            warnings.push_back(err.message);
            continue;
        }
        set.add(std::move(zip), ++priority);
    }

    // The loose files, above the folder's own archives. They include the
    // .pk3 files themselves, which is harmless.
    auto loose = std::make_unique<DirectoryArchive>();
    if (Error err = loose->open(*dir))
        return err;
    set.add(std::move(loose), base + kPrioritiesPerGameDir - 1);
    return Error::success();
}

Error mountGame(SearchSet& set, const std::filesystem::path& install, const std::filesystem::path& home,
                std::string_view baseDir, std::string_view mod, std::vector<std::string>& warnings,
                const std::vector<std::string>& skip) {
    set.clear();
    if (Error err = mountGameDir(set, install, baseDir, 0, warnings, skip))
        return err;
    if (!home.empty() && fs::findDirectory(home, baseDir))
        mountGameDir(set, home, baseDir, 1, warnings);
    if (!mod.empty()) {
        if (fs::findDirectory(install, mod))
            mountGameDir(set, install, mod, 2, warnings);
        if (!home.empty() && fs::findDirectory(home, mod))
            mountGameDir(set, home, mod, 3, warnings);
    }
    return Error::success();
}

std::string modFolderName(std::string_view value, std::string_view baseDir) {
    if (value.empty() || value[0] == '.' || value.find_first_of("/\\:") != std::string_view::npos ||
        fs::toLower(value) == fs::toLower(baseDir))
        return {};
    return std::string(value);
}

std::filesystem::path modSavePath(const std::filesystem::path& savePath, std::string_view mod) {
    if (savePath.empty() || mod.empty())
        return savePath;
    return savePath / "mods" / std::filesystem::path(std::u8string(mod.begin(), mod.end()));
}

std::vector<ModFolder> listMods(const std::filesystem::path& install, std::string_view baseDir) {
    auto utf8 = [](const std::filesystem::path& p) {
        const std::u8string u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    };
    std::vector<std::filesystem::path> folders;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(install, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = utf8(it->path().filename());
        if (it->is_directory(ec) && !name.empty() && name[0] != '.' && fs::toLower(name) != fs::toLower(baseDir))
            folders.push_back(it->path());
    }
    std::sort(folders.begin(), folders.end(),
              [&](const auto& a, const auto& b) { return fs::windowsNameLess(utf8(a.filename()), utf8(b.filename())); });
    std::vector<ModFolder> out;
    for (const std::filesystem::path& folder : folders) {
        // Only folders with paks of their own.
        bool paks = false;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end && !paks; it.increment(ec))
            paks = fs::toLower(it->path().extension().string()) == ".pk3" && it->is_regular_file(ec);
        if (!paks)
            continue;
        ModFolder mod;
        mod.name = utf8(folder.filename());
        // The first 48 bytes of description.txt, as they are (to a NUL); the
        // folder's name if there's none or it's empty.
        mod.description = mod.name;
        if (const auto file = fs::findFile(folder, "description.txt")) {
            std::ifstream in(*file, std::ios::binary);
            char text[48] = {};
            in.read(text, sizeof text);
            if (in.gcount() > 0) {
                mod.description.assign(text, size_t(in.gcount()));
                mod.description = mod.description.substr(0, mod.description.find('\0'));
            }
        }
        out.push_back(std::move(mod));
    }
    return out;
}

uint32_t packModList(const std::vector<ModFolder>& mods, uint32_t size, std::vector<uint8_t>& out) {
    out.assign(size ? 1 : 0, 0);
    if (!size)
        return 0;
    out.clear();
    uint32_t count = 0;
    for (const ModFolder& mod : mods) {
        // The list ends at the first that doesn't leave more than 2 bytes.
        if (out.size() + mod.name.size() + 1 + mod.description.size() + 1 + 2 >= size)
            break;
        out.insert(out.end(), mod.name.begin(), mod.name.end());
        out.push_back(0);
        out.insert(out.end(), mod.description.begin(), mod.description.end());
        out.push_back(0);
        ++count;
    }
    if (out.empty())
        out.push_back(0);
    return count;
}

} // namespace reluva::idtech3
