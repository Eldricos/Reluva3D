// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's shared commands for finding and importing games, as the
// launcher does.

#include "tools/tool.h"

#include "engines/registry.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// What the launcher finds in a folder (Add Game), and what it would
// import from one (Import Game), done here without the launcher: with a
// destination, the files are copied there.
int cmdDetect(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool detect <folder>\n");
        return 1;
    }
    const auto found = reluva::detectGames(argv[2]);
    for (const reluva::DetectedGame& g : found)
        std::printf("%s/%s: %s (%s, %s)\n", g.engineId.c_str(), g.gameId.c_str(), g.description.c_str(),
                    g.language.c_str(), g.platform.c_str());
    if (found.empty())
        std::printf("no game found\n");
    return found.empty() ? 1 : 0;
}

int cmdImport(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool import <source folder> [destination folder] [-patch file]...\n");
        return 1;
    }
    std::vector<std::string> patches, positional;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-patch") == 0 && i + 1 < argc)
            patches.emplace_back(argv[++i]);
        else
            positional.emplace_back(argv[i]);
    }
    auto plans = reluva::planImports(argv[2]);
    if (plans.empty()) {
        std::printf("nothing to import found in %s\n", argv[2]);
        return 1;
    }
    for (const std::string& patch : patches) {
        std::string message;
        const reluva::MetaEngine* meta = reluva::findEngine(plans.front().game.engineId);
        if (!meta || !meta->addToImport(plans.front(), patch, message))
            std::printf("%s: %s\n", patch.c_str(), message.c_str());
    }
    for (const reluva::ImportPlan& plan : plans) {
        std::printf("%s: %zu files, %.1f MB\n", plan.game.description.c_str(), plan.files.size(),
                    double(plan.totalBytes()) / (1024 * 1024));
        for (const std::string& note : plan.notes)
            std::printf("  %s\n", note.c_str());
        for (const reluva::ImportPlan::File& f : plan.files)
            std::printf("  %-28s <- %s%s\n", f.to.c_str(), f.from.string().c_str(),
                        f.packed ? (" (packed at " + std::to_string(f.packed->offset) + ")").c_str() : "");
    }
    if (positional.empty())
        return 0;
    const std::string dest = positional.front();
    int lastPercent = -1;
    if (reluva::Error err = reluva::runImport(plans.front(), dest, [&](uint64_t done, uint64_t total) {
            const int percent = total ? int(done * 100 / total) : 100;
            if (percent / 10 != lastPercent / 10)
                std::printf("  %d%%\n", percent);
            lastPercent = percent;
            return true;
        })) {
        std::fprintf(stderr, "import: %s\n", err.message.c_str());
        return 1;
    }
    std::printf("imported to %s; found there:\n", dest.c_str());
    std::string destArg = dest;
    char* detectArgs[] = {argv[0], argv[1], destArg.data()};
    return cmdDetect(3, detectArgs);
}

const Commands commands("", {
    {"detect", "<folder>",
     "what the launcher finds in a folder (Add Game)",
     cmdDetect},
    {"import", "<source folder> [destination folder] [-patch file]...",
     "import a game as Import Game does: list what a folder holds to import, or copy it to a "
     "destination, applying patches",
     cmdImport},
});

} // namespace
