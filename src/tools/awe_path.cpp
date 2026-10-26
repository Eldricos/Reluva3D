// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool awe-path: the game's own path-finder (motoe.exe's, run in the
// x86 interpreter) beside Reluva's A*, on a scene's walk grid.

#include "tools/awe_tool.h"

#include "engines/awe/ac2.h"
#include "engines/awe/game_paths.h"
#include "engines/awe/walk_grid.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace reluva::awe {

int aweCommandPath(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: reluva-tool awe-path <game folder> <scene.ac2> <a0> <b0> <a1> <b1>\n"
                             "  the game's path between two cells (from motoe.exe) and Reluva's\n");
        return 2;
    }
    auto read = [](const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    };
    Ac2Scene scene;
    if (parseAc2(read(argv[3]), scene) || !scene.camera()) {
        std::fprintf(stderr, "no scene with a camera in %s\n", argv[3]);
        return 1;
    }
    WalkGrid grid;
    if (!grid.load(*scene.camera())) {
        std::fprintf(stderr, "no walk grid in %s\n", argv[3]);
        return 1;
    }
    const WalkGrid::Cell from{std::atoi(argv[4]), std::atoi(argv[5])}, to{std::atoi(argv[6]), std::atoi(argv[7])};
    GamePathFinder game;
    if (Error e = game.load(read(std::string(argv[2]) + "/motoe.exe"))) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::vector<GamePathFinder::Point> points;
    const auto started = std::chrono::steady_clock::now();
    Error searched = game.search(grid, false, from, to, false, points);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (Error e = searched) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("game (%.1f ms):   %zu points:", ms, points.size());
    for (const auto& p : points)
        std::printf(" (%g, %g; %.3f)", p.a, p.b, p.heading);
    std::printf("\ngame, as it walks (0x440ce0's tries):");
    for (const WalkGrid::Cell& c : game.findPath(grid, from, to, false))
        std::printf(" (%d, %d)", c.a, c.b);
    std::printf("\nReluva's A*:");
    const std::vector<WalkGrid::Cell> ours = grid.findPath(from, to);
    std::printf(" %zu points:", ours.size());
    for (const auto& c : ours)
        std::printf(" (%d, %d)", c.a, c.b);
    std::printf("\n");
    return 0;
}

} // namespace reluva::awe
