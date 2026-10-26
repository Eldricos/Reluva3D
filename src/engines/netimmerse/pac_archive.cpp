// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/pac_archive.h"

#include <cstring>
#include <fstream>

namespace reluva::netimmerse {

namespace {

constexpr size_t kHeaderSize = 20;
// After an entry's name and its terminating zero: flags, offset, size and
// the FILETIME.
constexpr size_t kEntryFields = 20;
// FILETIME of 1970-01-01, in 100 ns units since 1601.
constexpr uint64_t kUnixEpoch = 116444736000000000ULL;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

} // namespace

int64_t PacArchive::unixTime(uint64_t fileTime) {
    return fileTime < kUnixEpoch ? 0 : int64_t((fileTime - kUnixEpoch) / 10000000);
}

Error PacArchive::open(const std::filesystem::path& path) {
    path_ = path;
    entries_.clear();
    byName_.clear();
    dataStart_ = 0;
    const std::string where = path.string();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return Error::fail("Cannot open " + where);
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    uint8_t header[kHeaderSize];
    in.seekg(0);
    if (fileSize < kHeaderSize || !in.read(reinterpret_cast<char*>(header), kHeaderSize) ||
        std::memcmp(header, "PACK", 4) != 0)
        return Error::fail(where + " is not a .pac archive");

    const uint32_t totalSize = le32(header + 4);
    const uint32_t dataStart = le32(header + 12);
    const uint32_t count = le32(header + 16);
    if (totalSize != fileSize)
        return Error::fail(where + ": the header gives a size of " + std::to_string(totalSize) + ", the file has " +
                           std::to_string(fileSize) + " bytes");
    if (dataStart < kHeaderSize || dataStart > fileSize)
        return Error::fail(where + ": data start " + std::to_string(dataStart) + " is outside the file");
    // Each entry takes at least a one-letter name, its zero and the fields.
    if (count > (dataStart - kHeaderSize) / (2 + kEntryFields))
        return Error::fail(where + ": " + std::to_string(count) + " files can't fit in the directory");

    std::vector<uint8_t> dir(dataStart - kHeaderSize);
    if (!in.read(reinterpret_cast<char*>(dir.data()), std::streamsize(dir.size())))
        return Error::fail("Cannot read " + where);

    size_t pos = 0;
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const std::string which = where + ": file " + std::to_string(i);
        const void* zero = std::memchr(dir.data() + pos, 0, dir.size() - pos);
        if (!zero)
            return Error::fail(which + "'s name runs past the directory");
        const size_t nameEnd = size_t(static_cast<const uint8_t*>(zero) - dir.data());
        if (nameEnd == pos)
            return Error::fail(which + " has no name");
        if (nameEnd + 1 + kEntryFields > dir.size())
            return Error::fail(which + " runs past the directory");
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(dir.data() + pos), nameEnd - pos);
        const uint8_t* f = dir.data() + nameEnd + 1;
        e.flags = le32(f);
        e.offset = le32(f + 4);
        e.size = le32(f + 8);
        e.modified = le64(f + 12);
        pos = nameEnd + 1 + kEntryFields;
        // Ambient.pac and LMap.pac list themselves (the packer must have
        // seen its own output file) with offset, size and date all 0: an
        // empty file, then.
        const bool placeholder = e.offset == 0 && e.size == 0;
        if (!placeholder && (e.offset < dataStart || uint64_t(e.offset) + e.size > fileSize))
            return Error::fail(which + " (" + e.name + ") lies outside the archive's data");
        // Names are unique in the archives seen; should one repeat, the
        // first is the one found by name.
        byName_.emplace(normalizeArchiveName(e.name), entries_.size());
        entries_.push_back(std::move(e));
    }
    dataStart_ = dataStart;
    return Error::success();
}

std::vector<std::string> PacArchive::list() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const Entry& e : entries_)
        names.push_back(e.name);
    return names;
}

const PacArchive::Entry* PacArchive::entry(std::string_view name) const {
    auto it = byName_.find(normalizeArchiveName(name));
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

bool PacArchive::has(std::string_view name) const { return entry(name) != nullptr; }

Error PacArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    const Entry* e = entry(name);
    if (!e)
        return Error::fail(std::string(name) + " not found in " + path_.string());
    return read(*e, out);
}

Error PacArchive::read(const Entry& e, std::vector<uint8_t>& out) const {
    const std::string where = path_.string() + ": " + e.name;
    if (e.flags != 0)
        return Error::fail(where + ": unknown flags " + std::to_string(e.flags) + " (compressed?)");
    // Opened per read so a const archive can be read from several threads.
    std::ifstream in(path_, std::ios::binary);
    out.resize(e.size);
    in.seekg(std::streamoff(e.offset));
    if (!in || (e.size && !in.read(reinterpret_cast<char*>(out.data()), std::streamsize(e.size))))
        return Error::fail("Cannot read " + where);
    return Error::success();
}

} // namespace reluva::netimmerse
