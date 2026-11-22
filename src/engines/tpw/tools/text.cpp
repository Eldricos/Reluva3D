// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's text: settings
// files (.sam).

#include "common/fs.h"
#include "common/text.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::SamFile;

// A game's files mounted, or a message why not.
bool mount(const char *folder, reluva::SearchSet &files) {
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, folder, warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    for (const std::string &w : warnings)
        std::printf("warning: %s\n", w.c_str());
    return true;
}

std::vector<std::string> namesEndingIn(const reluva::SearchSet &files, const char *extension) {
    std::vector<std::string> out;
    for (const std::string &name : files.list())
        if (reluva::fs::toLower(extensionOf(name)) == extension)
            out.push_back(name);
    return out;
}

void printSam(const SamFile &sam) {
    for (const SamFile::Entry &e : sam.entries()) {
        std::printf("%5d  %s", e.line, e.name.c_str());
        for (const SamFile::Value &v : e.values)
            std::printf(v.isText ? "  \"%s\"" : "  %s",
                        v.isText ? v.text.c_str() : reluva::text::format("%g", v.number).c_str());
        std::printf("\n");
        for (const std::string &row : e.grid)
            std::printf("         | %s\n", row.c_str());
    }
}

// Every settings file of a game read; with a name, that one's settings.
int cmdSamCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw sam-check <game folder> [name in Data]\n");
        return 2;
    }
    reluva::SearchSet files;
    if (!mount(argv[2], files))
        return 1;
    const std::vector<std::string> names = argc > 3 ? std::vector<std::string>{argv[3]} : namesEndingIn(files, ".sam");
    int failed = 0;
    size_t entries = 0;
    size_t grids = 0;
    for (const std::string &name : names) {
        std::vector<uint8_t> data;
        SamFile sam;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = sam.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (argc > 3)
            printSam(sam);
        entries += sam.entries().size();
        for (const SamFile::Entry &e : sam.entries())
            grids += !e.grid.empty();
    }
    std::printf("%zu settings files read, %d failed: %zu settings, %zu of them grids\n", names.size() - size_t(failed),
                failed, entries, grids);
    return failed ? 1 : 0;
}

const Commands kCommands("tpw", {
                                    {"sam-check", "<game folder> [name in Data]",
                                     "read every settings file (.sam), or list one's settings", cmdSamCheck},
                                });

} // namespace
