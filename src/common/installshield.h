// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva {

/** One file of an InstallShield cabinet. */
struct InstallShieldFile {
    std::string path;        ///< its folder and name, '/' between, as installed
    uint64_t size = 0;       ///< unpacked
    uint64_t packedSize = 0; ///< in the volumes, all its parts together
    uint64_t offset = 0;     ///< of its data in its first volume
    uint16_t volume = 0;     ///< which volume its data starts in, from 1
    bool compressed = false;
    bool split = false; ///< its data runs on into the next volume
    /** Whose data it is: its own index, or the file it shares data with. */
    uint32_t data = 0;
    std::array<uint8_t, 16> md5{}; ///< of the unpacked file
};

/** A file group: the files setup installs together, by index (`first` to `last`). */
struct InstallShieldGroup {
    std::string name;
    uint32_t first = 0;
    uint32_t last = 0;
};

/** One volume (data<n>.cab) found: where, and the files whose data it starts or ends with. */
struct InstallShieldVolume {
    std::filesystem::path path; ///< empty: not found yet
    uint32_t first = 0;         ///< the file its data starts with
    uint32_t last = 0;
    uint64_t firstOffset = 0;     ///< where the first file's data is, when it began in the volume before
    uint64_t firstPackedSize = 0; ///< how much of the first file's data is here, then
    uint64_t lastPackedSize = 0;  ///< how much of the last file's data is here, when it runs on
};

/**
 * An InstallShield cabinet, as games' CDs held their files (And Then There
 * Were None's): a header (data1.hdr) listing the files and their groups,
 * and their data in volumes (data1.cab, data2.cab...), one CD's or two.
 * Only the layout of version 7's header is read.
 */
struct InstallShieldCabinet {
    std::filesystem::path header;
    std::vector<InstallShieldFile> files; ///< by index; a file with no name isn't one
    std::vector<InstallShieldGroup> groups;
    std::vector<InstallShieldVolume> volumes; ///< [n] is volume n; [0] unused

    /** Volume n's file name: the header's with n in place of its 1 and .cab ("data3.cab"). */
    std::string volumeName(int n) const;
    /** Whether every volume file `index`'s data is in has been found. */
    bool reachable(size_t index) const;
    /** The first file in `group` whose path is `path` (ignoring case), or -1. */
    long find(const InstallShieldGroup &group, std::string_view path) const;
};

/** Reads `header` (data1.hdr), and the volumes beside it; fails if it isn't a cabinet header Reluva reads. */
Error readInstallShieldCabinet(const std::filesystem::path &header, InstallShieldCabinet &out);
/** Adds the cabinet's volumes found in `dir` (another CD's); returns how many weren't found before. */
int findInstallShieldVolumes(InstallShieldCabinet &cabinet, const std::filesystem::path &dir);
/** One file unpacked, its MD5 checked; fails if a volume it needs isn't found. */
Error extractInstallShieldFile(const InstallShieldCabinet &cabinet, size_t index, std::vector<uint8_t> &out);

} // namespace reluva
