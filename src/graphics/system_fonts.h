// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::ttf {

/**
 * The folders the system keeps its fonts in, and the player's own: on
 * Windows its Fonts folders, on macOS the Library's, elsewhere those
 * fontconfig looks in by default.
 */
std::vector<std::filesystem::path> systemFontFolders();

/**
 * The first file in `folders` (searched a few levels down) with one of
 * `names`, in any case; its contents, or nothing if there's none.
 */
std::vector<uint8_t> findFont(const std::vector<std::filesystem::path> &folders, const std::vector<std::string> &names);

/**
 * Arial as the player has it installed, regular or bold, by its usual
 * file names (Windows' arial.ttf and arialbd.ttf, macOS's, the core
 * fonts' packages'); nothing if it isn't. Never shipped with Reluva.
 */
std::vector<uint8_t> installedArial(bool bold);

} // namespace reluva::ttf
