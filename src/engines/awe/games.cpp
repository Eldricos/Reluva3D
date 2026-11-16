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

// The game's groups in `cabinet`, or none if one is missing.
std::vector<InstallShieldGroup> setupGroups(const GameInfo &game, const InstallShieldCabinet &cabinet) {
    std::vector<InstallShieldGroup> found;
    for (const std::string &name : game.setupGroups) {
        auto it = std::find_if(cabinet.groups.begin(), cabinet.groups.end(),
                               [&](const InstallShieldGroup &g) { return g.name == name; });
        if (it == cabinet.groups.end())
            return {};
        found.push_back(*it);
    }
    return found;
}

// Whether `variant`'s files are among the groups' with their sizes. Their
// MD5s are of what's unpacked, so not known yet: detection checks them
// once the game is imported.
bool matches(const GameVariant &variant, const InstallShieldCabinet &cabinet,
             const std::vector<InstallShieldGroup> &groups) {
    return std::all_of(variant.files.begin(), variant.files.end(), [&](const FileSignature &sig) {
        return std::any_of(groups.begin(), groups.end(), [&](const InstallShieldGroup &g) {
            const long i = cabinet.find(g, sig.name);
            return i >= 0 && (sig.size < 0 || cabinet.files[size_t(i)].size == uint64_t(sig.size));
        });
    });
}

// The plan's files: the groups', as setup would install them. Until every
// volume they're in is found there are none (the game needs its program,
// on the second CD): the notes say which are still wanted.
void planCabinet(ImportPlan &plan, const std::shared_ptr<const InstallShieldCabinet> &cabinet,
                 const std::vector<InstallShieldGroup> &groups) {
    plan.cabinet = cabinet;
    plan.files.clear();
    plan.notes.clear();
    std::vector<int> wanted;
    for (const InstallShieldGroup &g : groups)
        for (size_t i = g.first; i <= g.last && i < cabinet->files.size(); ++i) {
            const InstallShieldFile &f = cabinet->files[i];
            if (f.path.empty())
                continue;
            plan.files.push_back({cabinet->header, f.path, f.size, std::nullopt, i});
            if (cabinet->reachable(i))
                continue;
            for (size_t v = cabinet->files[f.data].volume; v < cabinet->volumes.size(); ++v)
                if (cabinet->volumes[v].path.empty() && std::find(wanted.begin(), wanted.end(), int(v)) == wanted.end())
                    wanted.push_back(int(v));
        }
    if (wanted.empty())
        return;
    std::sort(wanted.begin(), wanted.end());
    std::string names;
    for (int v : wanted)
        names += (names.empty() ? "" : ", ") + cabinet->volumeName(v);
    plan.notes.push_back("The game is on two CDs: add the other (with " + names +
                         ") with \"Add other disc...\" to import it");
    plan.files.clear();
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
                if (!variant && matches(v, *cabinet, groups))
                    variant = &v;
        if (!variant)
            continue;
        ImportPlan plan;
        plan.game = {"awe", variant->gameId, variant->description, variant->language, variant->platform};
        planCabinet(plan, cabinet, groups);
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
    auto cabinet = std::make_shared<InstallShieldCabinet>(*plan.cabinet);
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::is_directory(folder, ec) ? folder : folder.parent_path();
    if (findInstallShieldVolumes(*cabinet, dir) == 0) {
        std::string names;
        for (size_t v = 1; v < cabinet->volumes.size(); ++v)
            if (cabinet->volumes[v].path.empty())
                names += (names.empty() ? "" : ", ") + cabinet->volumeName(int(v));
        message = names.empty() ? "the game's CDs are both added already"
                                : "not the game's other CD: none of " + names + " is here";
        return false;
    }
    planCabinet(plan, cabinet, setupGroups(*game, *cabinet));
    return true;
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
