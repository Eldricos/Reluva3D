// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's models
// (docs/engines/storm3d/games/armada/formats/sod.md).
//
//   reluva-tool storm3d sod <game folder> <name.sod>
//   reluva-tool storm3d sod-check <game folder>

#include "common/text.h"
#include "engines/storm3d/formats/sod.h"
#include "engines/storm3d/game_files.h"
#include "devtools/tool.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::storm3d::GameFiles;
using reluva::storm3d::SodModel;

Error readModel(const GameFiles &files, const std::string &name, SodModel &model) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (Error err = model.parse(data))
        return Error::fail(name + ": " + err.message);
    return Error::success();
}

int cmdSod(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d sod <game folder> <name.sod>\n");
        return 2;
    }
    GameFiles files;
    SodModel model;
    std::string name = argv[3];
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (!files.has(name))
        name = "SOD/" + name;
    if (Error err = readModel(files, name, model)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("%s %.1f: %zu texture refs, %zu materials, %zu nodes, %zu meshes, %zu animations, %zu texture "
                "animations\n",
                model.magic.c_str(), double(model.version), model.textureRefs.size(), model.materials.size(),
                model.nodes.size(), model.meshes.size(), model.animations.size(), model.textureAnimations.size());
    for (const auto &m : model.materials)
        std::printf("material %-20s ambient %.2f %.2f %.2f  diffuse %.2f %.2f %.2f  specular %.2f %.2f %.2f  power "
                    "%g  mode %d\n",
                    m.name.c_str(), double(m.ambient[0]), double(m.ambient[1]), double(m.ambient[2]),
                    double(m.diffuse[0]), double(m.diffuse[1]), double(m.diffuse[2]), double(m.specular[0]),
                    double(m.specular[1]), double(m.specular[2]), double(m.power), m.mode);
    for (const auto &n : model.nodes) {
        std::string about;
        if (n.mesh >= 0) {
            const SodModel::Mesh &mesh = model.meshes[size_t(n.mesh)];
            size_t faces = 0;
            for (const auto &g : mesh.groups)
                faces += g.faces.size();
            about = reluva::text::format("mesh %s%s%s, %zu vertices, %zu faces", mesh.texture.c_str(),
                                         mesh.drawMode.empty() ? "" : " ", mesh.drawMode.c_str(), mesh.vertices.size(),
                                         faces);
        } else if (n.type == SodModel::kEmitter) {
            about = "emitter " + n.emitter;
        }
        std::printf("node %2d %-24s parent %-20s at %.1f %.1f %.1f  %s\n", n.type, n.name.c_str(), n.parent.c_str(),
                    double(n.transform[9]), double(n.transform[10]), double(n.transform[11]), about.c_str());
    }
    for (const auto &a : model.animations)
        if (!a.keys.empty())
            std::printf("animation %s: %zu keys over %g s\n", a.node.c_str(), a.keys.size(), double(a.length));
    for (const auto &t : model.textureAnimations)
        std::printf("texture animation %s: %s\n", t.node.c_str(), t.animation.c_str());
    return 0;
}

int cmdSodCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d sod-check <game folder>\n");
        return 2;
    }
    GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    std::map<std::string, int> versions;
    size_t nodes = 0;
    size_t meshes = 0;
    size_t faces = 0;
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".sod"))
            continue;
        SodModel model;
        if (Error err = readModel(files, name, model)) {
            std::printf("not read: %s\n", err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        ++versions[reluva::text::format("%s %.1f", model.magic.c_str(), double(model.version))];
        nodes += model.nodes.size();
        meshes += model.meshes.size();
        for (const auto &m : model.meshes)
            for (const auto &g : m.groups)
                faces += g.faces.size();
    }
    for (const auto &[v, n] : versions)
        std::printf("%5d  %s\n", n, v.c_str());
    std::printf("%d models read whole, %d failed; %zu nodes, %zu meshes, %zu triangles\n", read, failed, nodes, meshes,
                faces);
    return failed ? 1 : 0;
}

const reluva::tool::Commands
    kCommands("storm3d",
              {
                  {"sod", "<game folder> <name.sod>", "a model's materials, nodes, meshes and animations", cmdSod},
                  {"sod-check", "<game folder>", "read every model whole, and count them by version", cmdSodCheck},
              });

} // namespace
