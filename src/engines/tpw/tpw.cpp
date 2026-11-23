// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. So far it recognises
// SimCoaster, plays its opening and its front end, to a world chosen,
// then shows its loading screen.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/games.h"
#include "engines/tpw/ui/loading_screen.h"
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
        if (!playOpening(_sys, audioOut ? &mixer : nullptr, _target.path, files) ||
            runFrontEnd(_sys, files, _target.savePath) < 0)
            return Error::success();

        // What the game shows while the chosen world's park loads; the park
        // itself isn't built yet.
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kRGBA32);
        image::Image loading;
        if (!composeLoadingScreen(files, languageOf(files), loading))
            for (int y = 0; y < std::min(loading.h, kHeight); ++y)
                std::memcpy(screen.row(y), loading.pixel(0, y), size_t(std::min(loading.w, kWidth)) * 4);
        for (bool running = true; running;) {
            Event ev;
            while (_sys.pollEvent(ev))
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape))
                    running = false;
            _sys.updateScreen(screen);
            _sys.delayMillis(1);
        }
        return Error::success();
    }

private:
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
