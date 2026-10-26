// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::nfs {

// A .viv archive (EA's "BIGF"), as docs/formats/nfs/viv.md describes. The
// archives are small (the largest retail one is about 1.6 MB), so the whole
// file is kept in memory.
class VivArchive : public Archive {
public:
    struct Entry {
        std::string name; // as stored
        uint32_t offset = 0, size = 0;
    };

    Error open(const std::filesystem::path& path);
    // An archive already in memory; `name` is for messages.
    Error open(std::vector<uint8_t> data, std::string name);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t>& out) const override;
    std::string describe() const override { return name_; }

    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::string name_;
    std::vector<uint8_t> data_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byName_; // normalized name -> entries_ index
};

} // namespace reluva::nfs
