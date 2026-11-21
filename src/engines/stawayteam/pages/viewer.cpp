// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's data viewer, in reluva-viewer: DATA.dat's
// pictures, sequences, models, fonts, sounds and objects, a page each (each
// page's header has its keys). Tab moves between the pages, Esc leaves.

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"
#include "common/zip.h"
#include "engines/stawayteam/pages/font_viewer.h"
#include "engines/stawayteam/pages/model_viewer.h"
#include "engines/stawayteam/pages/object_viewer.h"
#include "engines/stawayteam/pages/picture_viewer.h"
#include "engines/stawayteam/pages/sequence_viewer.h"
#include "engines/stawayteam/pages/sound_viewer.h"
#include "engines/viewer.h"

#include <memory>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using namespace stawayteam;

class StawayteamViewer : public Engine {
public:
    StawayteamViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const auto path = fs::findFile(_target.path, "DATA.dat");
        if (!path)
            return Error::fail("No DATA.dat in " + _target.path.string());
        if (Error err = _data.open(*path))
            return err;
        // The game's own screen size.
        constexpr int kWidth = 640;
        constexpr int kHeight = 480;
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32))
            return err;
        _screen.create(kWidth, kHeight, PixelFormat::kRGBA32);

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

        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<PictureViewer>(_sys, _screen, _data));
        pages.push_back(std::make_unique<SequenceViewer>(_sys, _screen, _data));
        pages.push_back(std::make_unique<ModelViewer>(_sys, _screen, _data));
        pages.push_back(std::make_unique<FontViewer>(_sys, _screen, _data));
        pages.push_back(std::make_unique<SoundViewer>(_sys, _screen, _data, audioOut ? &mixer : nullptr));
        pages.push_back(std::make_unique<ObjectViewer>(_sys, _screen, _data));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("Nothing to show in " + path->string());
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
    ZipArchive _data;
};

class StawayteamMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "stawayteam"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StawayteamViewer>(system, target);
    }
};

} // namespace

const MetaViewer &stawayteamMetaViewer() {
    static const StawayteamMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
