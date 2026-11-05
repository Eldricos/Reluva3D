// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/detection.h"

#include "common/fs.h"
#include "common/stream.h"
#include "common/text.h"

#include <algorithm>
#include <initializer_list>

namespace reluva::stharbinger {

namespace {

// Whether any of `names` (relative paths, files or folders) is under `root`.
bool hasAny(const std::filesystem::path& root, std::initializer_list<const char*> names) {
    for (const char* name : names)
        if (fs::findFile(root, name) || fs::findDirectory(root, name))
            return true;
    return false;
}

// The disc number in DS9/TORPEDOS/CD ("0" or "1"), or -1.
int readDiscNumber(const std::filesystem::path& root) {
    for (const char* name : {"DS9/TORPEDOS/CD", "TORPEDOS/CD"}) {
        std::vector<uint8_t> data;
        if (auto path = fs::findFile(root, name); path && readFile(*path, data)) {
            const std::string text = text::trim(std::string(data.begin(), data.end()));
            if (!text.empty() && text[0] >= '0' && text[0] <= '9')
                return text[0] - '0';
        }
    }
    return -1;
}

GamePart examine(const std::filesystem::path& path) {
    GamePart part;
    part.folder = path;
    part.scripts = hasAny(path, {"CC.ALL", "SSE/CC.ALL", "SSE/SSE.ALL"});
    part.data = hasAny(path, {"DS9/ANIMS", "DS9/PALETTES", "ANIMS", "PALETTES"});
    part.exe = hasAny(path, {"DEEP.EXE", "SSE/DEEP.EXE"});
    part.disc = readDiscNumber(path);
    return part;
}

// One of the CDs (it says which), or a folder with some of the game.
bool isPart(const GamePart& p) { return p.scripts || p.data || p.exe || p.disc >= 0; }

} // namespace

std::vector<GamePart> findGameParts(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return {};
    GamePart self = examine(dir);
    if (isPart(self))
        return {self};
    // A folder holding the discs (copies of both CDs, or Reluva's import of
    // them): its folders that are parts.
    std::vector<std::filesystem::path> folders;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec))
            folders.push_back(it->path());
    std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) {
        return fs::windowsNameLess(a.filename().string(), b.filename().string());
    });
    std::vector<GamePart> parts;
    for (const auto& folder : folders) {
        GamePart part = examine(folder);
        if (isPart(part))
            parts.push_back(std::move(part));
    }
    return parts;
}

bool playable(const std::vector<GamePart>& parts) {
    const bool scripts = std::any_of(parts.begin(), parts.end(), [](const GamePart& p) { return p.scripts; });
    const bool data = std::any_of(parts.begin(), parts.end(), [](const GamePart& p) { return p.data; });
    return scripts && data;
}

} // namespace reluva::stharbinger
