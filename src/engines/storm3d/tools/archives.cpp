// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for the ZFS archives
// (docs/engines/storm3d/games/armada/formats/zfs.md).
//
//   reluva-tool storm3d zfs-list <file.zfs>
//   reluva-tool storm3d zfs-extract <file.zfs> <output dir> [name filter]

#include "engines/storm3d/formats/zfs_archive.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using reluva::storm3d::ZfsArchive;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

int cmdZfsList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d zfs-list <file.zfs>\n");
        return 2;
    }
    ZfsArchive zfs;
    if (reluva::Error err = zfs.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    uint64_t total = 0;
    for (const ZfsArchive::Entry &e : zfs.entries()) {
        std::printf("%5u %10u %10u %10u %u  %s\n", e.index, e.offset, e.size, e.modified, e.flags, e.name.c_str());
        total += e.size;
    }
    std::printf("%zu files, %llu bytes, key %08x\n", zfs.entries().size(), static_cast<unsigned long long>(total),
                zfs.key());
    return 0;
}

int cmdZfsExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d zfs-extract <file.zfs> <output dir> [name filter]\n");
        return 2;
    }
    ZfsArchive zfs;
    if (reluva::Error err = zfs.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path out = argv[3];
    const std::string filter = argc > 4 ? lower(argv[4]) : "";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    int written = 0;
    int failed = 0;
    std::vector<uint8_t> data;
    for (const ZfsArchive::Entry &e : zfs.entries()) {
        if (!filter.empty() && lower(e.name).find(filter) == std::string::npos)
            continue;
        if (reluva::Error err = zfs.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            ++failed;
            continue;
        }
        std::ofstream file(out / e.name, std::ios::binary);
        if (!file.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()))) {
            std::fprintf(stderr, "Cannot write %s\n", (out / e.name).string().c_str());
            ++failed;
            continue;
        }
        ++written;
    }
    std::printf("%d files written to %s\n", written, out.string().c_str());
    return failed ? 1 : 0;
}

const reluva::tool::Commands kCommands(
    "storm3d",
    {
        {"zfs-list", "<file.zfs>", "list a ZFS archive's files: index, offset, size, time, flags and name", cmdZfsList},
        {"zfs-extract", "<file.zfs> <output dir> [name filter]",
         "write a ZFS archive's files, decoded, to a folder (those whose names hold the filter)", cmdZfsExtract},
    });

} // namespace
