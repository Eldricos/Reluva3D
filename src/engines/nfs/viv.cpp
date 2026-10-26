// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viv.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::nfs {

namespace {

uint32_t be32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | p[3];
}

} // namespace

Error VivArchive::open(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Can't open " + path.string());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return open(std::move(data), path.string());
}

Error VivArchive::open(std::vector<uint8_t> data, std::string name) {
    name_ = std::move(name);
    data_ = std::move(data);
    entries_.clear();
    byName_.clear();

    auto bad = [&](const std::string& why) { return Error::fail(name_ + " is not a valid .viv archive: " + why); };
    if (data_.size() < 16 || std::memcmp(data_.data(), "BIGF", 4) != 0)
        return bad("no BIGF header");
    const uint32_t count = be32(&data_[8]);
    const uint32_t firstFile = be32(&data_[12]);
    if (firstFile > data_.size())
        return bad("its directory runs past the end");

    // Each entry: offset and size (big-endian), then the name and a 0.
    size_t pos = 16;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 9 > firstFile)
            return bad("its directory runs past the files");
        Entry e;
        e.offset = be32(&data_[pos]);
        e.size = be32(&data_[pos + 4]);
        pos += 8;
        const uint8_t* end = static_cast<const uint8_t*>(std::memchr(&data_[pos], 0, firstFile - pos));
        if (!end)
            return bad("an unterminated name");
        e.name.assign(reinterpret_cast<const char*>(&data_[pos]), end - &data_[pos]);
        pos += e.name.size() + 1;
        if (static_cast<uint64_t>(e.offset) + e.size > data_.size())
            return bad(e.name + " runs past the end");
        byName_.emplace(normalizeArchiveName(e.name), entries_.size());
        entries_.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> VivArchive::list() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const Entry& e : entries_)
        names.push_back(e.name);
    return names;
}

bool VivArchive::has(std::string_view name) const { return byName_.count(normalizeArchiveName(name)) != 0; }

Error VivArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    auto it = byName_.find(normalizeArchiveName(name));
    if (it == byName_.end())
        return Error::fail(std::string(name) + " is not in " + name_);
    const Entry& e = entries_[it->second];
    out.assign(data_.begin() + e.offset, data_.begin() + e.offset + e.size);
    return Error::success();
}

} // namespace reluva::nfs
