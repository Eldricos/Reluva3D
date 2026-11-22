// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's data viewer, in reluva-viewer: the resource file's pictures,
// sounds and object definitions, and the boards (see viewers.h for each
// page's keys). Tab moves between the pages, Esc leaves.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/legoloco/game_files.h"
#include "engines/legoloco/pages/viewers.h"
#include "engines/viewer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using legoloco::GameFiles;

class LegolocoViewer : public Engine {
public:
    LegolocoViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        GameFiles files;
        if (Error err = files.open(_target.path))
            return err;
        if (Error err = _sys.initGraphics(legoloco::kScreenW, legoloco::kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(legoloco::kScreenW, legoloco::kScreenH, PixelFormat::kRGBA32);

        // Without an audio device, sounds are listed and not played.
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

        legoloco::BoardPage boards(_sys, _screen, files);
        legoloco::PicturePage pictures(_sys, _screen, files);
        legoloco::DefinitionPage definitions(_sys, _screen, files);
        legoloco::SoundPage sounds(_sys, _screen, files, audioOut ? &mixer : nullptr);
        std::vector<legoloco::Page *> pages;
        for (legoloco::Page *page : std::initializer_list<legoloco::Page *>{&boards, &pictures, &definitions, &sounds})
            if (page->count() > 0)
                pages.push_back(page);
        if (pages.empty())
            return Error::fail("Nothing to show in " + _target.path.string());

        size_t page = 0;
        const auto show = [&] {
            _sys.setWindowTitle(("Reluva - " + _target.description + " - " + pages[page]->name()).c_str());
            pages[page]->activate();
        };
        show();
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
                    page = (page + 1) % pages.size();
                    show();
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

class LegolocoMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "legoloco"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegolocoViewer>(system, target);
    }
};

} // namespace

const MetaViewer &legolocoMetaViewer() {
    static const LegolocoMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
