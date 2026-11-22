// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::legoloco {

/**
 * Unpacks one compressed entry of the resource file: its unpacked size, the
 * root of a table of Huffman tree nodes, then the bits
 * (docs/engines/legoloco/formats/resource.md).
 */
Error unpackResource(std::span<const uint8_t> packed, std::vector<uint8_t> &out);

/**
 * The resource file: `resource.RFH`, the index, and `resource.RFD`, the
 * entries one after another in the index's order
 * (docs/engines/legoloco/formats/resource.md). The index is read when it's
 * opened; the data file is opened for each read. Names are the index's,
 * with '\' between folders; lookups take '/' or '\', in any case.
 */
class ResourceFile : public Archive {
public:
    /** One file in the index. */
    struct Entry {
        std::string name;    // as the index has it: "roads\half-vwint.dat"
        uint64_t offset = 0; // in resource.RFD
        uint32_t size = 0;   // bytes stored
        bool compressed = false;
    };

    /** Opens the folder's `resource.RFH` and `resource.RFD` (`art-res`, found in any case). */
    Error open(const std::filesystem::path &folder);
    /** Opens an index and its data file. */
    Error open(const std::filesystem::path &rfh, const std::filesystem::path &rfd);

    /** Every entry, in the index's order. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** The entry of a name, or -1. */
    int find(std::string_view name) const;
    /** An entry's bytes as stored, compressed or not. */
    Error readRaw(size_t entry, std::vector<uint8_t> &out) const;
    /** An entry's contents, unpacked if it's compressed. */
    Error readEntry(size_t entry, std::vector<uint8_t> &out) const;

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override { return find(name) >= 0; }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _rfd.string(); }

private:
    std::filesystem::path _rfd;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalizeArchiveName -> entry
};

} // namespace reluva::legoloco
