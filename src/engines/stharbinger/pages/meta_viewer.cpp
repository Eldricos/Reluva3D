// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Harbinger's data viewer for reluva-viewer: the scene viewer (SceneViewer),
// or with start_mode=video in reluva.ini, one of the game's videos on its own.

#include "audio/mixer.h"
#include "common/log.h"
#include "common/settings.h"
#include "common/text.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/game/game.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/pages/viewer.h"
#include "engines/stharbinger/platform.h"
#include "engines/viewer.h"

#include <cstdlib>

namespace reluva::stharbinger {

namespace {

// The game's settings the viewer uses, as stharbinger.cpp keeps them in
// reluva.ini, and its own, start_mode.
constexpr const char *kMusicVolume = "music_volume";
constexpr const char *kSpeechVolume = "speech_volume";
constexpr const char *kEffectsVolume = "effects_volume";
constexpr const char *kStartMode = "start_mode";
constexpr const char *kStartScene = "start_scene";
constexpr const char *kDebug = "debug";
constexpr const char *kDebugLevel = "debug_level";

std::string setting(const GameTarget &target, const char *key) {
    const auto it = target.settings.find(key);
    return it == target.settings.end() ? std::string() : it->second;
}

void runViewer(Harbinger &harbinger, const std::string &mode) {
    harbinger.initResources();
    const std::string &startScene = harbinger.getOptions().startScene;
    if (text::equalsIgnoreCase(mode, "video")) {
        if (startScene.empty())
            fatal("Playing one video needs it named in start_scene (in reluva.ini), e.g. INTRO/VNM.SMK");
        Game game(&harbinger);
        if (game.playVideoFile(startScene) != 0)
            fatal("The video DS9/%s isn't in the game's folders", startScene.c_str());
        return;
    }
    int start = 0;
    if (!startScene.empty()) {
        start = harbinger.findScene(startScene);
        if (start < 0)
            fatal("Unknown scene '%s' (reluva-tool stharbinger info lists them)", startScene.c_str());
    }
    SceneViewer sceneViewer(&harbinger);
    sceneViewer.run(start);
}

class StharbingerViewerEngine : public Engine {
public:
    StharbingerViewerEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        Options options;
        for (const GamePart &part : findGameParts(_target.path))
            options.paths.push_back(part.folder.string());
        if (options.paths.empty())
            return Error::fail("Harbinger isn't found in " + _target.path.string() + " any more");
        const std::string debug = setting(_target, kDebug);
        options.debug = !debug.empty() && debug != "0";
        options.startScene = setting(_target, kStartScene);
        log::setDebugLevel(options.debug ? std::atoi(setting(_target, kDebugLevel).c_str()) : 0);

        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        mixer.setTypeVolume(audio::SoundType::kMusic, settings::volume(_target.settings, kMusicVolume));
        mixer.setTypeVolume(audio::SoundType::kSpeech, settings::volume(_target.settings, kSpeechVolume));
        mixer.setTypeVolume(audio::SoundType::kEffects, settings::volume(_target.settings, kEffectsVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{_sys};

        Platform platform(_sys, audioOut ? &mixer : nullptr, _target.savePath);
        try {
            Harbinger harbinger(&platform, options);
            runViewer(harbinger, setting(_target, kStartMode));
        } catch (const FatalError &e) {
            log::setDebugLevel(0);
            return Error::fail(e.what());
        }
        log::setDebugLevel(0);
        return Error::success();
    }

private:
    System &_sys;
    GameTarget _target;
};

class StharbingerMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "stharbinger"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StharbingerViewerEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stharbinger

namespace reluva {

const MetaViewer &stharbingerMetaViewer() {
    static const stharbinger::StharbingerMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
