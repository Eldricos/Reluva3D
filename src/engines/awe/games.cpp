// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/games.h"

#include "engines/awe/attwn/attwn.h"
#include "engines/awe/motoe/motoe.h"

namespace reluva::awe {

const std::vector<const GameInfo *> &games() {
    static const std::vector<const GameInfo *> kGames = {&motoe::game(), &attwn::game()};
    return kGames;
}

const GameInfo *findGame(std::string_view id) {
    for (const GameInfo *g : games())
        if (id == g->id)
            return g;
    return nullptr;
}

std::vector<DetectedGame> detectGames(const std::filesystem::path &dir) {
    std::vector<DetectedGame> found;
    for (const GameInfo *g : games())
        for (DetectedGame &d : detectFromTable("awe", g->variants, dir))
            found.push_back(std::move(d));
    if (found.empty())
        for (const GameInfo *g : games())
            for (DetectedGame &d : detectFromTable("awe", g->fallback, dir))
                found.push_back(std::move(d));
    return found;
}

Error openGameFiles(const GameInfo *game, const std::filesystem::path &dir, std::unique_ptr<Archive> &out) {
    if (game && game->openFiles)
        return game->openFiles(dir, out);
    auto files = std::make_unique<DirectoryArchive>();
    if (Error err = files->open(dir))
        return err;
    out = std::move(files);
    return Error::success();
}

Error openInstall(const std::filesystem::path &dir, std::unique_ptr<Archive> &out, const GameInfo **game) {
    const std::vector<DetectedGame> found = detectGames(dir);
    const GameInfo *which = found.empty() ? nullptr : findGame(found.front().gameId);
    if (game)
        *game = which;
    return openGameFiles(which, dir, out);
}

} // namespace reluva::awe
