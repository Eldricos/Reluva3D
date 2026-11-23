// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the worlds' terrain: the
// park's ground (.lnd), the attribute maps (base.map) and the placed
// objects (scape.omp).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/lnd.h"
#include "engines/tpw/formats/omp.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/world_view.h"
#include "image/image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::AttributeMap;
using reluva::tpw::Land;
using reluva::tpw::ObjectMap;

// Every world's ground, attribute map and placed objects read: their
// sizes, each object, with a checksum of them all.
int cmdTerrainCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw terrain-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::Md5 all;
    int failed = 0;
    int grounds = 0;
    int maps = 0;
    int objectMaps = 0;
    for (const std::string &name : files.list()) {
        const std::string lower = reluva::fs::toLower(name);
        const bool isGround = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".lnd") == 0;
        const bool isMap = lower.size() > 9 && lower.compare(lower.size() - 9, 9, "/base.map") == 0;
        const bool isObjects = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".omp") == 0;
        if (!isGround && !isMap && !isObjects)
            continue;
        std::vector<uint8_t> data;
        reluva::Error err = files.read(name, data);
        Land land;
        AttributeMap map;
        ObjectMap objects;
        if (!err)
            err = isGround ? land.parse(data) : isMap ? map.parse(data) : objects.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (isObjects) {
            ++objectMaps;
            std::printf("%s: %zu objects\n", name.c_str(), objects.objects().size());
            for (const ObjectMap::Object &o : objects.objects()) {
                std::printf("  kind %u, %u, number %u at %.1f %.1f %.1f, radius %.1f, %.1f %.1f\n", o.kind, o.variant,
                            o.number, double(o.position[0]), double(o.position[1]), double(o.position[2]),
                            double(o.radius), double(o.extra[0]), double(o.extra[1]));
                all.update(&o, sizeof(o));
            }
            continue;
        }
        if (isMap) {
            ++maps;
            std::printf("%s: %dx%d tiles\n", name.c_str(), map.width(), map.height());
            for (int y = 0; y < map.height(); ++y)
                for (int x = 0; x < map.width(); ++x) {
                    const uint8_t v = map.at(x, y);
                    all.update(&v, 1);
                }
            continue;
        }
        ++grounds;
        std::printf("%s: %dx%d cells;", name.c_str(), land.width(), land.depth());
        for (const Land::Level &level : land.levels()) {
            std::printf(" %u: %zu textures from %dx%d, %zu masks;", level.scale, level.textures.size(),
                        level.textures.empty() ? 0 : level.textures[0].w,
                        level.textures.empty() ? 0 : level.textures[0].h, level.masks.size());
            for (const reluva::image::Image &t : level.textures)
                all.update(t.rgba.data(), t.rgba.size());
            for (const Land::Mask &m : level.masks)
                all.update(m.pixels.data(), m.pixels.size());
        }
        std::printf("\n");
        for (int z = 0; z < land.depth(); ++z)
            for (int x = 0; x < land.width(); ++x)
                all.update(land.cell(x, z), 4);
    }
    std::printf("%d failed; %d grounds, %d attribute maps, %d object maps; md5 %s\n", failed, grounds, maps, objectMaps,
                all.hexDigest().c_str());
    return failed || grounds == 0 ? 1 : 0;
}

// A world's terrain drawn as the viewer draws it, written out as a PNG:
// from the viewer's starting place, or turned, tilted and as far as given.
int cmdWorldShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw world-shot <game folder> <world folder> <out.png> [yaw pitch "
                             "distance]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::tpw::LoadedWorld world;
    if (reluva::Error err = world.load(files, argv[3])) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    reluva::tpw::WorldView view;
    if (argc > 7) {
        view.yaw = std::strtof(argv[5], nullptr);
        view.pitch = std::strtof(argv[6], nullptr);
        view.distance = std::strtof(argv[7], nullptr);
    }
    reluva::Surface screen;
    screen.create(640, 480, reluva::PixelFormat::kRGBA32);
    for (int y = 0; y < screen.h; ++y)
        for (int x = 0; x < screen.w; ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            p[0] = 120; // the viewer's sky blue
            p[1] = 160;
            p[2] = 210;
            p[3] = 255;
        }
    reluva::soft3d::Rasterizer rasterizer;
    reluva::tpw::drawWorld(screen, rasterizer, world, view);
    reluva::image::Image out;
    out.w = screen.w;
    out.h = screen.h;
    out.rgba.resize(size_t(screen.w) * size_t(screen.h) * 4);
    for (int y = 0; y < screen.h; ++y)
        std::copy(screen.row(y), screen.row(y) + screen.w * 4, out.pixel(0, y));
    const std::vector<uint8_t> png = reluva::image::encodePng(out);
    std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char *>(png.data()), long(png.size()));
    std::printf("%s: ground %s, attribute map %s\n", argv[3], world.hasLand ? "read" : "missing",
                world.hasAttributes ? "read" : "missing");
    return 0;
}

const Commands kCommands("tpw",
                         {
                             {"terrain-check", "<game folder>",
                              "read every world's ground (.lnd), attribute map (base.map) and objects (scape.omp)",
                              cmdTerrainCheck},
                             {"world-shot", "<game folder> <world folder> <out.png> [yaw pitch distance]",
                              "draw a world's terrain as the viewer does, to a PNG", cmdWorldShot},
                         });

} // namespace
