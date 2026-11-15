// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/registry.h"

#include <algorithm>

namespace reluva {

// Each engine exposes one accessor. The list of those built is generated
// from engines.cfg (cmake/ReluvaEngines.cmake), the testbed's first.
#define RELUVA_ENGINE(accessor) const MetaEngine &accessor();
#include "engines/engine_list.inc"
#undef RELUVA_ENGINE

const std::vector<const MetaEngine *> &allEngines() {
    static const std::vector<const MetaEngine *> kEngines = {
#define RELUVA_ENGINE(accessor) &accessor(),
#include "engines/engine_list.inc"
#undef RELUVA_ENGINE
    };
    return kEngines;
}

const MetaEngine *findEngine(std::string_view id) {
    for (const MetaEngine *e : allEngines())
        if (id == e->id())
            return e;
    return nullptr;
}

std::vector<DetectedGame> detectGames(const std::filesystem::path &dir) {
    std::vector<DetectedGame> all;
    for (const MetaEngine *e : allEngines()) {
        auto found = e->detect(dir);
        all.insert(all.end(), found.begin(), found.end());
    }
    return all;
}

std::filesystem::path gameFolderBelow(const std::filesystem::path &folder) {
    // Two levels down at most, in name order, the first with a game.
    std::error_code ec;
    std::vector<std::filesystem::path> level{folder};
    for (int depth = 0; depth < 2; ++depth) {
        std::vector<std::filesystem::path> next;
        for (const auto &dir : level) {
            std::vector<std::filesystem::path> children;
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
                if (entry.is_directory(ec))
                    children.push_back(entry.path());
            std::sort(children.begin(), children.end());
            for (const auto &child : children)
                if (!detectGames(child.string()).empty())
                    return child;
            next.insert(next.end(), children.begin(), children.end());
        }
        level = std::move(next);
    }
    return {};
}

std::vector<ImportPlan> planImports(const std::filesystem::path &source) {
    std::vector<ImportPlan> all;
    for (const MetaEngine *e : allEngines()) {
        auto found = e->planImport(source);
        all.insert(all.end(), found.begin(), found.end());
    }
    return all;
}

} // namespace reluva
