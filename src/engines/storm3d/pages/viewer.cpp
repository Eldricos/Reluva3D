// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's data viewer, in reluva-viewer: a page for each kind of file the
// game has, each page's keys in its header. Tab moves to the next page
// that has something to show, Esc leaves.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/image_page.h"
#include "engines/storm3d/pages/movie_page.h"
#include "engines/storm3d/pages/page.h"
#include "engines/storm3d/pages/sound_page.h"
#include "engines/viewer.h"

#include <memory>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using storm3d::GameFiles;
using storm3d::Page;

class Storm3dViewer : public Engine {
public:
    Storm3dViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        GameFiles files;
        if (Error err = files.open(_target.path))
            return err;
        if (Error err = _sys.initGraphics(storm3d::kViewerWidth, storm3d::kViewerHeight, PixelFormat::kRGBA32))
            return err;
        _screen.create(storm3d::kViewerWidth, storm3d::kViewerHeight, PixelFormat::kRGBA32);

        // One mixer for as long as the viewer runs; without an audio device
        // sounds are listed and not played.
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
        audio::Mixer *out = audioOut ? &mixer : nullptr;

        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<storm3d::ImagePage>(_sys, _screen, files));
        pages.push_back(std::make_unique<storm3d::SoundPage>(_sys, _screen, files, out));
        pages.push_back(std::make_unique<storm3d::MoviePage>(_sys, _screen, files, out));

        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("Nothing to show in " + _target.path.string());
        pages[page]->activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape)
                    return Error::success();
                if (ev.key == key::kTab) {
                    pages[page]->deactivate();
                    do
                        page = (page + 1) % pages.size();
                    while (pages[page]->count() == 0);
                    pages[page]->activate();
                    continue;
                }
                pages[page]->handleKey(ev.key);
            }
            pages[page]->update(_sys.getMillis());
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
};

class Storm3dMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "storm3d"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<Storm3dViewer>(system, target);
    }
};

} // namespace

const MetaViewer &storm3dMetaViewer() {
    static const Storm3dMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
