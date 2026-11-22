// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 300 archives', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/wad.md.

#include "engines/tpw/formats/wad.h"

#include "common/refpack.h"

#include <fstream>

namespace reluva::tpw {

namespace {

constexpr size_t kHeader = 0x58;
constexpr size_t kRecord = 0x28;
constexpr uint32_t kStored = 0;
constexpr uint32_t kRefPack = 4;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

bool readAt(std::ifstream &in, uint64_t offset, size_t size, std::vector<uint8_t> &out) {
    out.resize(size);
    in.clear();
    return in.seekg(std::streamoff(offset)) && in.read(reinterpret_cast<char *>(out.data()), std::streamsize(size));
}

} // namespace

Error WadArchive::open(const std::filesystem::path &path, const std::string &prefix) {
    _path = path;
    _prefix = prefix.empty() || prefix.back() == '/' ? prefix : prefix + "/";
    _entries.clear();
    _byName.clear();
    const std::string name = path.filename().string();
    std::ifstream in(path, std::ios::binary);
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    std::vector<uint8_t> header;
    if (!in || ec || !readAt(in, 0, kHeader, header))
        return Error::fail("Couldn't read " + path.string());
    if (header[0] != 'D' || header[1] != 'W' || header[2] != 'F' || header[3] != 'B')
        return Error::fail(name + " isn't a .wad archive");
    const uint32_t count = le32(header, 0x48);
    const uint32_t namesAt = le32(header, 0x4c);
    const uint32_t namesSize = le32(header, 0x50);
    std::vector<uint8_t> records;
    std::vector<uint8_t> names;
    if (kHeader + uint64_t(count) * kRecord > fileSize || uint64_t(namesAt) + namesSize > fileSize ||
        !readAt(in, kHeader, size_t(count) * kRecord, records) || !readAt(in, namesAt, namesSize, names))
        return Error::fail(name + " is damaged");

    // A name with a folder sets the folder of the names after it, which
    // have none of their own.
    std::string folder;
    _entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t r = size_t(i) * kRecord;
        Entry e;
        e.hash = le32(records, r);
        const uint32_t nameAt = le32(records, r + 4);
        const uint32_t nameSize = le32(records, r + 8);
        e.offset = le32(records, r + 12);
        e.packedSize = le32(records, r + 16);
        const uint32_t kind = le32(records, r + 20);
        e.refPack = kind == kRefPack;
        e.size = e.refPack ? le32(records, r + 24) : e.packedSize;
        if ((kind != kStored && !e.refPack) || nameAt < namesAt ||
            uint64_t(nameAt) + nameSize > uint64_t(namesAt) + namesSize || uint64_t(e.offset) + e.packedSize > fileSize)
            return Error::fail(name + " is damaged (file " + std::to_string(i) + ")");
        std::string stored(reinterpret_cast<const char *>(names.data()) + (nameAt - namesAt), nameSize);
        stored.resize(stored.find('\0') == std::string::npos ? stored.size() : stored.find('\0'));
        for (char &c : stored)
            if (c == '\\')
                c = '/';
        if (const size_t slash = stored.rfind('/'); slash != std::string::npos)
            folder = stored.substr(0, slash + 1);
        else
            stored = folder + stored;
        e.name = std::move(stored);
        _byName.emplace(normalizeArchiveName(_prefix + e.name), _entries.size());
        _entries.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> WadArchive::list() const {
    std::vector<std::string> out;
    out.reserve(_entries.size());
    for (const Entry &e : _entries)
        out.push_back(_prefix + e.name);
    return out;
}

bool WadArchive::has(std::string_view name) const {
    return _byName.count(normalizeArchiveName(name)) != 0;
}

Error WadArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    const auto it = _byName.find(normalizeArchiveName(name));
    if (it == _byName.end())
        return Error::fail(std::string(name) + " isn't in " + _path.filename().string());
    return read(_entries[it->second], out);
}

Error WadArchive::read(const Entry &entry, std::vector<uint8_t> &out) const {
    out.clear();
    std::ifstream in(_path, std::ios::binary);
    std::vector<uint8_t> packed;
    if (!in || !readAt(in, entry.offset, entry.packedSize, packed))
        return Error::fail("Couldn't read " + entry.name + " from " + _path.string());
    if (!entry.refPack) {
        out = std::move(packed);
        return Error::success();
    }
    if (Error err = decompressRefPack(packed, out))
        return Error::fail(entry.name + " in " + _path.filename().string() + ": " + err.message);
    if (out.size() != entry.size)
        return Error::fail(entry.name + " in " + _path.filename().string() + " isn't the size its record gives");
    return Error::success();
}

} // namespace reluva::tpw
