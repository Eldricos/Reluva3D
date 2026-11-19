// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/formats/viv.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::nfs {

namespace {

uint32_t be32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
           p[3];
}

} // namespace

Error VivArchive::open(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Can't open " + path.string());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return open(std::move(data), path.string());
}

Error VivArchive::open(std::vector<uint8_t> data, std::string name) {
    _name = std::move(name);
    _data = std::move(data);
    _entries.clear();
    _byName.clear();

    auto bad = [&](const std::string &why) { return Error::fail(_name + " is not a valid .viv archive: " + why); };
    if (_data.size() < 16 || std::memcmp(_data.data(), "BIGF", 4) != 0)
        return bad("no BIGF header");
    const uint32_t count = be32(&_data[8]);
    const uint32_t firstFile = be32(&_data[12]);
    if (firstFile > _data.size())
        return bad("its directory runs past the end");

    // Each entry: offset and size (big-endian), then the name and a 0.
    size_t pos = 16;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 9 > firstFile)
            return bad("its directory runs past the files");
        Entry e;
        e.offset = be32(&_data[pos]);
        e.size = be32(&_data[pos + 4]);
        pos += 8;
        const uint8_t *end = static_cast<const uint8_t *>(std::memchr(&_data[pos], 0, firstFile - pos));
        if (!end)
            return bad("an unterminated name");
        e.name.assign(reinterpret_cast<const char *>(&_data[pos]), end - &_data[pos]);
        pos += e.name.size() + 1;
        if (static_cast<uint64_t>(e.offset) + e.size > _data.size())
            return bad(e.name + " runs past the end");
        _byName.emplace(normalizeArchiveName(e.name), _entries.size());
        _entries.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> VivArchive::list() const {
    std::vector<std::string> names;
    names.reserve(_entries.size());
    for (const Entry &e : _entries)
        names.push_back(e.name);
    return names;
}

bool VivArchive::has(std::string_view name) const {
    return _byName.count(normalizeArchiveName(name)) != 0;
}

Error VivArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    auto it = _byName.find(normalizeArchiveName(name));
    if (it == _byName.end())
        return Error::fail(std::string(name) + " is not in " + _name);
    const Entry &e = _entries[it->second];
    out.assign(_data.begin() + e.offset, _data.begin() + e.offset + e.size);
    return Error::success();
}

} // namespace reluva::nfs
