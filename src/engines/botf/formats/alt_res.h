// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::botf {

/**
 * Decompresses one file of alt.res's LZSS (docs/engines/botf/formats/alt-res.md):
 * a 4 KB window filled with `windowFill` (0 in alt.res; spaces for a saved
 * game's text files) written from position 1; a flag byte per 8 items, low
 * bit first, 1 a literal byte, 0 a reference `a b` copying (a >> 4) + 2 bytes
 * from window position (a & 0x0F) << 8 | b. Stops after `size` bytes; fails
 * if `data` runs out first. `used` gets the number of input bytes read.
 */
Error decompressLzss(const uint8_t *data, size_t dataSize, size_t size, std::vector<uint8_t> &out,
                     size_t *used = nullptr, uint8_t windowFill = 0);

/**
 * alt.res: a header (name, directory offset, file count), each file's LZSS
 * data, and a directory of 40-byte entries (docs/engines/botf/formats/alt-res.md).
 * Saved games are the same archive (docs/engines/botf/formats/sav.md). The
 * file stays open by path; each read() loads one file's data.
 */
class AltResArchive : public Archive {
public:
    struct Entry {
        std::string name;
        uint32_t offset = 0; // of the compressed data, in the file
        uint32_t index = 0;
        uint32_t size = 0; // decoded
        uint32_t compressedSize = 0;
        uint32_t flag = 0;  // 1 in every retail entry
        uint32_t check = 0; // a 16-bit value, how it's made is unknown
    };

    Error open(const std::filesystem::path &path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.string(); }

    const std::vector<Entry> &entries() const { return _entries; }
    /** The header's name: "alt.res", or a saved game's name. */
    const std::string &name() const { return _name; }
    /** One file's data; `windowFill` as decompressLzss() takes it. */
    Error read(const Entry &entry, std::vector<uint8_t> &out, uint8_t windowFill = 0) const;

private:
    std::filesystem::path _path;
    std::string _name;
    uint64_t _fileSize = 0;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> entries_ index
};

} // namespace reluva::botf
