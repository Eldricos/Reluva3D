// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE Productions engine (clean-room reimplementation): Agatha
// Christie: Murder on the Orient Express, and And Then There Were None (its
// files only, so far). See docs/engines/awe/README.md.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md.
//
// The game is ScenePage (game/scene_page.h), started at its main menu;
// each game's own code is in its folder (motoe/, attwn/), described to the
// rest in games.h. The data viewer is in pages/, built into reluva-viewer
// only.

#include "engines/engine.h"

#include "engines/awe/game/scene_page.h"
#include "engines/awe/games.h"
#include "engines/awe/ui/page.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva {

namespace {

class AweEngine : public Engine {
public:
    AweEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const awe::GameInfo *game = awe::findGame(_target.gameId);
        if (!game)
            return Error::fail("No AWE game has the id '" + _target.gameId + "'");
        if (!game->playable)
            return Error::fail(_target.description + " can't be played yet, only looked at in reluva-viewer");
        const std::string title = game->title;
        return awe::runPages(
            _sys, _target,
            [&title](awe::ViewerContext &ctx) {
                std::vector<std::unique_ptr<awe::Page>> pages;
                pages.push_back(std::make_unique<awe::ScenePage>(ctx, awe::ScenePage::Browser::kOff, title));
                return pages;
            },
            "scenes (Locations/**/*.ac2)");
    }

private:
    System &_sys;
    GameTarget _target;
};

class AweMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "awe"; }
    const char *name() const override { return "AWE (Agatha Christie)"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override { return awe::detectGames(dir); }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<AweEngine>(system, target);
    }
};

} // namespace

const MetaEngine &aweMetaEngine() {
    static const AweMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
