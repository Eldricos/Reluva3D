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
// files one after another. See docs/formats/alphateam/pac.md.
//
// Every entry seen has 0 in its flags word; what other values would mean
// (compression?) isn't known, so such entries are listed but not read.
class PacArchive : public Archive {
public:
    struct Entry {
        std::string name;       // as stored, e.g. "ABADI MT CONDENSED.DDF"
        uint32_t flags = 0;     // 0 in every archive seen
        uint32_t offset = 0;    // of the data, from the start of the archive
        uint32_t size = 0;
        uint64_t modified = 0;  // Windows FILETIME: 100 ns units since 1601
    };

    Error open(const std::filesystem::path& path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t>& out) const override;
    std::string describe() const override { return path_.string(); }

    // In the directory's order, which is the data's.
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* entry(std::string_view name) const;
    Error read(const Entry& entry, std::vector<uint8_t>& out) const;
    // Where the first file's data starts, as the header gives it.
    uint32_t dataStart() const { return dataStart_; }

    // A FILETIME as Unix time (seconds since 1970), 0 if before 1970.
    static int64_t unixTime(uint64_t fileTime);

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byName_; // normalized name -> entries_ index
    uint32_t dataStart_ = 0;
};

} // namespace reluva::netimmerse
