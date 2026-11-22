// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco commands for EE.INI and the area layouts
// (docs/engines/legoloco/formats/ee-ini.md, lay.md).

#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoloco/formats/area_layout.h"
#include "engines/legoloco/formats/easter_eggs.h"
#include "engines/legoloco/formats/resource_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

std::filesystem::path artRes(const char *arg) {
    std::filesystem::path folder = arg;
    if (const auto found = fs::findDirectory(folder, "art-res"))
        folder = *found;
    return folder;
}

// The game reads art-res's EE.INI from disk; given a file, that file.
int cmdEe(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco ee <game or art-res folder | EE.INI>\n");
        return 2;
    }
    std::filesystem::path path = argv[2];
    if (!std::filesystem::is_regular_file(path)) {
        const auto found = fs::findFile(artRes(argv[2]), "ee.ini");
        if (!found) {
            std::fprintf(stderr, "no EE.INI in %s\n", argv[2]);
            return 1;
        }
        path = *found;
    }
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    legoloco::EasterEggs eggs;
    if (Error err = legoloco::parseEasterEggs(text, eggs)) {
        std::fprintf(stderr, "%s: %s\n", path.string().c_str(), err.message.c_str());
        return 1;
    }
    std::printf("[TimeEvents] %zu\n", eggs.timeEvents.size());
    std::printf("  %-11s %-11s %-11s %8s %4s %6s %4s %5s %5s\n", "from", "to", "hours", "resource", "set", "period",
                "type", "x", "y");
    for (const auto &e : eggs.timeEvents)
        std::printf("  %2d/%-2d       %2d/%-2d       %02d:%02d-%02d:%02d %8d %4d %6d %4c %5d %5d\n", e.startDay,
                    e.startMonth, e.endDay, e.endMonth, e.startHour, e.startMinute, e.endHour, e.endMinute, e.resource,
                    e.frameSet, e.period, e.type, e.x, e.y);
    std::printf("[LoadEvents] %zu\n", eggs.loadEvents.size());
    std::printf("  %-11s %-11s %8s %11s\n", "from", "to", "resource", "replacement");
    for (const auto &e : eggs.loadEvents)
        std::printf("  %2d/%-2d       %2d/%-2d       %8d %11d\n", e.startDay, e.startMonth, e.endDay, e.endMonth,
                    e.resource, e.replacement);
    return 0;
}

// Every arrangement index.lay names, from the resource file.
int cmdLay(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco lay <game or art-res folder>\n");
        return 2;
    }
    legoloco::ResourceFile rf;
    if (Error err = rf.open(artRes(argv[2]))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    if (Error err = rf.read("layouts/index.lay", data)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int failed = 0;
    for (const std::string &name :
         legoloco::parseLayoutIndex(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()))) {
        legoloco::AreaLayout layout;
        Error err = rf.read("layouts/" + name + ".lay", data);
        if (!err)
            err = legoloco::parseAreaLayout(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()),
                                            layout);
        if (err) {
            std::printf("%s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        std::printf("%-6s %d areas, %d by %d:", name.c_str(), layout.areas, layout.columns, layout.rows);
        for (const auto &board : layout.boards)
            std::printf(" %s%s", board.c_str(), rf.has("layouts/" + board) ? "" : " (missing)");
        std::printf("\n");
    }
    return failed ? 1 : 0;
}

const Commands kCommands("legoloco",
                         {
                             {"ee", "<game or art-res folder | EE.INI>", "print EE.INI's date-based surprises", cmdEe},
                             {"lay", "<game or art-res folder>",
                              "print the area layouts' arrangements that layouts\\index.lay names", cmdLay},
                         });

} // namespace
