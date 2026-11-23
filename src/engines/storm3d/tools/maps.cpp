// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's maps
// (docs/engines/storm3d/games/armada/formats/bzn.md).
//
//   reluva-tool storm3d bzn <game folder> <name.bzn>
//   reluva-tool storm3d bzn-check <game folder>

#include "common/text.h"
#include "devtools/tool.h"
#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/game_files.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::storm3d::BznMap;
using reluva::storm3d::GameFiles;

// A map by name, with or without "bzn/" and ".bzn".
Error readMap(const GameFiles &files, std::string name, BznMap &map) {
    if (!reluva::text::hasSuffixIgnoreCase(name, ".bzn"))
        name += ".bzn";
    std::vector<uint8_t> data;
    if (files.read(name, data) && files.read("bzn/" + name, data))
        return Error::fail(name + " isn't in the install");
    return map.parse(data);
}

int cmdBzn(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d bzn <game folder> <name.bzn>\n");
        return 2;
    }
    GameFiles files;
    BznMap map;
    Error err = files.open(argv[2]);
    if (!err)
        err = readMap(files, argv[3], map);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("version %d, \"%s\" (saved as %s), %zu fields\n", map.version, map.description.c_str(),
                map.name.c_str(), map.fields.size());
    std::printf("area: x %g to %g, y %g to %g, z %g to %g\n", map.area[0], map.area[1], map.area[2], map.area[3],
                map.area[4], map.area[5]);
    std::printf("light %g %g %g; background %s; tech tree %s; race %s\n", map.light[0], map.light[1], map.light[2],
                map.background.c_str(), map.techTree.c_str(), map.race.c_str());
    for (const std::string &s : map.scripts)
        std::printf("script %s\n", s.c_str());
    std::printf("%zu objects:\n", map.objects.size());
    for (const BznMap::Object &o : map.objects)
        std::printf("  %-10s team %d at %9.1f %8.1f %9.1f  %s (%zu fields)\n", o.odf.c_str(), o.team, o.position[0],
                    o.position[1], o.position[2], o.label.c_str(), o.fieldCount);
    std::printf("%zu paths:\n", map.paths.size());
    for (const BznMap::Path &p : map.paths)
        std::printf("  %-16s %zu points, from %.1f %.1f %.1f (kind %u)\n", p.name.c_str(), p.points.size(),
                    p.points.empty() ? 0.0f : p.points[0][0], p.points.empty() ? 0.0f : p.points[0][1],
                    p.points.empty() ? 0.0f : p.points[0][2], p.kind);
    return 0;
}

int cmdBznCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d bzn-check <game folder>\n");
        return 2;
    }
    GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    size_t objects = 0;
    size_t paths = 0;
    std::map<std::string, int> definitions; // how many maps place each
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".bzn"))
            continue;
        BznMap map;
        if (Error err = readMap(files, name, map)) {
            std::printf("%s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        objects += map.objects.size();
        paths += map.paths.size();
        std::map<std::string, bool> here;
        for (const BznMap::Object &o : map.objects)
            here[reluva::text::toLower(o.odf)] = true;
        for (const auto &[odf, yes] : here)
            ++definitions[odf];
    }
    std::printf("%d maps read, %d failed; %zu objects of %zu definitions, %zu paths\n", read, failed, objects,
                definitions.size(), paths);
    return failed ? 1 : 0;
}

const reluva::tool::Commands kCommands(
    "storm3d",
    {
        {"bzn", "<game folder> <name.bzn>",
         "a map: its settings, its objects (definition, team, position, name), its scripts and paths", cmdBzn},
        {"bzn-check", "<game folder>", "read every map and report what fails", cmdBznCheck},
    });

} // namespace
