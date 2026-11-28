// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/gob.h"

#include "common/bytes.h"

#include <cstring>
#include <fstream>

namespace reluva::sith {

Error GobArchive::open(const std::filesystem::path &path) {
    _path = path;
    _entries.clear();
    _byName.clear();
    auto bad = [&](const std::string &why) { return Error::fail(path.string() + " is not a .gob archive: " + why); };

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Can't open " + path.string());
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    uint8_t header[kGobHeaderSize];
    if (ec || !in.read(reinterpret_cast<char *>(header), sizeof(header)))
        return bad("too short");
    if (std::memcmp(header, "GOB ", 4) != 0)
        return bad("no GOB header");
    if (readLE32(header + 4) != kGobVersion)
        return bad("version " + std::to_string(readLE32(header + 4)) + ", not 20");

    const uint32_t indexAt = readLE32(header + 8);
    uint8_t countBytes[4];
    in.seekg(indexAt);
    if (!in.read(reinterpret_cast<char *>(countBytes), 4))
        return bad("its index is past the end");
    const uint32_t count = readLE32(countBytes);
    if (indexAt + 4 + uint64_t(count) * kGobEntrySize > fileSize)
        return bad("its index runs past the end");

    std::vector<uint8_t> index(size_t(count) * kGobEntrySize);
    if (!in.read(reinterpret_cast<char *>(index.data()), std::streamsize(index.size())))
        return bad("its index can't be read");
    _entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = &index[size_t(i) * kGobEntrySize];
        Entry e;
        e.offset = readLE32(p);
        e.size = readLE32(p + 4);
        const char *name = reinterpret_cast<const char *>(p + 8);
        e.name.assign(name, strnlen(name, kGobNameSize));
        if (uint64_t(e.offset) + e.size > fileSize)
            return bad(e.name + " runs past the end");
        _byName.emplace(normalizeArchiveName(e.name), _entries.size());
        _entries.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> GobArchive::list() const {
    std::vector<std::string> names;
    names.reserve(_entries.size());
    for (const Entry &e : _entries)
        names.push_back(e.name);
    return names;
}

bool GobArchive::has(std::string_view name) const {
    return _byName.count(normalizeArchiveName(name)) != 0;
}

Error GobArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    auto it = _byName.find(normalizeArchiveName(name));
    if (it == _byName.end())
        return Error::fail(std::string(name) + " is not in " + describe());
    const Entry &e = _entries[it->second];
    std::ifstream in(_path, std::ios::binary);
    out.resize(e.size);
    in.seekg(e.offset);
    if (!in || !in.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()))) {
        out.clear();
        return Error::fail("Can't read " + e.name + " from " + _path.string());
    }
    return Error::success();
}

} // namespace reluva::sith
