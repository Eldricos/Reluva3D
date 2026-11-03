// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "tools/tool.h"
#include <cstdio>

namespace {
int cmdResourceCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool boggle resource-check <game folder>\n");
        return 2;
    }
    reluva::boggle::Resources resources;
    if (auto err = resources.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::boggle::ResourceSummary summary;
    if (auto err = resources.check(summary)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("images %zu, sounds %zu, palettes %zu, dice sets %zu, meshes %zu, languages %zu\n",
                summary.images, summary.sounds, summary.palettes, summary.diceSets, summary.meshes, summary.languages);
    std::printf("vertices %zu, faces %zu, dice %zu, strings %zu\n",
                summary.vertices, summary.faces, summary.dice, summary.strings);
    std::printf("decoded resources: %s\n", summary.digest.c_str());
    std::printf("Not checked: word dictionaries, definitions, AVI video and TrueType fonts.\n");
    return 0;
}
const reluva::tool::Commands commands("boggle", {
    {"resource-check", "<game folder>", "validate supported resources; print counts and a decoded-content checksum", cmdResourceCheck},
});
} // namespace
