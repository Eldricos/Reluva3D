// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::sith {

/**
 * A .gob archive, as docs/engines/sith/games/jk/formats/gob.md describes.
 * Only the index is kept in memory (the archives run to 60 MB); each read
 * opens the file again.
 */
class GobArchive : public Archive {
public:
    /** One file in the archive. */
    struct Entry {
        std::string name; // as stored, with '\' between folders
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    /** Reads the index; fails if it, or a file in it, runs past the end. */
    Error open(const std::filesystem::path &path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.filename().string(); }

    /** Every file, in the index's order. */
    const std::vector<Entry> &entries() const { return _entries; }

private:
    std::filesystem::path _path;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> _entries index
};

/** The archive's layout, for the tests that build one. */
///@{
constexpr uint32_t kGobVersion = 0x14;
constexpr size_t kGobHeaderSize = 12;
constexpr size_t kGobEntrySize = 136;
constexpr size_t kGobNameSize = 128;
///@}

} // namespace reluva::sith
