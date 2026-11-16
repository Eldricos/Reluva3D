// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout here was worked out from And Then There Were None's two CDs
// (data1.hdr, three volumes): see docs/common/installshield.md.

#include "common/installshield.h"

#include "common/fs.h"
#include "common/md5.h"
#include "common/text.h"
#include "common/zip.h"

#include <algorithm>
#include <fstream>

namespace reluva {

namespace {

constexpr uint32_t kSignature = 0x28635349; // "ISc("
constexpr size_t kCommonHeader = 0x14;      // a header's and every volume's
constexpr size_t kFileEntry = 0x57;
constexpr size_t kGroupSlots = 71; // the cab descriptor's table of group lists
constexpr uint64_t kChunk = 0x8000;

struct Bytes {
    const std::vector<uint8_t> &d;
    bool bad = false;

    uint32_t u8(size_t at) {
        if (at >= d.size()) {
            bad = true;
            return 0;
        }
        return d[at];
    }
    uint32_t u16(size_t at) { return u8(at) | u8(at + 1) << 8; }
    uint32_t u32(size_t at) { return u16(at) | u16(at + 2) << 16; }
    uint64_t u64(size_t at) { return u32(at) | uint64_t(u32(at + 4)) << 32; }
    std::string text(size_t at) {
        std::string s;
        for (; at < d.size() && d[at]; ++at)
            s.push_back(char(d[at]));
        if (at >= d.size())
            bad = true;
        return text::windows1252ToUtf8(s);
    }
};

bool readFile(const std::filesystem::path &file, std::vector<uint8_t> &data, size_t limit) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    data.resize(limit);
    in.read(reinterpret_cast<char *>(data.data()), std::streamsize(limit));
    data.resize(size_t(in.gcount()));
    return true;
}

// Where a file's data is: a piece in each volume it's in.
struct Piece {
    size_t volume;
    uint64_t offset;
    uint64_t size;
};

// False if a volume it's in isn't found (`missing` says which).
bool pieces(const InstallShieldCabinet &cab, size_t index, std::vector<Piece> &out, size_t &missing) {
    out.clear();
    const uint32_t data = cab.files[index].data;
    const InstallShieldFile &f = cab.files[data];
    uint64_t left = f.packedSize;
    for (size_t v = f.volume;; ++v) {
        if (v >= cab.volumes.size() || cab.volumes[v].path.empty()) {
            missing = v;
            return false;
        }
        const InstallShieldVolume &vol = cab.volumes[v];
        Piece p{v, f.offset, left};
        if (v != f.volume) {
            p.offset = vol.firstOffset;
            p.size = std::min(left, vol.firstPackedSize);
        } else if (f.split && vol.last == data) {
            p.size = std::min(left, vol.lastPackedSize);
        }
        if (p.size == 0 && left > 0) {
            missing = v;
            return false;
        }
        out.push_back(p);
        left -= p.size;
        if (left == 0)
            return true;
    }
}

} // namespace

std::string InstallShieldCabinet::volumeName(int n) const {
    std::string stem = header.stem().string();
    if (!stem.empty() && stem.back() == '1')
        stem.pop_back();
    return stem + std::to_string(n) + ".cab";
}

bool InstallShieldCabinet::reachable(size_t index) const {
    std::vector<Piece> unused;
    size_t missing = 0;
    return index < files.size() && pieces(*this, index, unused, missing);
}

long InstallShieldCabinet::find(const InstallShieldGroup &group, std::string_view path) const {
    for (size_t i = group.first; i <= group.last && i < files.size(); ++i)
        if (!files[i].path.empty() && text::equalsIgnoreCase(files[i].path, path))
            return long(i);
    return -1;
}

Error readInstallShieldCabinet(const std::filesystem::path &header, InstallShieldCabinet &out) {
    out = {};
    out.header = header;
    std::vector<uint8_t> d;
    if (!readFile(header, d, size_t(64) << 20))
        return Error::fail("Couldn't read " + header.string());
    Bytes b{d};
    const std::string name = header.filename().string();
    if (b.u32(0) != kSignature)
        return Error::fail(name + " isn't an InstallShield cabinet");
    // The version is 0x01007000 for InstallShield 7's, its 7 in bits 12-15.
    const uint32_t version = b.u32(4);
    if (version >> 24 != 1 || (version >> 12 & 0xf) != 7)
        return Error::fail(name + " is an InstallShield cabinet of a version Reluva doesn't read");
    const size_t cd = b.u32(12); // the cab descriptor
    const size_t table = cd + b.u32(cd + 0x0c);
    const uint32_t folders = b.u32(cd + 0x1c);
    const uint32_t count = b.u32(cd + 0x28);
    const size_t entries = table + b.u32(cd + 0x2c);
    if (b.bad || folders > d.size() / 4 || count > d.size() / kFileEntry || entries + count * kFileEntry > d.size())
        return Error::fail(name + " is damaged");
    std::vector<std::string> folderNames(folders);
    for (uint32_t i = 0; i < folders; ++i)
        folderNames[i] = b.text(table + b.u32(table + 4 * i));
    out.files.resize(count);
    uint16_t lastVolume = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = entries + i * kFileEntry;
        InstallShieldFile &f = out.files[i];
        const uint32_t flags = b.u16(e);
        f.split = flags & 1;
        f.compressed = flags & 4;
        f.size = b.u64(e + 2);
        f.packedSize = b.u64(e + 10);
        f.offset = b.u64(e + 18);
        for (size_t k = 0; k < 16; ++k)
            f.md5[k] = uint8_t(b.u8(e + 26 + k));
        f.volume = uint16_t(b.u16(e + 85));
        f.data = i;
        lastVolume = std::max(lastVolume, f.volume);
        // Flag 8: an entry for no file.
        const uint32_t nameAt = b.u32(e + 58);
        if (flags & 8 || nameAt == 0)
            continue;
        const uint32_t folder = b.u16(e + 62);
        f.path = b.text(table + nameAt);
        if (folder < folders && !folderNames[folder].empty()) {
            std::string dir = folderNames[folder];
            std::replace(dir.begin(), dir.end(), '\\', '/');
            f.path = dir + "/" + f.path;
        }
    }
    // A file with link flag 1 has the same data as an earlier one (its
    // volume number isn't then its data's), named at +76.
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t data = i;
        for (int hops = 0; hops < 64 && b.u8(entries + data * kFileEntry + 84) & 1; ++hops) {
            const uint32_t previous = b.u32(entries + data * kFileEntry + 76);
            if (previous >= count)
                break;
            data = previous;
        }
        out.files[i].data = data;
    }
    // Groups: lists, from a table of 71, each entry its name's offset,
    // its descriptor's and the next entry's, all from the cab descriptor.
    for (size_t slot = 0; slot < kGroupSlots; ++slot) {
        uint32_t at = b.u32(cd + 0x3e + 4 * slot);
        for (int n = 0; at && n < 1000 && !b.bad; ++n) {
            const size_t g = cd + b.u32(cd + at + 4);
            InstallShieldGroup group;
            group.name = b.text(cd + b.u32(g));
            group.first = b.u32(g + 0x16);
            group.last = b.u32(g + 0x1a);
            out.groups.push_back(group);
            at = b.u32(cd + at + 8);
        }
    }
    if (b.bad)
        return Error::fail(name + " is damaged");
    out.volumes.resize(size_t(lastVolume) + 1);
    findInstallShieldVolumes(out, header.parent_path());
    return Error::success();
}

int findInstallShieldVolumes(InstallShieldCabinet &cabinet, const std::filesystem::path &dir) {
    int added = 0;
    for (size_t n = 1; n < cabinet.volumes.size(); ++n) {
        if (!cabinet.volumes[n].path.empty())
            continue;
        const auto path = fs::findFile(dir, cabinet.volumeName(int(n)));
        std::vector<uint8_t> d;
        if (!path || !readFile(*path, d, kCommonHeader + 0x40))
            continue;
        Bytes b{d};
        // After the common header: its data's offset, a word not known,
        // then the first and last files whose data it holds, the first's
        // offset and its sizes here, then the last's.
        const size_t v = kCommonHeader;
        InstallShieldVolume vol;
        vol.path = *path;
        vol.first = b.u32(v + 8);
        vol.last = b.u32(v + 12);
        vol.firstOffset = b.u64(v + 16);
        vol.firstPackedSize = b.u64(v + 32);
        vol.lastPackedSize = b.u64(v + 56);
        // Another game's volume of the same name: its files aren't these.
        if (b.bad || b.u32(0) != kSignature || vol.first > vol.last || vol.last >= cabinet.files.size() ||
            cabinet.files[cabinet.files[vol.last].data].volume != n)
            continue;
        cabinet.volumes[n] = vol;
        ++added;
    }
    return added;
}

Error extractInstallShieldFile(const InstallShieldCabinet &cabinet, size_t index, std::vector<uint8_t> &out) {
    out.clear();
    if (index >= cabinet.files.size() || cabinet.files[index].path.empty())
        return Error::fail("No such file in " + cabinet.header.filename().string());
    const InstallShieldFile &named = cabinet.files[index];
    const InstallShieldFile &f = cabinet.files[named.data];
    std::vector<Piece> parts;
    size_t missing = 0;
    if (!pieces(cabinet, index, parts, missing))
        return Error::fail(named.path + " needs " + cabinet.volumeName(int(missing)));
    std::vector<uint8_t> packed;
    packed.reserve(size_t(f.packedSize));
    for (const Piece &p : parts) {
        std::ifstream in(cabinet.volumes[p.volume].path, std::ios::binary);
        const size_t at = packed.size();
        packed.resize(at + size_t(p.size));
        if (!in.seekg(std::streamoff(p.offset)) ||
            !in.read(reinterpret_cast<char *>(packed.data() + at), std::streamsize(p.size)))
            return Error::fail("Couldn't read " + named.path + " from " + cabinet.volumes[p.volume].path.string());
    }
    if (!f.compressed) {
        out = std::move(packed);
    } else {
        // Chunks of 32 KiB unpacked (the last the rest), each a 16-bit
        // length and a deflate stream of its own.
        out.reserve(size_t(f.size));
        std::vector<uint8_t> chunk;
        for (size_t at = 0; at < packed.size();) {
            const size_t length = at + 2 <= packed.size() ? packed[at] | size_t(packed[at + 1]) << 8 : 0;
            at += 2;
            const uint64_t want = std::min<uint64_t>(kChunk, f.size - out.size());
            if (at + length > packed.size() || want == 0 || inflateRaw(packed.data() + at, length, size_t(want), chunk))
                return Error::fail(named.path + " is damaged in " + cabinet.header.filename().string());
            out.insert(out.end(), chunk.begin(), chunk.end());
            at += length;
        }
    }
    Md5 md5;
    md5.update(out.data(), out.size());
    std::string expected;
    for (uint8_t byte : f.md5)
        expected += text::format("%02x", byte);
    if (out.size() != f.size || md5.hexDigest() != expected)
        return Error::fail(named.path + " doesn't come out whole from " + cabinet.header.filename().string());
    return Error::success();
}

} // namespace reluva
