// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/zfs_archive.h"

#include "common/bytes.h"

#include <cstring>
#include <fstream>

namespace reluva::storm3d {

namespace {

constexpr size_t kHeaderSize = 28;
// After an entry's name: its offset, index, size, time and flags.
constexpr size_t kEntryFields = 20;
// Both of Armada's archives have 16 and 100; larger ones are refused as
// damage rather than read.
constexpr uint32_t kMaxNameLength = 256;
constexpr uint32_t kMaxPerBlock = 65536;

} // namespace

Error ZfsArchive::open(const std::filesystem::path &path) {
    _path = path;
    _entries.clear();
    _byName.clear();
    _key = 0;
    const std::string where = path.string();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return Error::fail("Cannot open " + where);
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    // Read whole to walk the tables: Armada's archives are 19 kB and 832 kB.
    std::vector<uint8_t> file(fileSize);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(file.data()), std::streamsize(fileSize)))
        return Error::fail("Cannot read " + where);
    if (fileSize < kHeaderSize || std::memcmp(file.data(), "ZFSF", 4) != 0)
        return Error::fail(where + " is not a ZFS archive");

    const uint32_t version = readLE32(&file[4]);
    const uint32_t nameLength = readLE32(&file[8]);
    const uint32_t perBlock = readLE32(&file[12]);
    const uint32_t count = readLE32(&file[16]);
    const uint32_t key = readLE32(&file[20]);
    uint32_t block = readLE32(&file[24]);
    if (version != 1)
        return Error::fail(where + ": ZFS version " + std::to_string(version) + " (only 1 is known)");
    if (nameLength == 0 || nameLength > kMaxNameLength || perBlock == 0 || perBlock > kMaxPerBlock)
        return Error::fail(where + ": names of " + std::to_string(nameLength) + " bytes in blocks of " +
                           std::to_string(perBlock) + " entries can't be right");
    const uint64_t entrySize = nameLength + kEntryFields;
    const uint64_t blockSize = 4 + entrySize * perBlock;

    // Each block names the next; they only go forward, so a loop can't be followed.
    uint64_t lastBlock = 0;
    while (block != 0) {
        if (block <= lastBlock || block + blockSize > fileSize)
            return Error::fail(where + ": a table at " + std::to_string(block) +
                               " is outside the file or out of order");
        lastBlock = block;
        for (uint32_t i = 0; i < perBlock; ++i) {
            const uint8_t *e = &file[block + 4 + i * entrySize];
            // A name may fill its field with no zero after it.
            const void *zero = std::memchr(e, 0, nameLength);
            const size_t length = zero ? size_t(static_cast<const uint8_t *>(zero) - e) : nameLength;
            if (length == 0)
                continue; // the last block's unused entries
            Entry entry;
            entry.name.assign(reinterpret_cast<const char *>(e), length);
            const uint8_t *fields = e + nameLength;
            entry.offset = readLE32(fields);
            entry.index = readLE32(fields + 4);
            entry.size = readLE32(fields + 8);
            entry.modified = readLE32(fields + 12);
            entry.flags = readLE32(fields + 16);
            if (uint64_t(entry.offset) + entry.size > fileSize)
                return Error::fail(where + ": " + entry.name + " runs past the end of the file");
            _byName.emplace(normalizeArchiveName(entry.name), _entries.size());
            _entries.push_back(std::move(entry));
        }
        block = readLE32(&file[block]);
    }
    if (_entries.size() != count)
        return Error::fail(where + ": the header counts " + std::to_string(count) + " files, the tables have " +
                           std::to_string(_entries.size()));
    _key = key;
    return Error::success();
}

std::vector<std::string> ZfsArchive::list() const {
    std::vector<std::string> names;
    names.reserve(_entries.size());
    for (const Entry &e : _entries)
        names.push_back(e.name);
    return names;
}

bool ZfsArchive::has(std::string_view name) const {
    return entry(name) != nullptr;
}

const ZfsArchive::Entry *ZfsArchive::entry(std::string_view name) const {
    const auto it = _byName.find(normalizeArchiveName(name));
    return it == _byName.end() ? nullptr : &_entries[it->second];
}

Error ZfsArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    const Entry *e = entry(name);
    if (!e)
        return Error::fail(std::string(name) + " isn't in " + _path.string());
    return read(*e, out);
}

Error ZfsArchive::read(const Entry &entry, std::vector<uint8_t> &out) const {
    const std::string which = _path.string() + ": " + entry.name;
    if (entry.flags != 0)
        return Error::fail(which + " has flags " + std::to_string(entry.flags) + ", which no archive seen has");
    std::ifstream in(_path, std::ios::binary);
    out.resize(entry.size);
    in.seekg(std::streamoff(entry.offset));
    if (!in || !in.read(reinterpret_cast<char *>(out.data()), std::streamsize(entry.size)))
        return Error::fail("Cannot read " + which);
    decode(out, _key);
    return Error::success();
}

void ZfsArchive::decode(std::vector<uint8_t> &data, uint32_t key) {
    const size_t whole = data.size() - data.size() % 4;
    for (size_t i = 0; i < whole; ++i)
        data[i] ^= uint8_t(key >> (8 * (i % 4)));
}

} // namespace reluva::storm3d
