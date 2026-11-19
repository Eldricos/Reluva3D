// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's data viewer, in reluva-viewer: the mansion, videos,
// animations, images, sounds and text (see viewers.h for each page's keys).
// Tab moves between the pages, Esc leaves.

#include "common/settings.h"
#include "engines/clue/pages/viewers.h"
#include "engines/viewer.h"

namespace reluva {

namespace {

using clue::AnimationViewer;
using clue::ImageViewer;
using clue::MansionViewer;
using clue::SoundViewer;
using clue::TextViewer;
using clue::VideoViewer;

class ClueViewer : public Engine {
public:
    ClueViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const auto language = _target.gameId == "clue-uk" ? clue::Language::kUK : clue::Language::kUS;
        if (Error err = _data.open(_target.path, language))
            return err;
        if (Error err = _sys.initGraphics(clue::kScreenW, clue::kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(clue::kScreenW, clue::kScreenH, PixelFormat::kRGBA32);
        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, audioOut};

        MansionViewer mansion(_sys, _screen, _data);
        VideoViewer videos(_sys, _screen, _data, audioOut ? &mixer : nullptr);
        AnimationViewer animations(_sys, _screen, _data);
        ImageViewer images(_sys, _screen, _data);
        SoundViewer sounds(_sys, _screen, _data, audioOut ? &mixer : nullptr);
        TextViewer texts(_sys, _screen, _data);
        // Viewer 0 shows the mansion (or why it can't), 1 videos, 2
        // animations, 3 images, 4 sounds, 5 text; Tab moves to the next one
        // that has something to show.
        constexpr int kViewers = 6;
        const size_t counts[kViewers] = {
            1, videos.count(), animations.count(), images.count(), sounds.count(), texts.count()};
        int viewer = 0;
        const auto activate = [&] {
            if (viewer == 0)
                mansion.activate();
            else if (viewer == 1)
                videos.activate();
            else if (viewer == 2)
                animations.activate();
            else if (viewer == 3)
                images.activate();
            else if (viewer == 4)
                sounds.activate();
            else
                texts.activate();
        };
        // Stops the sound of the page left.
        const auto deactivate = [&] {
            if (viewer == 1)
                videos.deactivate();
            else if (viewer == 4)
                sounds.deactivate();
        };
        activate();

        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                    deactivate();
                    return Error::success();
                }
                if (viewer == 0) {
                    // Tab, and Esc outside the overview, are the viewer's;
                    // the rest is the mansion's (the mouse too).
                    const bool keyDown = ev.type == EventType::kKeyDown;
                    if (!(keyDown && (ev.key == key::kTab || (ev.key == key::kEscape && !mansion.overview())))) {
                        mansion.handleEvent(ev);
                        continue;
                    }
                }
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape) {
                    deactivate();
                    return Error::success();
                }
                if (ev.key == key::kTab) {
                    deactivate();
                    do
                        viewer = (viewer + 1) % kViewers;
                    while (counts[viewer] == 0);
                    activate();
                    continue;
                }
                if (viewer == 1)
                    videos.handleKey(ev.key);
                else if (viewer == 2)
                    animations.handleKey(ev.key);
                else if (viewer == 3)
                    images.handleKey(ev.key);
                else if (viewer == 4)
                    sounds.handleKey(ev.key);
                else if (viewer == 5)
                    texts.handleKey(ev.key);
            }
            if (viewer == 0)
                mansion.update(_sys.getMillis());
            else if (viewer == 1)
                videos.update(_sys.getMillis());
            else if (viewer == 2)
                animations.update(_sys.getMillis());
            else if (viewer == 4)
                sounds.update(_sys.getMillis());
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
    clue::GameData _data;
};

class ClueMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "clue"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<ClueViewer>(system, target);
    }
};

} // namespace

const MetaViewer &clueMetaViewer() {
    static const ClueMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
