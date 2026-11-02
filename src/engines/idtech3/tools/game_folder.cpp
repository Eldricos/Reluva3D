// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/tools/game_folder.h"

#include "engines/idtech3/filesystem.h"

#include <cstdio>

namespace reluva::idtech3::tool {

std::pair<std::filesystem::path, std::string> idTech3Folders(const char* folder) {
    const std::filesystem::path dir = std::filesystem::path(folder).lexically_normal();
    const std::filesystem::path path = dir.has_filename() ? dir : dir.parent_path();
    return {path.parent_path(), path.filename().string()};
}

bool mountIdTech3(const char* folder, reluva::SearchSet& set, const std::vector<std::string>& skip) {
    const std::filesystem::path dir = std::filesystem::path(folder).lexically_normal();
    const std::filesystem::path path = dir.has_filename() ? dir : dir.parent_path();
    std::vector<std::string> warnings;
    if (reluva::Error err =
            reluva::idtech3::mountGameDir(set, path.parent_path(), path.filename().string(), 0, warnings, skip)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    for (const std::string& w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    return true;
}

} // namespace reluva::idtech3::tool
