// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Builds small ISO 9660 images for tests: a disc's data track holding the
// files given, in 2048-byte sectors or raw 2352-byte mode 1 sectors.

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::test {

/** A file to put on the disc: its path from the root, '/' between folders. */
struct IsoEntry {
    std::string path;
    std::vector<uint8_t> data;
};

/**
 * A data track with `files` on it, named in capitals with ";1" as a disc
 * has them, each folder in one sector (so a few files to a folder).
 * `raw`: 2352-byte sectors, the data 16 bytes in, as a cue sheet's
 * MODE1/2352 track.
 */
inline std::vector<uint8_t> makeIsoImage(const std::vector<IsoEntry> &files, const std::string &volume,
                                         bool raw = false) {
    constexpr size_t kSector = 2048;
    struct Folder {
        std::vector<std::string> folders;
        std::vector<size_t> files;
        uint32_t sector = 0;
    };
    std::map<std::string, Folder> folders{{"", {}}};
    auto parentOf = [](const std::string &p) {
        const size_t slash = p.find_last_of('/');
        return slash == std::string::npos ? std::string() : p.substr(0, slash);
    };
    for (size_t i = 0; i < files.size(); ++i) {
        std::string folder = parentOf(files[i].path);
        folders[folder].files.push_back(i);
        while (!folder.empty()) {
            const std::string parent = parentOf(folder);
            auto &list = folders[parent].folders;
            if (std::find(list.begin(), list.end(), folder) == list.end())
                list.push_back(folder);
            folders[folder];
            folder = parent;
        }
    }
    // The descriptors at 16 and 17, a sector a folder, then the files.
    uint32_t next = 18;
    for (auto &[name, f] : folders)
        f.sector = next++;
    std::vector<uint32_t> fileSector(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        fileSector[i] = next;
        next += uint32_t((files[i].data.size() + kSector - 1) / kSector);
    }
    std::vector<uint8_t> image(size_t(next) * kSector);
    auto both32 = [](uint8_t *p, uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            p[i] = uint8_t(v >> (8 * i));
            p[7 - i] = uint8_t(v >> (8 * i));
        }
    };
    auto record = [&](uint8_t *p, uint32_t sector, uint32_t size, bool folder, const std::string &name) {
        const size_t length = (33 + name.size() + 1) & ~size_t(1);
        p[0] = uint8_t(length);
        both32(p + 2, sector);
        both32(p + 10, size);
        p[25] = folder ? 2 : 0;
        p[28] = 1;
        p[31] = 1;
        p[32] = uint8_t(name.size());
        std::copy(name.begin(), name.end(), p + 33);
        return length;
    };
    auto leaf = [](const std::string &p) { return p.substr(p.find_last_of('/') + 1); };
    for (auto &[name, f] : folders) {
        uint8_t *p = &image[size_t(f.sector) * kSector];
        size_t at = record(p, f.sector, kSector, true, std::string(1, '\0'));
        at += record(p + at, folders[parentOf(name)].sector, kSector, true, std::string(1, '\1'));
        for (const std::string &sub : f.folders)
            at += record(p + at, folders[sub].sector, kSector, true, leaf(sub));
        for (size_t i : f.files)
            at += record(p + at, fileSector[i], uint32_t(files[i].data.size()), false, leaf(files[i].path) + ";1");
    }
    for (size_t i = 0; i < files.size(); ++i)
        std::copy(files[i].data.begin(), files[i].data.end(), image.begin() + std::ptrdiff_t(fileSector[i] * kSector));
    uint8_t *pvd = &image[16 * kSector];
    pvd[0] = 1;
    std::copy_n("CD001", 5, pvd + 1);
    pvd[6] = 1;
    std::fill_n(pvd + 40, 32, uint8_t(' '));
    std::copy(volume.begin(), volume.end(), pvd + 40);
    record(pvd + 156, folders[""].sector, kSector, true, std::string(1, '\0'));
    uint8_t *end = &image[17 * kSector];
    end[0] = 255;
    std::copy_n("CD001", 5, end + 1);
    end[6] = 1;
    if (!raw)
        return image;
    std::vector<uint8_t> out;
    for (size_t s = 0; s < next; ++s) {
        std::vector<uint8_t> sector(2352);
        std::fill_n(sector.begin() + 1, 10, uint8_t(0xff));
        sector[15] = 1;
        std::copy_n(image.begin() + std::ptrdiff_t(s * kSector), kSector, sector.begin() + 16);
        out.insert(out.end(), sector.begin(), sector.end());
    }
    return out;
}

} // namespace reluva::test
