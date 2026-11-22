// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco commands for object definitions
// (docs/engines/legoloco/formats/dat.md).

#include "common/archive.h"
#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoloco/formats/object_definition.h"
#include "engines/legoloco/formats/resource_file.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

void printGrid(const char *name, const legoloco::Occupancy &grid) {
    std::printf("%s: %d columns, %d rows, %d layers\n", name, grid.columns, grid.rows, grid.layers);
    for (int layer = 0; layer < grid.layers; ++layer) {
        for (int row = 0; row < grid.rows; ++row) {
            std::printf("  ");
            for (int column = 0; column < grid.columns; ++column)
                std::printf(" %d", grid.at(layer, row, column));
            std::printf("\n");
        }
        if (layer + 1 < grid.layers)
            std::printf("\n");
    }
}

void print(const legoloco::ObjectDefinition &def) {
    for (size_t p = 0; p < def.parts.size(); ++p) {
        std::printf("part %zu:\n", p + 1);
        for (const auto &line : def.parts[p]) {
            std::printf("  %s", line.keyword.c_str());
            for (const auto &v : line.values)
                std::printf(" %s", v.c_str());
            std::printf("\n");
        }
    }
    if (def.physical)
        printGrid("physical occupancy", *def.physical);
    if (def.bitmap)
        printGrid("bitmap occupancy", *def.bitmap);
    if (def.frameSets.empty())
        return;
    std::printf("%d frames; cursor frame sets %d %d\n", def.totalFrames, def.cursorFrameSets[0],
                def.cursorFrameSets[1]);
    std::printf("  %-14s %5s %5s %5s %5s %7s %5s %6s %7s %5s %4s\n", "frame set", "start", "end", "delay", "split",
                "restart", "next", "sound", "replay", "tenth", "flip");
    for (const auto &s : def.frameSets)
        std::printf("  %-14s %5u %5u %5u %5u %7d %5d %6d %7d %5u %4u\n", s.name.c_str(), s.start, s.end, s.delay,
                    s.split, s.restartDelay, s.next, s.sound, s.replayDelay, s.unnamed, s.flipX);
}

// With a name, that definition in full; without, every one read, and those that fail.
int cmdDat(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco dat <game or art-res folder> [name.dat]\n");
        return 2;
    }
    std::filesystem::path folder = argv[2];
    if (const auto artRes = fs::findDirectory(folder, "art-res"))
        folder = *artRes;
    legoloco::ResourceFile rf;
    if (Error err = rf.open(folder)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<size_t> wanted;
    if (argc > 3) {
        const int entry = rf.find(argv[3]);
        if (entry < 0) {
            std::fprintf(stderr, "no %s in the resource file\n", argv[3]);
            return 1;
        }
        wanted.push_back(size_t(entry));
    } else {
        for (size_t i = 0; i < rf.entries().size(); ++i)
            if (normalizeArchiveName(rf.entries()[i].name).ends_with(".dat"))
                wanted.push_back(i);
    }
    int failed = 0;
    int withFrames = 0;
    int withGrids = 0;
    for (size_t i : wanted) {
        std::vector<uint8_t> data;
        legoloco::ObjectDefinition def;
        Error err = rf.readEntry(i, data);
        if (!err)
            err = legoloco::parseObjectDefinition(
                std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), def);
        if (err) {
            std::printf("%s: %s\n", rf.entries()[i].name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (wanted.size() == 1)
            print(def);
        withFrames += !def.frameSets.empty();
        withGrids += def.physical.has_value();
    }
    if (wanted.size() > 1)
        std::printf("%zu definitions: %d with frame sets, %d with a physical occupancy, %d failed\n", wanted.size(),
                    withFrames, withGrids, failed);
    return failed ? 1 : 0;
}

const Commands kCommands("legoloco", {
                                         {"dat", "<game or art-res folder> [name.dat]",
                                          "read an object definition from the resource file and print it; without a "
                                          "name, read every one and list those that fail",
                                          cmdDat},
                                     });

} // namespace
