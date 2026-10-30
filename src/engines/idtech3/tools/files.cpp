// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for a game's files and paks.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "common/zip.h"
#include "engines/idtech3/pure.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

int cmdPk3List(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool pk3-list <game folder> [name filter]\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const std::string filter = argc == 4 ? reluva::normalizeArchiveName(argv[3]) : std::string();
    for (const std::string& name : set.list()) {
        if (!filter.empty() && reluva::normalizeArchiveName(name).find(filter) == std::string::npos)
            continue;
        const reluva::Archive* a = set.find(name);
        long long size = -1;
        if (const auto* zip = dynamic_cast<const reluva::ZipArchive*>(a))
            if (const auto* e = zip->entry(name))
                size = e->size;
        std::printf("%10lld  %-60s %s\n", size, name.c_str(),
                    std::filesystem::path(a->describe()).filename().string().c_str());
    }
    return 0;
}

// Each .pk3's checksum in a game folder, as id Tech 3 games tell them
// apart (and the pure check's, with a feed if given).
int cmdPk3Sums(int argc, char** argv) {
    namespace q3 = reluva::idtech3;
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool pk3-sums <game folder> [feed]\n");
        return 1;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const bool withFeed = argc >= 4;
    const auto feed = int32_t(withFeed ? std::strtoll(argv[3], nullptr, 10) : 0);
    for (const reluva::Archive* a : set.archives())
        if (const auto* pak = dynamic_cast<const reluva::ZipArchive*>(a)) {
            std::printf("%s: %u", pak->describe().c_str(), uint32_t(q3::pakChecksum(*pak)));
            if (withFeed)
                std::printf(" (pure %d)", q3::pakChecksum(*pak, feed));
            std::printf("\n");
        }
    if (withFeed)
        std::printf("%s\n", q3::pureCommand(set, feed, "").c_str());
    return 0;
}

int cmdFileShow(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool file-show <game folder> <path>\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    Bytes data;
    if (reluva::Error err = set.read(argv[3], data)) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    std::fwrite(data.data(), 1, data.size(), stdout);
    if (!data.empty() && data.back() != '\n')
        std::printf("\n");
    return 0;
}

const Commands commands("idtech3", {
    {"pk3-list", "<game folder> [name filter]",
     "list the search path's files, with sizes and the archive each comes from",
     cmdPk3List},
    {"pk3-sums", "<game folder> [feed]",
     "each .pk3's checksum, as id Tech 3 games tell them apart (and the pure check's, with a feed)",
     cmdPk3Sums},
    {"file-show", "<game folder> <path>",
     "print a file from the search path (a text file: a config, a script)",
     cmdFileShow},
});

} // namespace
