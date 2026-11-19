// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's data viewer, in reluva-viewer: the race screen,
// a cruise round the cities, the cities flown through, the models,
// textures and files (pages/viewers.h has each page's keys). Tab moves
// between the pages, Esc leaves.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/pages/viewers.h"
#include "engines/viewer.h"

#include <memory>

namespace reluva {

namespace {

using namespace age;

class AgeViewer : public Engine {
public:
    AgeViewer(System &system, GameTarget target, const GameInfo &game)
        : _sys(system), _target(std::move(target)), _game(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        // Damaged archives don't stop the viewer; the Files page lists what
        // went wrong.
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

        ScreenContext ctx{_sys, _screen, _files, _game, warnings, audioOut ? &mixer : nullptr, _target.savePath};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<RaceScreen>(ctx));
        pages.push_back(std::make_unique<DriveViewer>(ctx));
        pages.push_back(std::make_unique<WorldViewer>(ctx));
        pages.push_back(std::make_unique<ModelViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<FileViewer>(ctx));
        return runPages(_sys, _screen, pages, true);
    }

private:
    System &_sys;
    GameTarget _target;
    const GameInfo &_game;
    SearchSet _files;
    Surface _screen;
};

class AgeMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "age"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<AgeViewer>(system, target, *game);
    }
};

} // namespace

const MetaViewer &ageMetaViewer() {
    static const AgeMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
