// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/cat_archive.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace reluva::newworlds {

namespace {

constexpr size_t kHeaderSize = 12; // root chunk header + table chunk header
constexpr size_t kFileEntrySize = 334;
constexpr size_t kContainerEntrySize = 264;
constexpr size_t kPathLen = 260;
constexpr size_t kModifiedOffset = 8 + 0x11e;

uint16_t rd16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}
uint32_t rd32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::string hex(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace

bool isContainerTag(uint16_t tag) {
    return tag == cattag::kGroup || tag == cattag::kFontPart || tag == cattag::kFontSet || tag == cattag::kTextureSet;
}

std::string CatArchive::normalizeName(std::string_view originalPath) {
    std::string s = fs::toLower(originalPath);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.size() >= 3 && s[1] == ':' && s[2] == '/')
        s.erase(0, 3);
    if (size_t p = s.find("/pc_win32/"); p != std::string::npos)
        s.erase(0, p + 10);
    else if (s.rfind("prjs/", 0) == 0)
        s.erase(0, 5);
    return s;
}

Error CatArchive::open(const std::filesystem::path &path) {
    _path = path;
    _entries.clear();
    _byName.clear();
    _file.close();
    _file.clear();
    _file.open(path, std::ios::binary);
    if (!_file)
        return Error::fail("Cannot open " + path.string());

    const std::string where = path.filename().string() + ": ";
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < kHeaderSize + 4)
        return Error::fail(where + "too small to be an archive");

    uint8_t header[kHeaderSize];
    _file.read(reinterpret_cast<char *>(header), kHeaderSize);
    if (!_file || rd16(header) != cattag::kRoot || rd32(header + 2) + 6ULL != fileSize ||
        rd16(header + 6) != cattag::kTable)
        return Error::fail(where + "not a New Worlds archive");

    const uint32_t tableSize = rd32(header + 8);
    if (kHeaderSize + uint64_t(tableSize) > fileSize || tableSize < 4)
        return Error::fail(where + "table size out of range");
    std::vector<uint8_t> table(tableSize);
    _file.read(reinterpret_cast<char *>(table.data()), tableSize);
    if (!_file)
        return Error::fail(where + "cannot read table");

    const uint32_t count = rd32(table.data());
    size_t pos = 4;
    uint64_t expectedChunk = kHeaderSize + uint64_t(tableSize);
    std::vector<std::pair<int, uint64_t>> open; // container index, end of its chunk

    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 8 > tableSize)
            return Error::fail(where + "table ends inside entry " + std::to_string(i));
        const uint8_t *e = table.data() + pos;
        // Offsets are relative to the file without the table's contents.
        const uint64_t chunk = uint64_t(rd32(e)) + tableSize;
        if (chunk != expectedChunk)
            return Error::fail(where + "entry " + std::to_string(i) + " points at " + hex(chunk) + ", expected " +
                               hex(expectedChunk));

        uint8_t chunkHeader[6];
        _file.seekg(std::streamoff(chunk));
        _file.read(reinterpret_cast<char *>(chunkHeader), 6);
        if (!_file)
            return Error::fail(where + "cannot read chunk of entry " + std::to_string(i));

        CatEntry entry;
        entry.tag = rd16(chunkHeader);
        entry.size = rd32(chunkHeader + 2);
        entry.offset = chunk + 6;
        entry.container = isContainerTag(entry.tag);
        if (entry.offset + entry.size > fileSize)
            return Error::fail(where + "entry " + std::to_string(i) + " extends past the end of the file");

        while (!open.empty() && chunk >= open.back().second)
            open.pop_back();
        entry.parent = open.empty() ? -1 : open.back().first;

        const size_t entrySize = entry.container ? kContainerEntrySize : kFileEntrySize;
        if (pos + entrySize > tableSize)
            return Error::fail(where + "table ends inside entry " + std::to_string(i));
        if (entry.container) {
            open.push_back({int(_entries.size()), entry.offset + entry.size});
            expectedChunk = entry.offset;
        } else {
            const char *name = reinterpret_cast<const char *>(e + 8);
            entry.originalPath.assign(name, strnlen(name, kPathLen));
            entry.name = normalizeName(entry.originalPath);
            entry.modified = rd32(e + kModifiedOffset);
            expectedChunk = entry.offset + entry.size;
            if (!entry.name.empty())
                _byName.emplace(entry.name, _entries.size());
        }
        _entries.push_back(std::move(entry));
        pos += entrySize;
    }

    if (pos != tableSize)
        return Error::fail(where + "table has " + std::to_string(tableSize - pos) + " unexplained bytes");
    return Error::success();
}

const CatEntry *CatArchive::find(std::string_view name) const {
    std::string key = fs::toLower(name);
    std::replace(key.begin(), key.end(), '\\', '/');
    auto it = _byName.find(key);
    return it == _byName.end() ? nullptr : &_entries[it->second];
}

Error CatArchive::read(const CatEntry &entry, std::vector<uint8_t> &out) {
    out.resize(entry.size);
    _file.clear();
    _file.seekg(std::streamoff(entry.offset));
    _file.read(reinterpret_cast<char *>(out.data()), std::streamsize(entry.size));
    if (!_file)
        return Error::fail("Cannot read " + entry.name + " from " + _path.filename().string());
    return Error::success();
}

} // namespace reluva::newworlds
