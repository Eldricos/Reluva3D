// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/game_files.h"

#include "common/fs.h"

#include <algorithm>
#include <string>

namespace reluva::sith {

Error GameFiles::open(const std::filesystem::path &install) {
    _archives.clear();
    for (const char *folder : {"Resource", "Episode"}) {
        const auto dir = fs::findDirectory(install, folder);
        if (!dir)
            continue;
        std::vector<std::filesystem::path> gobs;
        std::error_code ec;
        for (const auto &e : std::filesystem::directory_iterator(*dir, ec))
            if (e.is_regular_file(ec) && fs::toLower(e.path().extension().string()) == ".gob")
                gobs.push_back(e.path());
        std::sort(gobs.begin(), gobs.end(), [](const auto &a, const auto &b) {
            return fs::windowsNameLess(a.filename().string(), b.filename().string());
        });
        for (const auto &path : gobs) {
            auto gob = std::make_unique<GobArchive>();
            if (Error err = gob->open(path))
                return err;
            _archives.push_back(std::move(gob));
        }
    }
    if (_archives.empty())
        return Error::fail("No .gob archives in " + install.string() + "'s Resource or Episode folders");
    return Error::success();
}

const GobArchive *GameFiles::find(std::string_view name) const {
    for (const auto &gob : _archives)
        if (gob->has(name))
            return gob.get();
    return nullptr;
}

Error GameFiles::read(std::string_view name, std::vector<uint8_t> &out) const {
    if (const GobArchive *gob = find(name))
        return gob->read(name, out);
    return Error::fail(std::string(name) + " isn't in any of the game's archives");
}

} // namespace reluva::sith
