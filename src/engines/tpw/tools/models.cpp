// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's models (.MD2) and
// their height maps (.hmp).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/animation.h"
#include "engines/tpw/formats/hmp.h"
#include "engines/tpw/formats/md2.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/model_view.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::Model;

// Whether the meshes, placed, fill the header's box (to 1% of its size):
// most models' do, and a reader that got the matrices wrong wouldn't.
bool fillsItsBox(const Model &model) {
    std::array<float, 6> box = {INFINITY, INFINITY, INFINITY, -INFINITY, -INFINITY, -INFINITY};
    for (const Model::Mesh &mesh : model.meshes())
        for (const reluva::gfx::Vec3 &p : mesh.positions) {
            const reluva::gfx::Vec3 q = reluva::gfx::transformPoint(mesh.transform, p);
            for (size_t i = 0; i < 3; ++i) {
                box[i] = std::min(box[i], q[i]);
                box[i + 3] = std::max(box[i + 3], q[i]);
            }
        }
    float size = 1;
    for (float v : model.bounds())
        size = std::max(size, std::fabs(v));
    for (size_t i = 0; i < 6; ++i)
        if (!(std::fabs(box[i] - model.bounds()[i]) <= size / 100))
            return false;
    return true;
}

// Every model of a game read: how many of each kind, their meshes and
// faces, with a checksum; with a model's name, its meshes and textures.
int cmdMd2Check(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw md2-check <game folder> [model]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::map<std::string, int> kinds;
    reluva::Md5 all;
    int failed = 0;
    int boxes = 0;
    size_t models = 0;
    size_t meshes = 0;
    size_t faces = 0;
    for (const std::string &name : files.list()) {
        if (reluva::fs::toLower(extensionOf(name)) != ".md2" ||
            (argc > 3 && reluva::fs::toLower(name) != reluva::fs::toLower(argv[3])))
            continue;
        std::vector<uint8_t> data;
        Model model;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = model.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (model.kind() == Model::Kind::kAnimation) {
            ++kinds["animations, not read"];
            continue;
        }
        if (model.kind() == Model::Kind::kSummary) {
            ++kinds["headers alone"];
            continue;
        }
        ++kinds["models"];
        ++models;
        if (fillsItsBox(model))
            ++boxes;
        for (const Model::Mesh &mesh : model.meshes()) {
            ++meshes;
            faces += mesh.faces.size();
            all.update(mesh.name.data(), mesh.name.size());
            all.update(mesh.transform.data(), sizeof(mesh.transform));
            all.update(mesh.positions.data(), mesh.positions.size() * sizeof(mesh.positions[0]));
            all.update(mesh.positionOf.data(), mesh.positionOf.size() * sizeof(mesh.positionOf[0]));
            all.update(mesh.uv.data(), mesh.uv.size() * sizeof(mesh.uv[0]));
            for (const Model::Face &f : mesh.faces)
                all.update(f.vertex.data(), sizeof(f.vertex));
            if (argc > 3) {
                std::printf("mesh %-20s %5zu positions %5zu vertices %5zu faces\n", mesh.name.c_str(),
                            mesh.positions.size(), mesh.uv.size(), mesh.faces.size());
                for (const Model::Group &g : mesh.groups)
                    std::printf("  faces %4zu-%-4zu %s\n", g.firstFace, g.firstFace + g.faceCount,
                                g.texture == Model::Group::kNoTexture ? "(none)"
                                                                      : model.textures()[g.texture].name.c_str());
            }
        }
    }
    for (const auto &[kind, n] : kinds)
        std::printf("  %-24s %d\n", kind.c_str(), n);
    std::printf("%d failed; %zu models, %zu meshes, %zu faces; %d of them fill their header's box; md5 %s\n", failed,
                models, meshes, faces, boxes, all.hexDigest().c_str());
    return models == 0 ? 1 : 0;
}

// One model drawn as the viewer draws it, written out as a PNG: from the
// viewer's starting place, or turned and tilted by the radians given;
// with -heights, its height map's samples over it; with -anim, posed by
// one of its animations at a frame.
int cmdMd2Shot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(
            stderr,
            "usage: reluva-tool tpw md2-shot <game folder> <model> <out.png> [yaw pitch] [-heights] [-anim <file> "
            "<frame>]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::tpw::LoadedModel loaded;
    if (reluva::Error err = loaded.load(files, argv[3])) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    reluva::tpw::ModelView view;
    int at = 5;
    if (argc > at + 1 && std::strcmp(argv[at], "-heights") != 0) {
        view.yaw = std::strtof(argv[at], nullptr);
        view.pitch = std::strtof(argv[at + 1], nullptr);
        at += 2;
    }
    for (; at < argc; ++at) {
        if (std::strcmp(argv[at], "-heights") == 0) {
            view.heights = true;
        } else if (std::strcmp(argv[at], "-anim") == 0 && at + 2 < argc) {
            // An animation of the model's, by its file's name, at a frame.
            for (size_t i = 0; i < loaded.animationNames.size(); ++i)
                if (reluva::fs::toLower(loaded.animationNames[i]) == reluva::fs::toLower(argv[at + 1]))
                    view.animation = int(i);
            if (view.animation < 0) {
                std::fprintf(stderr, "%s isn't one of the model's animations\n", argv[at + 1]);
                return 1;
            }
            view.frame = std::strtof(argv[at + 2], nullptr);
            at += 2;
        }
    }
    reluva::Surface screen;
    screen.create(640, 480, reluva::PixelFormat::kRGBA32);
    // The viewer's background.
    for (int y = 0; y < screen.h; ++y)
        for (int x = 0; x < screen.w; ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            p[0] = 44;
            p[1] = 52;
            p[2] = 66;
            p[3] = 255;
        }
    reluva::soft3d::Rasterizer rasterizer;
    reluva::tpw::drawModel(screen, rasterizer, loaded, view);
    reluva::image::Image out;
    out.w = screen.w;
    out.h = screen.h;
    out.rgba.resize(size_t(screen.w) * size_t(screen.h) * 4);
    for (int y = 0; y < screen.h; ++y)
        std::copy(screen.row(y), screen.row(y) + screen.w * 4, out.pixel(0, y));
    const std::vector<uint8_t> png = reluva::image::encodePng(out);
    std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char *>(png.data()), long(png.size()));
    if (loaded.model.kind() != Model::Kind::kModel)
        std::printf("%s: not a model but %s: nothing drawn\n", argv[3],
                    loaded.model.kind() == Model::Kind::kAnimation ? "an animation" : "a header alone");
    else
        std::printf("%s: %zu meshes, %zu of %zu textures found\n", argv[3], loaded.model.meshes().size(),
                    loaded.texturesFound, loaded.textures.size());
    return 0;
}

// Every height map of a game read: their tiles, how many tiles' heights
// are their highest sample's, with a checksum of the maps.
int cmdHmpCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw hmp-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    using reluva::tpw::HeightMap;
    constexpr int kSide = HeightMap::kSamplesPerTile;
    reluva::Md5 all;
    int failed = 0;
    size_t maps = 0;
    size_t tiles = 0;
    size_t highest = 0;
    size_t flagged = 0;
    for (const std::string &name : files.list()) {
        if (reluva::fs::toLower(extensionOf(name)) != ".hmp")
            continue;
        std::vector<uint8_t> data;
        HeightMap map;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = map.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++maps;
        for (int tz = 0; tz < map.depth(); ++tz)
            for (int tx = 0; tx < map.width(); ++tx) {
                int top = 0;
                for (int z = 0; z < kSide; ++z)
                    for (int x = 0; x < kSide; ++x)
                        top = std::max(top, int(map.sample(tx * kSide + x, tz * kSide + z)));
                const size_t t = size_t(tz * map.width() + tx);
                highest += map.tileHeights()[t] == top;
                flagged += map.tileFlags()[t] != 0;
                ++tiles;
            }
        for (int z = 0; z < map.depth() * kSide; ++z)
            for (int x = 0; x < map.width() * kSide; ++x) {
                const uint8_t v = map.sample(x, z);
                all.update(&v, 1);
            }
        all.update(map.tileHeights().data(), map.tileHeights().size());
        all.update(map.tileFlags().data(), map.tileFlags().size());
    }
    std::printf("%d failed; %zu height maps, %zu tiles; %zu tiles' heights their highest sample's, %zu tiles "
                "flagged; md5 %s\n",
                failed, maps, tiles, highest, flagged, all.hexDigest().c_str());
    return failed || maps == 0 ? 1 : 0;
}

// The model an animation moves: of the models in its folder (`models`,
// by folder), the one whose name its own starts with, longest ("wateride"
// for "waterideb"); empty for none.
std::string modelOf(const std::map<std::string, std::vector<std::string>> &models, const std::string &animation) {
    const size_t slash = animation.rfind('/');
    const std::string folder = animation.substr(0, slash + 1);
    const std::string stem = reluva::fs::toLower(animation.substr(slash + 1, animation.size() - slash - 5));
    std::string best;
    size_t bestLength = 0;
    const auto inFolder = models.find(folder);
    if (inFolder == models.end())
        return best;
    for (const std::string &name : inFolder->second) {
        const std::string other = reluva::fs::toLower(name.substr(folder.size(), name.size() - folder.size() - 4));
        if (other.size() < stem.size() && stem.compare(0, other.size(), other) == 0 && other.size() > bestLength) {
            best = name;
            bestLength = other.size();
        }
    }
    return best;
}

// Every animation of a game read and set against its model: its tracks'
// nodes in range, and how many tracks start at their node's rest pose.
int cmdAnimCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw anim-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    using reluva::tpw::Animation;
    reluva::Md5 all;
    int failed = 0;
    int animations = 0;
    int withoutModel = 0;
    size_t tracks = 0;
    size_t moved = 0;
    size_t outOfRange = 0;
    size_t atRest = 0;
    size_t other = 0;
    // The models and the animations, the models by folder.
    std::map<std::string, std::vector<std::string>> models;
    std::vector<std::string> animationNames;
    for (const std::string &name : files.list()) {
        if (reluva::fs::toLower(extensionOf(name)) != ".md2")
            continue;
        std::vector<uint8_t> data;
        Model probe;
        if (files.read(name, data) || probe.parse(data))
            continue;
        if (probe.kind() == Model::Kind::kModel)
            models[name.substr(0, name.rfind('/') + 1)].push_back(name);
        else if (probe.kind() == Model::Kind::kAnimation)
            animationNames.push_back(name);
    }
    for (const std::string &name : animationNames) {
        std::vector<uint8_t> data;
        if (files.read(name, data))
            continue;
        Animation animation;
        if (reluva::Error err = animation.parse(data)) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++animations;
        all.update(data.data(), data.size());
        const std::string modelName = modelOf(models, name);
        Model model;
        std::vector<uint8_t> modelData;
        const bool hasModel = !modelName.empty() && !files.read(modelName, modelData) && !model.parse(modelData);
        if (!hasModel)
            ++withoutModel;
        for (const Animation::Track &t : animation.tracks()) {
            ++tracks;
            other += t.hasOther;
            if (t.curve == Animation::Curve::kNone && t.rotations.empty() && t.scales.empty())
                continue;
            ++moved;
            if (!hasModel)
                continue;
            if (t.target >= model.nodes().size()) {
                ++outOfRange;
                continue;
            }
            const reluva::gfx::Mat4 &rest = model.nodes()[t.target].local;
            const reluva::gfx::Mat4 start = Animation::pose(t, 0, rest);
            float most = 0;
            for (size_t i = 0; i < 16; ++i)
                most = std::max(most, std::fabs(start[i] - rest[i]));
            atRest += most < 0.01f;
        }
    }
    std::printf("%d failed; %d animations (%d without a model of their name), %zu tracks: %zu moving whole nodes "
                "(%zu at nodes past their model's, %zu starting at their node's rest pose), %zu moving vertices or "
                "textures; md5 %s\n",
                failed, animations, withoutModel, tracks, moved, outOfRange, atRest, other, all.hexDigest().c_str());
    return failed || animations == 0 ? 1 : 0;
}

const Commands kCommands(
    "tpw",
    {
        {"md2-check", "<game folder> [model]", "read every model (.MD2), or list one's meshes", cmdMd2Check},
        {"hmp-check", "<game folder>", "read every height map (.hmp)", cmdHmpCheck},
        {"anim-check", "<game folder>", "read every model animation (.MD2) and set it against its model", cmdAnimCheck},
        {"md2-shot", "<game folder> <model> <out.png> [yaw pitch] [-heights] [-anim <file> <frame>]",
         "draw a model as the viewer does, to a PNG; with its height map's samples", cmdMd2Shot},
    });

} // namespace
