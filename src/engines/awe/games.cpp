// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/games.h"

#include "engines/awe/attwn/attwn.h"
#include "engines/awe/motoe/motoe.h"

#include "common/fs.h"

#include <algorithm>

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

namespace {

// The game's groups in `cabinet`, as its setup installs them.
std::vector<InstallShieldGroup> setupGroups(const GameInfo &game, const InstallShieldCabinet &cabinet) {
    return installShieldGroups(cabinet, game.setupGroups);
}

} // namespace

std::vector<ImportPlan> planImport(const std::filesystem::path &source) {
    const auto header = fs::findFile(source, "data1.hdr");
    if (!header)
        return {};
    auto cabinet = std::make_shared<InstallShieldCabinet>();
    if (readInstallShieldCabinet(*header, *cabinet))
        return {};
    for (const GameInfo *game : games()) {
        const std::vector<InstallShieldGroup> groups = setupGroups(*game, *cabinet);
        if (groups.empty())
            continue;
        const GameVariant *variant = nullptr;
        for (const auto *table : {&game->variants, &game->fallback})
            for (const GameVariant &v : *table)
                if (!variant && installShieldMatches(v, *cabinet, groups))
                    variant = &v;
        if (!variant)
            continue;
        ImportPlan plan;
        plan.game = {"awe", variant->gameId, variant->description, variant->language, variant->platform};
        planInstallShield(plan, cabinet, groups);
        plan.addPatchLabel.clear();
        plan.addFolderLabel = "Add other disc...";
        plan.addFolderTooltip = "Copy the game's other CD too (or a copy of it)";
        return {plan};
    }
    return {};
}

bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message) {
    const GameInfo *game = findGame(plan.game.gameId);
    if (!game || !plan.cabinet) {
        message = "not a file Reluva can use for this game";
        return false;
    }
    return addInstallShieldVolumes(plan, folder, setupGroups(*game, *plan.cabinet), message);
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
