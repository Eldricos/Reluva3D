// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Away Team commands for DATA.dat, the archive
// holding nearly all the game's files.

#include "devtools/tool.h"

#include "common/fs.h"
#include "common/zip.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// Whether `name` is chosen by `filter`: a part of it, in any case; an
// empty filter chooses every file.
bool chosen(const std::string &name, const std::string &filter) {
    return filter.empty() || fs::toLower(name).find(fs::toLower(filter)) != std::string::npos;
}

int cmdDataList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam data-list <DATA.dat> [part of a name]\n");
        return 2;
    }
    ZipArchive zip;
    if (Error err = zip.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string filter = argc > 3 ? argv[3] : "";
    size_t files = 0;
    uint64_t bytes = 0;
    for (const auto &entry : zip.entries()) {
        if (!chosen(entry.name, filter))
            continue;
        std::printf("%10u  %s\n", entry.size, entry.name.c_str());
        ++files;
        bytes += entry.size;
    }
    std::printf("%zu files, %llu bytes\n", files, static_cast<unsigned long long>(bytes));
    return 0;
}

int cmdDataExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam data-extract <DATA.dat> <output folder> "
                             "[part of a name]\n");
        return 2;
    }
    ZipArchive zip;
    if (Error err = zip.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path out = argv[3];
    const std::string filter = argc > 4 ? argv[4] : "";
    size_t files = 0;
    std::vector<uint8_t> data;
    for (const auto &entry : zip.entries()) {
        if (!chosen(entry.name, filter))
            continue;
        if (Error err = zip.read(entry, data)) {
            std::fprintf(stderr, "%s: %s\n", entry.name.c_str(), err.message.c_str());
            return 1;
        }
        const std::filesystem::path path = out / std::filesystem::path(entry.name).relative_path();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary);
        if (!file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
            std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
            return 1;
        }
        ++files;
    }
    std::printf("%zu files written to %s\n", files, out.string().c_str());
    return 0;
}

const Commands kCommands("stawayteam",
                         {
                             {"data-list", "<DATA.dat> [part of a name]",
                              "list the files in DATA.dat, with their sizes", cmdDataList},
                             {"data-extract", "<DATA.dat> <output folder> [part of a name]",
                              "write out the files in DATA.dat, checked against their CRC-32s", cmdDataExtract},
                         });

} // namespace
