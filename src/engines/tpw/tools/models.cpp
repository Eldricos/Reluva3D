// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's models (.MD2).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/md2.h"
#include "engines/tpw/game_files.h"

#include <cmath>
#include <cstdio>
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

const Commands kCommands("tpw", {
                                    {"md2-check", "<game folder> [model]",
                                     "read every model (.MD2), or list one's meshes", cmdMd2Check},
                                });

} // namespace
