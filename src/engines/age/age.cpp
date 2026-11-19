// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Angel Game Engine: Angel Studios' games, starting with Midtown Madness 2.
// The game opens on its own menus and races (ui/screen.h); its data viewer
// (pages/) is in reluva-viewer.
//
// Sources: the file formats are worked out from the game's own data, and
// its behaviour from public footage (docs/engines/age/). The program is
// encrypted by its copy protection, so nothing comes from it. See
// CONTRIBUTING.md: never use leaked or unlicensed code.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/ui/screen.h"
#include "engines/engine.h"
#include "video/movie.h"

#include <algorithm>
#include <cctype>

#include <memory>

namespace reluva {

namespace {

using namespace age;

// The game folder's file of that name, in any letter case; empty if none.
std::filesystem::path findInFolder(const std::filesystem::path &dir, const std::string &name) {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string file = entry.path().filename().string();
        std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (file == name && entry.is_regular_file(ec))
            return entry.path();
    }
    return {};
}

class AgeEngine : public Engine {
public:
    AgeEngine(System &system, GameTarget target, const GameInfo &game)
        : _sys(system), _target(std::move(target)), _game(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = mountGame(_files, _target.path, warnings))
            return err;
        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);

        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            bool on;
            ~AudioOff() {
                if (on)
                    sys.stopAudio();
            }
        } audioOff{_sys, audioOut};

        // The opening movie (Indeo 5), as the game opens; a key or click
        // skips it. Where it can't be played the splash picture stands in,
        // as the game's own "No Movie" shortcut leaves it out.
        bool opening = false;
        if (const auto movie = findInFolder(_target.path, "logos.avi"); !movie.empty()) {
            bool quit = false;
            opening = !video::playMovie(_sys, audioOut ? &mixer : nullptr, movie, quit);
            if (quit)
                return Error::success();
            if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                return err;
        }

        ScreenContext ctx{_sys, _screen, _files, _game, warnings, audioOut ? &mixer : nullptr, _target.savePath};
        ctx.openingPlayed = opening;
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<RaceScreen>(ctx));
        return runPages(_sys, _screen, pages, false);
    }

private:
    System &_sys;
    GameTarget _target;
    const GameInfo &_game;
    SearchSet _files;
    Surface _screen;
};

class AgeMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "age"; }
    const char *name() const override { return "Angel Game Engine"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<AgeEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine &ageMetaEngine() {
    static const AgeMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
