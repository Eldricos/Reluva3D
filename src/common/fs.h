// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace reluva::fs {

namespace stdfs = std::filesystem;

std::string toLower(std::string_view s);

// Names in the order Windows lists a folder on NTFS (and the order the
// games' Windows engines then see): compared with a-z taken as upper case,
// so '_' comes after the letters, unlike a lower-case comparison. Other
// bytes compare as they are (UTF-8 keeps code point order); names equal
// but for case, which only other systems can hold, by their bytes. Used
// wherever a folder's listing reaches a game, so Linux, macOS and Windows
// give it the same order.
bool windowsNameLess(std::string_view a, std::string_view b);

// Find a file under `dir` ignoring case, as old Windows/DOS games expect.
// `relative` may contain '/' separated components ("data/sound.bin").
std::optional<stdfs::path> findFile(const stdfs::path& dir, std::string_view relative);
// The same for a folder.
std::optional<stdfs::path> findDirectory(const stdfs::path& dir, std::string_view relative);

// MD5 (hex) of the first `maxBytes` bytes of a file, or of the whole file
// when maxBytes is 0. Returns nullopt if the file can't be read.
std::optional<std::string> md5File(const stdfs::path& file, uint64_t maxBytes);

} // namespace reluva::fs
