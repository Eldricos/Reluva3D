// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. So far it recognises
// SimCoaster, plays its opening and its front end, to a world chosen,
// then shows its loading screen.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/games.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/globe.h"
#include "engines/tpw/ui/loading_screen.h"
#include "engines/tpw/ui/park_screen.h"
#include "engines/tpw/ui/main_menu.h"
#include "engines/tpw/ui/opening.h"
#include "image/image.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

/**
 * The game running: for now its opening, its front end to a world
 * chosen, then its loading screen until the player quits.
 */
class TpwEngine : public Engine {
public:
    TpwEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        SearchSet files;
        std::vector<std::string> warnings;
        if (Error err = mountGame(files, _target.path, warnings))
            return err;
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());

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
        if (!playOpening(_sys, audioOut ? &mixer : nullptr, _target.path, files))
            return Error::success();
        // After the movie, which plays as music, at its own full level.
        setTypeVolumes(files, mixer);
        PlayerFile player;
        const int world = runFrontEnd(_sys, files, _target.savePath, audioOut ? &mixer : nullptr, &player);
        if (world < 0)
            return Error::success();

        // The loading screen while the chosen world's park loads, then the
        // park. Its first mission's for now: missions after the first wait
        // for the park to be won.
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kRGBA32);
        image::Image loading;
        if (!composeLoadingScreen(files, languageOf(files), loading))
            for (int y = 0; y < std::min(loading.h, kHeight); ++y)
                std::memcpy(screen.row(y), loading.pixel(0, y), size_t(std::min(loading.w, kWidth)) * 4);
        _sys.updateScreen(screen);
        Park park;
        ParkScreen parkScreen;
        const std::string folder = Globe::worlds()[size_t(world)].folder;
        if (Error err = park.load(files, folder.substr(0, folder.size() - 1), 0))
            return err;
        if (Error err = parkScreen.load(files, park))
            return err;
        // The mission's briefing over the park as it opens.
        Briefing briefing;
        soft3d::Rasterizer rasterizer;
        const ParkScreen::Camera start = parkScreen.startCamera(park);
        if (!briefing.load(files) && !showBriefing(_sys, briefing, park, player.name,
                                                   [&](Surface &s) { parkScreen.draw(s, rasterizer, park, start); }))
            return Error::success();
        playPark(_sys, parkScreen, park);
        return Error::success();
    }

private:
    // The game's own levels for its sounds, music and speech, percent, as
    // its settings give them (sound.sam's DefaultVolume); full without them.
    static void setTypeVolumes(const SearchSet &files, audio::Mixer &mixer) {
        std::vector<uint8_t> data;
        SamFile sound;
        if (files.read("sound.sam", data) || sound.parse(data))
            return;
        for (auto [type, name] : {std::pair{audio::SoundType::kEffects, "DefaultVolume.SFX"},
                                  std::pair{audio::SoundType::kMusic, "DefaultVolume.MUSIC"},
                                  std::pair{audio::SoundType::kSpeech, "DefaultVolume.SPEECH"}})
            mixer.setTypeVolume(type, int(sound.number(name, 100) * audio::kMaxVolume / 100));
    }

    static constexpr int kWidth = kOpeningWidth;
    static constexpr int kHeight = kOpeningHeight;
    System &_sys;
    GameTarget _target;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a TpwEngine to run one.
 */
class TpwMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "tpw"; }
    const char *name() const override { return "Theme Park World"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        return tpw::planImport(source);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<TpwEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::tpw

namespace reluva {

const MetaEngine &tpwMetaEngine() {
    static const tpw::TpwMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
