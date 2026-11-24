// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/scene/model_tree.h"

#include "common/text.h"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace reluva::storm3d {

namespace {

// The number after "lod" in a node's name, or -1.
int lodNumber(const std::string &name) {
    if (name.size() < 4 || !text::equalsIgnoreCase(name.substr(0, 3), "lod"))
        return -1;
    for (size_t i = 3; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9')
            return -1;
    return std::atoi(name.c_str() + 3);
}

// The index of the first node of that name, or -1.
int findNode(const SodModel &model, const std::string &name) {
    for (size_t i = 0; i < model.nodes.size(); ++i)
        if (model.nodes[i].name == name)
            return int(i);
    return -1;
}

} // namespace

gfx::Mat4 ModelTree::toMatrix(const SodModel::Transform &t) {
    gfx::Mat4 m = gfx::identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[size_t(i * 4 + j)] = t[size_t(i * 3 + j)];
    for (int j = 0; j < 3; ++j)
        m[size_t(12 + j)] = t[size_t(9 + j)];
    return m;
}

std::vector<gfx::Mat4> ModelTree::worldMatrices(const SodModel &model) {
    std::vector<gfx::Mat4> world(model.nodes.size(), gfx::identity());
    std::vector<int> state(model.nodes.size(), 0); // 0 not done, 1 being done, 2 done
    std::function<void(size_t)> place = [&](size_t i) {
        if (state[i] == 2)
            return;
        state[i] = 1;
        const SodModel::Node &n = model.nodes[i];
        const int parent = n.parent.empty() ? -1 : findNode(model, n.parent);
        gfx::Mat4 base = gfx::identity();
        // A parent that is its own ancestor is taken as a root.
        if (parent >= 0 && state[size_t(parent)] != 1) {
            place(size_t(parent));
            base = world[size_t(parent)];
        }
        world[i] = gfx::multiply(base, toMatrix(n.transform));
        state[i] = 2;
    };
    for (size_t i = 0; i < model.nodes.size(); ++i)
        place(i);
    return world;
}

std::vector<int> ModelTree::detailLevels(const SodModel &model) {
    std::vector<int> levels;
    for (const SodModel::Node &n : model.nodes)
        if (const int lod = lodNumber(n.name); lod >= 0 && std::find(levels.begin(), levels.end(), lod) == levels.end())
            levels.push_back(lod);
    std::sort(levels.begin(), levels.end());
    return levels;
}

bool ModelTree::shownAtLevel(const SodModel &model, size_t node, int level) {
    // Up the parents, at most as many as there are nodes, against loops.
    int at = int(node);
    for (size_t steps = 0; at >= 0 && steps <= model.nodes.size(); ++steps) {
        const SodModel::Node &n = model.nodes[size_t(at)];
        if (const int lod = lodNumber(n.name); lod >= 0 && lod != level)
            return false;
        at = n.parent.empty() ? -1 : findNode(model, n.parent);
    }
    return true;
}

bool ModelTree::assimilationPart(const SodModel &model, size_t node) {
    int at = int(node);
    for (size_t steps = 0; at >= 0 && steps <= model.nodes.size(); ++steps) {
        const SodModel::Node &n = model.nodes[size_t(at)];
        if (text::equalsIgnoreCase(n.name, "borg"))
            return true;
        at = n.parent.empty() ? -1 : findNode(model, n.parent);
    }
    return false;
}

} // namespace reluva::storm3d
