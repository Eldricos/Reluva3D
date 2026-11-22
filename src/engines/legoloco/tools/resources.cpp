// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco commands for the resource file
// (docs/engines/legoloco/formats/resource.md).

#include "common/archive.h"
#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoloco/formats/resource_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// The game's folder or its art-res folder, whichever was given.
bool openResources(const char *arg, legoloco::ResourceFile &rf) {
    std::filesystem::path folder = arg;
    if (const auto artRes = fs::findDirectory(folder, "art-res"))
        folder = *artRes;
    if (Error err = rf.open(folder)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// Every entry unpacked, so a failure to unpack shows in the list.
int cmdList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco rf-list <game or art-res folder>\n");
        return 2;
    }
    legoloco::ResourceFile rf;
    if (!openResources(argv[2], rf))
        return 1;
    int failed = 0;
    size_t compressed = 0;
    std::printf("%10s %10s  %s\n", "stored", "unpacked", "name (* compressed)");
    for (size_t i = 0; i < rf.entries().size(); ++i) {
        const auto &e = rf.entries()[i];
        std::vector<uint8_t> data;
        if (Error err = rf.readEntry(i, data)) {
            std::printf("%10u %10s  %s: %s\n", e.size, "-", e.name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        compressed += e.compressed;
        std::printf("%10u %10zu %c%s\n", e.size, data.size(), e.compressed ? '*' : ' ', e.name.c_str());
    }
    std::printf("%zu entries, %zu compressed, %d failed\n", rf.entries().size(), compressed, failed);
    return failed ? 1 : 0;
}

int cmdExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool legoloco rf-extract <game or art-res folder> <output folder> "
                             "[name...]\n");
        return 2;
    }
    legoloco::ResourceFile rf;
    if (!openResources(argv[2], rf))
        return 1;
    const std::filesystem::path out = argv[3];
    std::vector<std::string> wanted;
    for (int i = 4; i < argc; ++i)
        wanted.push_back(normalizeArchiveName(argv[i]));
    int written = 0;
    int failed = 0;
    for (size_t i = 0; i < rf.entries().size(); ++i) {
        const std::string name = normalizeArchiveName(rf.entries()[i].name);
        // A name given picks that entry, or every entry in a folder of that name.
        bool pick = wanted.empty();
        for (const std::string &w : wanted)
            pick = pick || name == w || name.starts_with(w + "/");
        if (!pick)
            continue;
        std::vector<uint8_t> data;
        if (Error err = rf.readEntry(i, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            ++failed;
            continue;
        }
        const std::filesystem::path file = out / name;
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream f(file, std::ios::binary);
        if (!f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()))) {
            std::fprintf(stderr, "can't write %s\n", file.string().c_str());
            ++failed;
            continue;
        }
        ++written;
    }
    std::printf("%d written, %d failed\n", written, failed);
    return failed ? 1 : 0;
}

const Commands kCommands("legoloco",
                         {
                             {"rf-list", "<game or art-res folder>",
                              "list the resource file's entries, each unpacked to check it", cmdList},
                             {"rf-extract", "<game or art-res folder> <output folder> [name...]",
                              "write the resource file's entries, unpacked, under a folder: every one, or those "
                              "named (a name may be a folder)",
                              cmdExtract},
                         });

} // namespace
