// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the archives (.wad) and the
// game's files as the engine mounts them.

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/wad.h"
#include "engines/tpw/game_files.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

using namespace reluva::tool;
using reluva::tpw::WadArchive;

int openWad(const char *path, WadArchive &wad) {
    if (reluva::Error err = wad.open(path)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    return 0;
}

// Every file of an archive, with its sizes and how it's stored.
int cmdWadList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw wad-list <file.wad>\n");
        return 2;
    }
    WadArchive wad;
    if (int rc = openWad(argv[2], wad))
        return rc;
    for (const WadArchive::Entry &e : wad.entries())
        std::printf("%10u %10u  %-7s %08x  %s\n", e.size, e.packedSize, e.refPack ? "refpack" : "stored", e.hash,
                    e.name.c_str());
    std::printf("%zu files\n", wad.entries().size());
    return 0;
}

// Every file of an archive written out under a folder, or the one named.
int cmdWadExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool tpw wad-extract <file.wad> <folder> [name]\n");
        return 2;
    }
    WadArchive wad;
    if (int rc = openWad(argv[2], wad))
        return rc;
    int written = 0;
    for (const WadArchive::Entry &e : wad.entries()) {
        if (argc > 4 && reluva::normalizeArchiveName(e.name) != reluva::normalizeArchiveName(argv[4]))
            continue;
        std::vector<uint8_t> data;
        if (reluva::Error err = wad.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        const std::filesystem::path out = std::filesystem::path(argv[3]) / e.name;
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream(out, std::ios::binary)
            .write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
        ++written;
    }
    std::printf("%d files written\n", written);
    return written > 0 ? 0 : 1;
}

// Every file the engine finds in a game's Data folder read: how many,
// by kind, with a checksum of them all, for the game-files test.
int cmdDataCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw data-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const std::string &w : warnings)
        std::printf("warning: %s\n", w.c_str());
    std::map<std::string, int> kinds;
    reluva::Md5 all;
    int failed = 0;
    size_t count = 0;
    for (const std::string &name : files.list()) {
        std::vector<uint8_t> data;
        if (reluva::Error err = files.read(name, data)) {
            std::printf("failed: %s\n", err.message.c_str());
            ++failed;
            continue;
        }
        ++count;
        ++kinds[reluva::fs::toLower(extensionOf(name))];
        all.update(name.data(), name.size());
        all.update(data.data(), data.size());
    }
    std::printf("%zu archives, %zu files read, %d failed\n", files.size() - 1, count, failed);
    for (const auto &[ext, n] : kinds)
        std::printf("  %-6s %d\n", ext.empty() ? "(none)" : ext.c_str(), n);
    std::printf("md5 %s\n", all.hexDigest().c_str());
    return failed || !warnings.empty() ? 1 : 0;
}

const Commands kCommands("tpw",
                         {
                             {"wad-list", "<file.wad>", "list an archive's files: sizes, storage, names", cmdWadList},
                             {"wad-extract", "<file.wad> <folder> [name]",
                              "write an archive's files (or the one named) under a folder", cmdWadExtract},
                             {"data-check", "<game folder>",
                              "read every file of a game's Data folder, archives and loose files", cmdDataCheck},
                         });

} // namespace
