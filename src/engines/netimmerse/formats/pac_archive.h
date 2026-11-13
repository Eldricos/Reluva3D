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

namespace reluva::netimmerse {

// A .pac archive, as LEGO Alpha Team keeps its data (Bhvr.pac, Etc.pac...):
// a header, a directory of names with their place in the file, then the
// files one after another. See docs/engines/netimmerse/games/legoalphateam/formats/pac.md.
//
// Each entry is the game's DDFileAttr (DDPackedDirFileLib): its data's
// 64-bit offset (high half first), size and date. A directory can hold
// subdirectories after its files; no archive does.
class PacArchive : public Archive {
public:
    struct Entry {
        std::string name;    // as stored, e.g. "ABADI MT CONDENSED.DDF"
        uint64_t offset = 0; // of the data, from the start of the archive (high half 0 in all)
        uint32_t size = 0;
        uint64_t modified = 0; // Windows FILETIME: 100 ns units since 1601
    };

    Error open(const std::filesystem::path &path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.string(); }

    // In the directory's order, which is the data's.
    const std::vector<Entry> &entries() const { return _entries; }
    const Entry *entry(std::string_view name) const;
    Error read(const Entry &entry, std::vector<uint8_t> &out) const;
    // Where the first file's data starts, as the header gives it.
    uint32_t dataStart() const { return _dataStart; }

    // A FILETIME as Unix time (seconds since 1970), 0 if before 1970.
    static int64_t unixTime(uint64_t fileTime);

private:
    std::filesystem::path _path;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> entries_ index
    uint32_t _dataStart = 0;
};

} // namespace reluva::netimmerse
