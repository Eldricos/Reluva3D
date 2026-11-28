// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/gob.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace reluva::sith {

/**
 * The install's archives: every .gob in its Resource and Episode folders,
 * each folder's sorted by name. A name is found as the game finds it, in
 * an episode's archives before the resources' (0x40f4c0); the game opens
 * one episode at a time, this every one, the first by name winning
 * (docs/engines/sith/games/jk/formats/gob.md).
 */
class GameFiles {
public:
    /** Opens the archives; fails if there are none, or one can't be read. */
    Error open(const std::filesystem::path &install);

    /** The archives, Resource's then Episode's. */
    const std::vector<std::unique_ptr<GobArchive>> &archives() const { return _archives; }

    /** The archive the file is found in, or nullptr. */
    const GobArchive *find(std::string_view name) const;

    /** A file from the archive it's found in. */
    Error read(std::string_view name, std::vector<uint8_t> &out) const;

private:
    std::vector<std::unique_ptr<GobArchive>> _archives;
    /** Where the Episode folder's archives start in _archives. */
    size_t _firstEpisode = 0;
};

} // namespace reluva::sith
