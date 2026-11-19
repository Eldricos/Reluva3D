// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed engine, starting with Need for Speed III: Hot Pursuit
// (docs/engines/nfs/games/nfs3/PLAN.md).
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, public format documentation, or documented
// reverse engineering of a legally owned copy. Never from leaked source
// code. See CONTRIBUTING.md.
//
// The game is the retail nfs3.exe run in the x86 interpreter (host/),
// shown in the window by the Play page (ui/play_page.h). Its data viewer
// is reluva-viewer's (pages/viewer.cpp).

#include "engines/engine.h"
#include "engines/nfs/games.h"
#include "engines/nfs/ui/page.h"
#include "engines/nfs/ui/play_page.h"

#include <string>
#include <vector>

namespace reluva {

namespace {

using nfs::GameInfo;

class NfsEngine : public Engine {
public:
    NfsEngine(System &system, GameTarget target, const GameInfo &game)
        : _sys(system), _target(std::move(target)), _game(game) {}

    Error run() override {
        auto makePages = [this](const nfs::GameFolders &folders, audio::Mixer *mixer) {
            std::vector<std::unique_ptr<nfs::Page>> pages;
            pages.push_back(nfs::makePlayPage(folders, _target.savePath, mixer, _target.settings));
            return pages;
        };
        return nfs::runPages(_sys, _target, _game, makePages, false);
    }

private:
    System &_sys;
    GameTarget _target;
    const GameInfo &_game;
};

class NfsMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "nfs"; }
    const char *name() const override { return "Need for Speed"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), nfs::detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), nfs::fallbackTable(), dir) : found;
    }

    // The settings the game reads (ui/play_page.cpp, makePlayPage), for
    // every NFS game and each: the first choice of each is what the
    // engine does when it's set nowhere. The others (cdpath, serial, modem,
    // wheelpedals, glidedll, glideboard) name things of the machine's, and
    // are set in reluva.ini.
    std::vector<GameOption> engineOptions() const override {
        auto choice = [](const char *key, const char *label, const char *tooltip,
                         std::vector<std::pair<std::string, std::string>> choices) {
            GameOption o;
            o.key = key;
            o.label = label;
            o.tooltip = tooltip;
            o.choices = std::move(choices);
            return o;
        };
        return {
            choice("renderer", "Renderer the game is told of",
                   "The game draws as for a Voodoo 2 (with fog) through Reluva's own THRASH; or as for its "
                   "Direct3D renderer; or its own Voodoo 2 renderer runs on the Glide stand-in.",
                   {{"voodoo2", "Voodoo 2"}, {"direct3d", "Direct3D"}, {"glide", "Voodoo 2 on Glide"}}),
            choice("renderscale", "Picture size", "The game's 640x480 drawn at this many times its size on the GPU.",
                   {{"2", "2x"}, {"1", "1x"}, {"3", "3x"}, {"4", "4x"}}),
            choice("texturefilter", "Textures", "Filtered as the game's renderers ask, or point-sampled.",
                   {{"bilinear", "Filtered"}, {"point", "Point-sampled"}}),
            choice("surround", "Dolby Surround",
                   "The game's Dolby Surround mode through a model of a Pro Logic decoder: to a 4.0 or 5.1 "
                   "device, the passive matrix alone, or folded back to stereo for headphones.",
                   {{"off", "Off (stereo as mixed)"},
                    {"prologic", "Pro Logic"},
                    {"passive", "Passive"},
                    {"headphones", "Headphones"}}),
            choice("joystick", "Game controller", "A controller connected when the game starts, offered to it.",
                   {{"on", "Offered to the game"}, {"off", "Left out"}}),
            choice("forcefeedback", "Force feedback", "The game's forces to a wheel's motor or a pad's rumble.",
                   {{"on", "On"}, {"off", "Off"}}),
            choice("network", "Network play", "The game's network play over the machine's network.",
                   {{"on", "On"}, {"off", "Off"}}),
            choice("positionalupdates", "Positional updates",
                   "Network races with the game's positional updates (for slow links); every player needs the "
                   "same.",
                   {{"off", "Off"}, {"on", "On"}}),
            choice("movielines", "Movies' black lines", "The lines the game leaves black between a movie's.",
                   {{"fill", "Filled in"}, {"keep", "Kept"}}),
        };
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = nfs::findGame(target.gameId);
        if (!game)
            game = &nfs::games().front();
        return std::make_unique<NfsEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine &nfsMetaEngine() {
    static const NfsMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
