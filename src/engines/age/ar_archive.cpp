// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's archives; see
// docs/formats/age/ar.md.

#include "engines/age/ar_archive.h"

#include "common/zip.h"

#include <fstream>

namespace reluva::age {

namespace {

constexpr size_t kHeaderSize = 2048; // the directory starts here
constexpr size_t kEntrySize = 16;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAt(std::ifstream& in, uint64_t offset, uint8_t* out, size_t size) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size)));
}

} // namespace

Error ArArchive::open(const std::filesystem::path& path) {
    path_ = path;
    entries_.clear();
    byName_.clear();
    const std::string where = path.string();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return Error::fail("Cannot open " + where);
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    uint8_t header[16];
    if (fileSize < kHeaderSize || !readAt(in, 0, header, sizeof header))
        return Error::fail(where + " is too small to be an .ar archive");
    if (header[0] != 'D' || header[1] != 'A' || header[2] != 'V' || header[3] != 'E')
        return Error::fail(where + " is not an .ar archive (no DAVE header)");
    const uint32_t count = le32(header + 4);
    const uint32_t dirSize = le32(header + 8);
    const uint32_t namesSize = le32(header + 12);
    if (uint64_t(count) * kEntrySize > dirSize || kHeaderSize + uint64_t(dirSize) + namesSize > fileSize)
        return Error::fail(where + ": directory runs past the end of the file");

    std::vector<uint8_t> dir(dirSize), names(namesSize);
    if ((dirSize && !readAt(in, kHeaderSize, dir.data(), dirSize)) ||
        (namesSize && !readAt(in, kHeaderSize + dirSize, names.data(), namesSize)))
        return Error::fail("Cannot read " + where);

    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* d = &dir[i * kEntrySize];
        const uint32_t nameAt = le32(d);
        size_t nameEnd = nameAt;
        while (nameEnd < names.size() && names[nameEnd] != 0)
            ++nameEnd;
        if (nameEnd >= names.size())
            return Error::fail(where + ": damaged name table");
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(&names[nameAt]), nameEnd - nameAt);
        e.offset = le32(d + 4);
        e.size = le32(d + 8);
        e.storedSize = le32(d + 12);
        if (e.name.empty() || e.name.back() == '/')
            continue; // a folder
        if (uint64_t(e.offset) + e.storedSize > fileSize)
            return Error::fail(where + ": " + e.name + " runs past the end of the file");
        if (byName_.try_emplace(normalizeArchiveName(e.name), entries_.size()).second)
            entries_.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> ArArchive::list() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const Entry& e : entries_)
        names.push_back(e.name);
    return names;
}

const ArArchive::Entry* ArArchive::entry(std::string_view name) const {
    auto it = byName_.find(normalizeArchiveName(name));
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

bool ArArchive::has(std::string_view name) const { return entry(name) != nullptr; }

Error ArArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    const Entry* e = entry(name);
    if (!e)
        return Error::fail(std::string(name) + " not found in " + path_.string());
    return read(*e, out);
}

Error ArArchive::read(const Entry& e, std::vector<uint8_t>& out) const {
    const std::string where = path_.string() + ": " + e.name;
    // Opened per read so a const archive can be read from several threads.
    std::ifstream in(path_, std::ios::binary);
    std::vector<uint8_t> raw(e.storedSize);
    if (!in || (e.storedSize && !readAt(in, e.offset, raw.data(), raw.size())))
        return Error::fail(where + ": truncated");
    if (!e.deflated()) {
        out = std::move(raw);
        return Error::success();
    }
    if (Error err = inflateRaw(raw.data(), raw.size(), e.size, out))
        return Error::fail(where + ": " + err.message);
    return Error::success();
}

} // namespace reluva::age
