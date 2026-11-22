// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::storm3d {

/**
 * A ZFS archive (`ZFSF` at its start), as Star Trek: Armada keeps its object
 * definitions and mission texts (`assets.zfs`, `objtxt.zfs`): a header, then
 * blocks of a fixed number of entries, each block followed by its files'
 * data. Every file is XORed with a key from the header. See
 * docs/engines/storm3d/games/armada/formats/zfs.md.
 */
class ZfsArchive : public Archive {
public:
    /** A file in the archive's tables. */
    struct Entry {
        std::string name;    // as stored, at most the header's name length (16 in Armada's)
        uint32_t offset = 0; // of the data, from the start of the archive
        uint32_t index = 0;  // the entry's place in the tables, from 0
        uint32_t size = 0;
        uint32_t modified = 0; // Unix time
        uint32_t flags = 0;    // 0 in every entry seen; read() refuses any other
    };

    /**
     * Reads the header and every table block, checking each file lies within
     * the archive. The data is read from the file on each read().
     */
    Error open(const std::filesystem::path &path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.string(); }

    /** Every file, in the tables' order. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** The file of that name, matched as normalizeArchiveName() does, or nullptr. */
    const Entry *entry(std::string_view name) const;
    /** One of entries()' files, decoded. The archive is opened for each read. */
    Error read(const Entry &entry, std::vector<uint8_t> &out) const;
    /** The key the files are XORed with, as the header gives it. */
    uint32_t key() const { return _key; }

    /**
     * Undoes a file's encoding: each whole 4 bytes XORed with the key, low
     * byte first; the last size % 4 bytes are stored as they are.
     */
    static void decode(std::vector<uint8_t> &data, uint32_t key);

private:
    std::filesystem::path _path;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> index into _entries
    uint32_t _key = 0;
};

} // namespace reluva::storm3d
