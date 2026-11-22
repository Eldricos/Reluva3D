// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::tpw {

/**
 * A Theme Park World engine archive (`.wad`, "DWFB"), as SimCoaster's
 * `Data/ui.wad` and its rides'. See
 * docs/engines/tpw/games/simcoaster/formats/wad.md.
 */
class WadArchive : public Archive {
public:
    /** A file's record. */
    struct Entry {
        std::string name;    // its folder and name, '/' between, as stored
        uint32_t hash = 0;   // the record's first word: not worked out
        uint32_t offset = 0; // of its data, from the start of the archive
        uint32_t packedSize = 0;
        uint32_t size = 0; // unpacked
        bool refPack = false;
    };

    /**
     * Reads the archive's records and names; files are read from it when
     * asked for. Fails without "DWFB", or on a record or name past the
     * file's end.
     * @param prefix Put before every name ("levels/water/rides/TourRide"
     *     for that archive, the folder it stands for); empty for none.
     */
    Error open(const std::filesystem::path &path, const std::string &prefix = {});

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.string(); }

    /** Every file in the archive's order, names without the prefix. */
    const std::vector<Entry> &entries() const { return _entries; }
    /**
     * A file's contents, unpacked if need be. Opens the archive for each
     * read, so a const archive can be read from several threads at once.
     * Fails if it doesn't unpack to its recorded size.
     */
    Error read(const Entry &entry, std::vector<uint8_t> &out) const;

private:
    std::filesystem::path _path;
    std::string _prefix; // with its '/' at the end, or empty
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name with the prefix -> _entries index
};

} // namespace reluva::tpw
