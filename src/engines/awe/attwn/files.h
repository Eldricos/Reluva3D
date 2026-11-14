// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// How ac.exe names and reads its files. It asks for a file by its ordinary
// name ("text/itemlist.txt") and opens the same name with the extension's
// first two letters made "ac" (0x403bd0) when that file is there (0x403c20):
// .act for .txt, .aca for .tga, .acg for .jpg, .acv for .wav. Only text is
// changed inside: .act is scrambled (0x4028f0, 0x402980). See
// docs/engines/awe/games/attwn/formats/files.md.

namespace reluva::awe::attwn {

/**
 * The text of an .act file, as the plain .txt it stands for: a u16 count of
 * lines, then each line as a u16 length and its bytes XORed with 0x30, each
 * with its own line ending.
 */
Error unscrambleText(const std::vector<uint8_t> &act, std::vector<uint8_t> &text);

/** The name `name` is opened by in the install ("x.txt" -> "x.act"), or "" for any other kind of file. */
std::string acName(std::string_view name);

/** The ordinary name of a file of the install ("x.act" -> "x.txt"), or "" for any other kind of file. */
std::string plainName(std::string_view name);

/**
 * The install's files as the game opens them: by their ordinary names,
 * each read from its "ac" file when there is one, the text unscrambled.
 * Names the install has that aren't the game's renamed kinds are as they are.
 */
class GameFiles : public Archive {
public:
    Error open(const std::filesystem::path &dir);
    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _dir.describe(); }

private:
    DirectoryArchive _dir;
};

/** GameFiles over `dir` (GameInfo::openFiles). */
Error openFiles(const std::filesystem::path &dir, std::unique_ptr<Archive> &out);

} // namespace reluva::awe::attwn
