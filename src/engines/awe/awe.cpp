// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE Productions engine (clean-room reimplementation): Agatha
// Christie: Murder on the Orient Express. See docs/engines/awe/README.md.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md.
//
// The game is ScenePage (game/scene_page.h), started at its main menu.
// Its data viewer is in pages/, built into reluva-viewer only.

#include "engines/engine.h"

#include "engines/awe/game/scene_page.h"
#include "engines/awe/ui/page.h"

#include <memory>
#include <vector>

namespace reluva {

namespace {

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes. Detection keys on the
// item list: a patch is likelier to replace the executable than the text.
const std::vector<GameVariant> kVariants = {
    {"motoe",
     "Agatha Christie: Murder on the Orient Express",
     "en",
     "windows",
     {{"motoe.exe", "cdfcba4e6c8c7cee637959bc78bd46e7", 684032},
      {"Text/itemlist.txt", "a2a0d2cd331a6fb36ffa79dced219aa5", 37328}}},
};
// The game's name, whatever its release.
constexpr const char *kGameTitle = "Agatha Christie: Murder on the Orient Express";
// Any other release: the same files, whatever their contents.
const std::vector<GameVariant> kFallback = {
    {"motoe",
     "Agatha Christie: Murder on the Orient Express (unknown release)",
     "",
     "windows",
     {{"motoe.exe", "", -1}, {"Text/itemlist.txt", "", -1}, {"granny2.dll", "", -1}}},
};

class AweEngine : public Engine {
public:
    AweEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        return awe::runPages(
            _sys, _target,
            [](awe::ViewerContext &ctx) {
                // The window has the game's name. The original's title is
                // its command line (WinMain 0x4018f0, CreateWindowExW at
                // 0x401a13), given by the installer's shortcut, which the
                // install doesn't hold: the name is the one its readme and
                // shortcut go by.
                std::vector<std::unique_ptr<awe::Page>> pages;
                pages.push_back(std::make_unique<awe::ScenePage>(ctx, awe::ScenePage::Browser::kOff, kGameTitle));
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

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

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
