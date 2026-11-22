// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/resource_file.h"

#include "common/bytes.h"
#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace reluva::legoloco {

namespace {

// The tree table always has room for 512 nodes, used or not: 0-255 are the
// byte values (their slots stay zero), 256 up the inner nodes.
constexpr size_t kNodes = 512;
constexpr size_t kPackedHeader = 8 + kNodes * 4;
// Larger than any entry the CD has (its largest unpacks to under 1 MB), so
// that a damaged size can't ask for gigabytes.
constexpr uint32_t kMaxUnpacked = 64u << 20;

} // namespace

Error unpackResource(std::span<const uint8_t> packed, std::vector<uint8_t> &out) {
    if (packed.size() < kPackedHeader)
        return Error::fail("compressed entry too short for its header");
    const uint32_t size = readLE32(packed.data());
    const uint32_t root = readLE32(packed.data() + 4);
    if (size > kMaxUnpacked)
        return Error::fail("compressed entry claims " + std::to_string(size) + " bytes");
    // A tree of one byte value would have a leaf as its root; no entry has one.
    if (root < 256 || root >= kNodes)
        return Error::fail("compressed entry's root node " + std::to_string(root) + " isn't an inner node");

    const uint8_t *table = packed.data() + 8;
    const uint64_t endBit = uint64_t(packed.size()) * 8;
    uint64_t bit = uint64_t(kPackedHeader) * 8;
    out.clear();
    out.reserve(size);
    while (out.size() < size) {
        uint32_t node = root;
        while (node >= 256) {
            if (bit >= endBit)
                return Error::fail("compressed entry ends after " + std::to_string(out.size()) + " of " +
                                   std::to_string(size) + " bytes");
            // Bits are taken from each byte's lowest first; 0 is the left child.
            const uint32_t which = (packed[size_t(bit >> 3)] >> (bit & 7)) & 1;
            ++bit;
            node = readLE16(table + node * 4 + which * 2);
            if (node >= kNodes)
                return Error::fail("compressed entry's tree points at node " + std::to_string(node));
        }
        out.push_back(uint8_t(node));
    }
    return Error::success();
}

Error ResourceFile::open(const std::filesystem::path &folder) {
    const auto rfh = fs::findFile(folder, "resource.rfh");
    const auto rfd = fs::findFile(folder, "resource.rfd");
    if (!rfh || !rfd)
        return Error::fail("no resource.RFH and resource.RFD in " + folder.string());
    return open(*rfh, *rfd);
}

Error ResourceFile::open(const std::filesystem::path &rfh, const std::filesystem::path &rfd) {
    _rfd = rfd;
    _entries.clear();
    _byName.clear();
    std::ifstream in(rfh, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + rfh.string());
    const std::vector<uint8_t> index((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::error_code ec;
    const uint64_t dataSize = std::filesystem::file_size(rfd, ec);
    if (ec)
        return Error::fail("can't open " + rfd.string());
    auto bad = [&](size_t at, const std::string &why) {
        return Error::fail(rfh.string() + ": " + why + " at " + std::to_string(at));
    };

    uint64_t offset = 0;
    for (size_t p = 0; p < index.size();) {
        if (index.size() - p < 4)
            return bad(p, "an entry cut short");
        // The name's length counts its terminating zero.
        const uint32_t nameLength = readLE32(&index[p]);
        if (nameLength == 0 || index.size() - p - 4 < uint64_t(nameLength) + 8)
            return bad(p, "an entry cut short");
        const auto nameStart = index.begin() + std::ptrdiff_t(p + 4);
        Entry e;
        e.name.assign(nameStart, std::find(nameStart, nameStart + nameLength, uint8_t(0)));
        p += 4 + nameLength;
        e.size = readLE32(&index[p]);
        const uint32_t flag = readLE32(&index[p + 4]);
        p += 8;
        if (flag > 1)
            return bad(p - 4, "flag " + std::to_string(flag) + ", neither 0 nor 1,");
        e.compressed = flag == 1;
        e.offset = offset;
        offset += e.size;
        if (offset > dataSize)
            return Error::fail(rfd.string() + ": shorter than its index says (" + e.name + ")");
        _byName.emplace(normalizeArchiveName(e.name), _entries.size());
        _entries.push_back(std::move(e));
    }
    return Error::success();
}

int ResourceFile::find(std::string_view name) const {
    const auto it = _byName.find(normalizeArchiveName(name));
    return it == _byName.end() ? -1 : int(it->second);
}

Error ResourceFile::readRaw(size_t entry, std::vector<uint8_t> &out) const {
    if (entry >= _entries.size())
        return Error::fail("no entry " + std::to_string(entry) + " in " + _rfd.string());
    const Entry &e = _entries[entry];
    std::ifstream in(_rfd, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + _rfd.string());
    out.resize(e.size);
    in.seekg(std::streamoff(e.offset));
    if (!in.read(reinterpret_cast<char *>(out.data()), std::streamsize(e.size)))
        return Error::fail(_rfd.string() + ": can't read " + e.name);
    return Error::success();
}

Error ResourceFile::readEntry(size_t entry, std::vector<uint8_t> &out) const {
    if (!(entry < _entries.size() && _entries[entry].compressed))
        return readRaw(entry, out);
    std::vector<uint8_t> packed;
    if (Error err = readRaw(entry, packed))
        return err;
    if (Error err = unpackResource(packed, out))
        return Error::fail(_entries[entry].name + ": " + err.message);
    return Error::success();
}

std::vector<std::string> ResourceFile::list() const {
    std::vector<std::string> names;
    names.reserve(_entries.size());
    for (const Entry &e : _entries)
        names.push_back(e.name);
    return names;
}

Error ResourceFile::read(std::string_view name, std::vector<uint8_t> &out) const {
    const int entry = find(name);
    if (entry < 0)
        return Error::fail("no " + std::string(name) + " in " + _rfd.string());
    return readEntry(size_t(entry), out);
}

} // namespace reluva::legoloco
