// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool awe-path: Reluva's port of the game's path-finder beside the
// game's own (motoe.exe's, run in the x86 interpreter), on one scene's
// walk grid or, with -all, on many cells of every scene.

#include "tools/awe_tool.h"

#include "common/fs.h"
#include "engines/awe/ac2.h"
#include "engines/awe/game_paths.h"
#include "engines/awe/walk_grid.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

namespace reluva::awe {

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

double since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// Both searches between two cells; true if they agree to the bit.
bool compare(GamePathFinder& game, const WalkGrid& grid, WalkGrid::Cell from, WalkGrid::Cell to, bool openThrees,
             bool allowTwos, double& gameMs, double& oursMs, std::string& why) {
    std::vector<GamePathFinder::Point> theirs;
    auto t = std::chrono::steady_clock::now();
    if (Error e = game.search(grid, openThrees, from, to, allowTwos, theirs)) {
        why = e.message;
        return false;
    }
    gameMs += since(t);
    std::vector<WalkGrid::Point> ours;
    t = std::chrono::steady_clock::now();
    grid.search(from, to, openThrees, allowTwos, ours);
    oursMs += since(t);
    bool same = theirs.size() == ours.size();
    for (size_t i = 0; same && i < ours.size(); i++)
        same = std::memcmp(&theirs[i].a, &ours[i].a, 8) == 0 && std::memcmp(&theirs[i].b, &ours[i].b, 8) == 0 &&
               std::memcmp(&theirs[i].heading, &ours[i].heading, 8) == 0;
    if (!same) {
        char buf[96];
        why = "game:";
        for (const auto& p : theirs) {
            std::snprintf(buf, sizeof buf, " (%.17g, %.17g; %.17g)", p.a, p.b, p.heading);
            why += buf;
        }
        why += "\n    ours:";
        for (const auto& p : ours) {
            std::snprintf(buf, sizeof buf, " (%.17g, %.17g; %.17g)", p.a, p.b, p.heading);
            why += buf;
        }
    }
    return same;
}

// -all: from cells of each scene's grid to others, every way the game
// searches; `perScene` pairs each (all pairs of open cells if 0).
int compareAll(GamePathFinder& game, const std::string& folder, int perScene) {
    size_t scenes = 0, searches = 0, mismatches = 0, found = 0;
    double gameMs = 0, oursMs = 0;
    std::mt19937 random(1);
    const auto locations = fs::findDirectory(folder, "Locations");
    if (!locations) {
        std::fprintf(stderr, "no Locations folder in %s\n", folder.c_str());
        return 1;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*locations))
        if (entry.is_regular_file() && fs::toLower(entry.path().extension().string()) == ".ac2")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();
        Ac2Scene scene;
        if (parseAc2(readFile(file.string()), scene) || !scene.camera())
            continue;
        WalkGrid grid;
        if (!grid.load(*scene.camera()))
            continue;
        std::vector<WalkGrid::Cell> open;
        for (int a = 0; a < WalkGrid::kSize; a++)
            for (int b = 0; b < WalkGrid::kSize; b++)
                if (grid.at(a, b) != 1)
                    open.push_back({a, b});
        if (open.size() < 2)
            continue;
        scenes++;
        for (int k = 0; k < perScene; k++) {
            const WalkGrid::Cell from = open[random() % open.size()], to = open[random() % open.size()];
            for (int way = 0; way < 4; way++) {
                const bool openThrees = way & 1, allowTwos = way & 2;
                std::string why;
                searches++;
                if (!compare(game, grid, from, to, openThrees, allowTwos, gameMs, oursMs, why)) {
                    if (++mismatches <= 10)
                        std::printf("MISMATCH %s (%d, %d) -> (%d, %d)%s%s:\n    %s\n", name.c_str(), from.a, from.b,
                                    to.a, to.b, openThrees ? " 3s open" : "", allowTwos ? " 2s allowed" : "",
                                    why.c_str());
                    continue;
                }
                std::vector<WalkGrid::Point> points;
                found += grid.search(from, to, openThrees, allowTwos, points) > 0;
            }
        }
    }
    std::printf("%zu scenes, %zu searches (%zu with a path), %zu mismatches\n", scenes, searches, found, mismatches);
    std::printf("time: the game's %.0f ms, Reluva's %.0f ms\n", gameMs, oursMs);
    return mismatches ? 1 : 0;
}

} // namespace

int aweCommandPath(int argc, char** argv) {
    const bool all = argc >= 4 && std::strcmp(argv[3], "-all") == 0;
    if (argc < 4 || (!all && argc < 8)) {
        std::fprintf(stderr, "usage: reluva-tool awe-path <game folder> <scene.ac2> <a0> <b0> <a1> <b1>\n"
                             "       reluva-tool awe-path <game folder> -all [pairs per scene]\n"
                             "  Reluva's path-finder beside the game's (from motoe.exe); -all compares them\n"
                             "  point for point between random cells of every scene\n");
        return 2;
    }
    GamePathFinder game;
    if (Error e = game.load(readFile(std::string(argv[2]) + "/motoe.exe"))) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    if (all)
        return compareAll(game, argv[2], argc >= 5 ? std::atoi(argv[4]) : 100);

    Ac2Scene scene;
    if (parseAc2(readFile(argv[3]), scene) || !scene.camera()) {
        std::fprintf(stderr, "no scene with a camera in %s\n", argv[3]);
        return 1;
    }
    WalkGrid grid;
    if (!grid.load(*scene.camera())) {
        std::fprintf(stderr, "no walk grid in %s\n", argv[3]);
        return 1;
    }
    const WalkGrid::Cell from{std::atoi(argv[4]), std::atoi(argv[5])}, to{std::atoi(argv[6]), std::atoi(argv[7])};
    std::vector<GamePathFinder::Point> theirs;
    auto t = std::chrono::steady_clock::now();
    if (Error e = game.search(grid, false, from, to, false, theirs)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("game (%.1f ms):   %zu points:", since(t), theirs.size());
    for (const auto& p : theirs)
        std::printf(" (%g, %g; %.3f)", p.a, p.b, p.heading);
    std::vector<WalkGrid::Point> ours;
    t = std::chrono::steady_clock::now();
    grid.search(from, to, false, false, ours);
    std::printf("\nReluva (%.1f ms): %zu points:", since(t), ours.size());
    for (const auto& p : ours)
        std::printf(" (%g, %g; %.3f)", p.a, p.b, p.heading);
    std::printf("\nas it walks (0x440ce0's tries):");
    for (const WalkGrid::Cell& c : grid.findPath(from, to))
        std::printf(" (%d, %d)", c.a, c.b);
    std::printf("\n");
    return 0;
}

} // namespace reluva::awe
