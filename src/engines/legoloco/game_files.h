// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/legoloco/formats/resource_file.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::legoloco {

/**
 * A copy's files: the folder holding `Exe` and `art-res` (an install or
 * the CD), its resource file, and the resources' names, which `loco.exe`
 * keeps in its string table under their numbers
 * (docs/engines/legoloco/formats/resource-numbers.md).
 */
class GameFiles {
public:
    /** Opens the copy at `root`, or at the folder above it if given its `art-res`. */
    Error open(const std::filesystem::path &root);

    /** The art-res folder. */
    const std::filesystem::path &artRes() const { return _artRes; }
    /** The resource file. */
    const ResourceFile &resources() const { return _resources; }
    /** Every resource number with its name ("track\post"), as the executable lists them. */
    const std::map<uint32_t, std::string> &resourceNames() const { return _names; }
    /**
     * The resource file's entry for a resource number and an extension
     * ("bmp", "dat"), or -1.
     */
    int findResource(uint32_t number, const char *extension) const;
    /** A file or folder in art-res, found in any case. */
    ///@{
    std::optional<std::filesystem::path> findFile(const std::string &relative) const;
    std::optional<std::filesystem::path> findDirectory(const std::string &relative) const;
    ///@}

private:
    std::filesystem::path _artRes;
    ResourceFile _resources;
    std::map<uint32_t, std::string> _names;
};

/**
 * The resource names in an executable's string table: every string with a
 * `\` in it, by number.
 */
Error readResourceNames(const std::vector<uint8_t> &exe, std::map<uint32_t, std::string> &out);

} // namespace reluva::legoloco
