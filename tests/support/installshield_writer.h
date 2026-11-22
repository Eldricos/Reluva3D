// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Builds small InstallShield cabinets for tests, laid out as
// docs/common/installshield.md says: a data1.hdr and its volumes. A
// compressed file's chunks hold stored (uncompressed) deflate blocks, so
// the tests need no compressor.

#pragma once

#include "common/md5.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::test {

/** A file to put in the cabinet. */
struct CabinetFile {
    std::string folder; // "Data\\Sub", or "" for the top
    std::string name;   // "": an entry for no file
    std::vector<uint8_t> data;
    bool compressed = true;
    int volume = 1;
    int linkTo = -1;    // shares this earlier file's data (its own volume number kept, as found)
    bool split = false; // half its packed data in its volume, the rest first in the next
};

/** A file group: the files from index `first` to `last`, both included. */
struct CabinetGroup {
    std::string name;
    uint32_t first;
    uint32_t last;
};

/**
 * Writes data1.hdr in volumeDirs[0], and volume n (`data<n>.cab`) in
 * volumeDirs[n - 1]. Files are in the order of their data in the volumes.
 * @param version The header's and volumes' version word: InstallShield 7's
 *     by default, 0x0100600c for 6's.
 */
inline void writeCabinet(const std::vector<std::filesystem::path> &volumeDirs, const std::vector<CabinetFile> &files,
                         const std::vector<CabinetGroup> &groups, uint32_t version = 0x01007000) {
    auto put = [](std::vector<uint8_t> &v, size_t at, uint64_t x, int bytes) {
        if (v.size() < at + size_t(bytes))
            v.resize(at + size_t(bytes));
        for (int i = 0; i < bytes; ++i)
            v[at + size_t(i)] = uint8_t(x >> (8 * i));
    };
    auto text = [](std::vector<uint8_t> &v, const std::string &s) {
        const size_t at = v.size();
        v.insert(v.end(), s.begin(), s.end());
        v.push_back(0);
        return at;
    };

    struct Placed {
        std::vector<uint8_t> packed;
        uint64_t offset = 0;
        std::array<uint8_t, 16> md5{};
    };
    struct Volume {
        std::vector<uint8_t> bytes = std::vector<uint8_t>(0x200, 0);
        uint32_t first = 0xffffffff;
        uint32_t last = 0;
        uint64_t firstOffset = 0;
        uint64_t firstPacked = 0;
        uint64_t lastPacked = 0;
    };
    std::vector<Volume> volumes(volumeDirs.size() + 1);
    std::vector<Placed> placed(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        const CabinetFile &f = files[i];
        if (f.name.empty() || f.linkTo >= 0)
            continue;
        Placed &p = placed[i];
        if (!f.compressed) {
            p.packed = f.data;
        } else {
            // 32 KiB chunks, each a 16-bit length and one stored block.
            for (size_t at = 0; at < f.data.size(); at += 0x8000) {
                const size_t n = std::min<size_t>(0x8000, f.data.size() - at);
                put(p.packed, p.packed.size(), n + 5, 2);
                p.packed.push_back(1);
                put(p.packed, p.packed.size(), n, 2);
                put(p.packed, p.packed.size(), n ^ 0xffff, 2);
                p.packed.insert(p.packed.end(), f.data.begin() + long(at), f.data.begin() + long(at + n));
            }
        }
        Md5 md5;
        md5.update(f.data.data(), f.data.size());
        const std::string hex = md5.hexDigest();
        for (size_t k = 0; k < 16; ++k)
            p.md5[k] = uint8_t(std::stoi(hex.substr(2 * k, 2), nullptr, 16));
        Volume &v = volumes[size_t(f.volume)];
        p.offset = v.bytes.size();
        const size_t here = f.split ? p.packed.size() / 2 : p.packed.size();
        v.bytes.insert(v.bytes.end(), p.packed.begin(), p.packed.begin() + long(here));
        v.first = std::min(v.first, uint32_t(i));
        v.last = uint32_t(i);
        if (f.split) {
            v.lastPacked = here;
            Volume &next = volumes[size_t(f.volume) + 1];
            next.first = uint32_t(i);
            next.firstOffset = next.bytes.size();
            next.firstPacked = p.packed.size() - here;
            next.bytes.insert(next.bytes.end(), p.packed.begin() + long(here), p.packed.end());
        }
    }

    // The header: its common header, the cab descriptor at 0x200, the
    // groups after its table of 71, then the file table.
    std::vector<uint8_t> h(0x200 + 0x400, 0);
    const size_t cd = 0x200;
    put(h, 0, 0x28635349, 4);
    put(h, 4, version, 4);
    put(h, 12, cd, 4);
    for (size_t g = 0; g < groups.size(); ++g) {
        const size_t name = text(h, groups[g].name);
        const size_t desc = h.size();
        put(h, desc, name - cd, 4);
        put(h, desc + 0x16, groups[g].first, 4);
        put(h, desc + 0x1a, groups[g].last, 4);
        const size_t entry = h.size();
        put(h, entry, name - cd, 4);
        put(h, entry + 4, desc - cd, 4);
        put(h, entry + 8, 0, 4);
        put(h, cd + 0x3e + 4 * (g % 71), entry - cd, 4);
    }
    std::vector<std::string> folders;
    for (const CabinetFile &f : files)
        if (std::find(folders.begin(), folders.end(), f.folder) == folders.end())
            folders.push_back(f.folder);
    const size_t table = h.size();
    put(h, cd + 0x0c, table - cd, 4);
    put(h, cd + 0x1c, folders.size(), 4);
    put(h, cd + 0x28, files.size(), 4);
    h.resize(table + 4 * folders.size());
    for (size_t k = 0; k < folders.size(); ++k)
        put(h, table + 4 * k, text(h, folders[k]) - table, 4);
    std::vector<size_t> names(files.size());
    for (size_t i = 0; i < files.size(); ++i)
        names[i] = files[i].name.empty() ? 0 : text(h, files[i].name) - table;
    const size_t entries = h.size();
    put(h, cd + 0x2c, entries - table, 4);
    h.resize(entries + 0x57 * files.size(), 0);
    for (size_t i = 0; i < files.size(); ++i) {
        const CabinetFile &f = files[i];
        const size_t e = entries + 0x57 * i;
        const size_t owner = f.linkTo >= 0 ? size_t(f.linkTo) : i;
        const Placed &p = placed[owner];
        const CabinetFile &o = files[owner];
        put(h, e, f.name.empty() ? 8 : (o.compressed ? 4 : 0) | (o.split ? 1 : 0), 2);
        put(h, e + 2, o.data.size(), 8);
        put(h, e + 10, p.packed.size(), 8);
        put(h, e + 18, p.offset, 8);
        for (size_t k = 0; k < 16; ++k)
            h[e + 26 + k] = p.md5[k];
        put(h, e + 58, names[i], 4);
        put(h, e + 62, size_t(std::find(folders.begin(), folders.end(), f.folder) - folders.begin()), 2);
        if (f.linkTo >= 0) {
            put(h, e + 76, uint32_t(f.linkTo), 4);
            h[e + 84] = 1;
        }
        put(h, e + 85, uint32_t(f.volume), 2);
    }
    writeFile(volumeDirs[0] / "data1.hdr", h);

    for (size_t n = 1; n < volumes.size(); ++n) {
        Volume &v = volumes[n];
        put(v.bytes, 0, 0x28635349, 4);
        put(v.bytes, 4, version, 4);
        put(v.bytes, 0x14, 0x200, 4);
        put(v.bytes, 0x1c, v.first == 0xffffffff ? 0 : v.first, 4);
        put(v.bytes, 0x20, v.last, 4);
        put(v.bytes, 0x24, v.firstOffset, 8);
        put(v.bytes, 0x34, v.firstPacked, 8);
        put(v.bytes, 0x4c, v.lastPacked, 8);
        writeFile(volumeDirs[n - 1] / ("data" + std::to_string(n) + ".cab"), v.bytes);
    }
}

} // namespace reluva::test
