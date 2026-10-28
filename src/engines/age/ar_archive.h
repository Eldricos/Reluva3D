// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::age {

// An Angel Game Engine .ar archive ("DAVE"), as Midtown Madness 2's
// mm2core.ar, mm2tex.ar, mm2aud.ar and mm2audex.ar. See
// docs/formats/age/ar.md. Folder entries are left out of list().
class ArArchive : public Archive {
public:
    struct Entry {
        std::string name;    // as stored, '/' separators
        uint32_t offset = 0; // of the data, from the start of the file
        uint32_t size = 0;
        uint32_t storedSize = 0; // differs from size when deflated
        bool deflated() const { return storedSize != size; }
    };

    Error open(const std::filesystem::path& path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t>& out) const override;
    std::string describe() const override { return path_.string(); }

    // Every file in the archive's order, folders left out.
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* entry(std::string_view name) const;
    Error read(const Entry& entry, std::vector<uint8_t>& out) const;

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byName_; // normalized name -> entries_ index
};

} // namespace reluva::age
