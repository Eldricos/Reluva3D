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
 * each folder's sorted by name. Which of them the game searches, and in
 * what order, isn't worked out yet; until it is, a name is found in the
 * first archive that has it.
 */
class GameFiles {
public:
    /** Opens the archives; fails if there are none, or one can't be read. */
    Error open(const std::filesystem::path &install);

    /** The archives, Resource's then Episode's. */
    const std::vector<std::unique_ptr<GobArchive>> &archives() const { return _archives; }

    /** The first archive with the file, or nullptr. */
    const GobArchive *find(std::string_view name) const;

    /** A file from the first archive with it. */
    Error read(std::string_view name, std::vector<uint8_t> &out) const;

private:
    std::vector<std::unique_ptr<GobArchive>> _archives;
};

} // namespace reluva::sith
