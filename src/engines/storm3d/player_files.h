// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The files the game writes in its install's folder (its preferences,
 * the player's progress, saved games), kept in Reluva's save folder for
 * the game instead: read from there first, then from the install, and
 * written there, so the install stays as it was.
 */
class PlayerFiles {
public:
    PlayerFiles(const GameFiles &files, std::filesystem::path saveFolder)
        : _files(files), _folder(std::move(saveFolder)) {}

    /**
     * A file's contents: the save folder's copy if there is one.
     * @return a failure if neither has it
     */
    Error read(const std::string &name, std::vector<uint8_t> &out) const;
    /**
     * Writes a file to the save folder.
     * @return a failure if there's no save folder or it can't be written
     */
    Error write(const std::string &name, const std::vector<uint8_t> &data) const;

private:
    const GameFiles &_files;
    std::filesystem::path _folder;
};

} // namespace reluva::storm3d
