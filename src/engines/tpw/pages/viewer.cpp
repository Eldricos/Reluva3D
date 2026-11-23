// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's data viewer, in reluva-viewer: the game's
// pictures, fonts, text, settings, models, worlds, sounds, movies and files (see
// viewers.h for each page's keys). Tab and Shift+Tab move between the
// pages, Esc leaves.

#include "audio/mixer.h"
#include "common/events.h"
#include "common/settings.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/pages/viewers.h"
#include "engines/viewer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using namespace tpw;

class TpwViewer : public Engine {
public:
    TpwViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        SearchSet files;
        std::vector<std::string> warnings;
        if (Error err = mountGame(files, _target.path, warnings))
            return err;
        if (Error err = _sys.initGraphics(kViewerWidth, kViewerHeight, PixelFormat::kRGBA32))
            return err;
        Surface screen;
        screen.create(kViewerWidth, kViewerHeight, PixelFormat::kRGBA32);

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

        ViewerContext context{_sys, files, _target.path, audioOut ? &mixer : nullptr, languageOf(files), {}};
        std::vector<uint8_t> data;
        if (!files.read("Language/" + context.language + "/MBToUni.dat", data))
            (void)context.characters.parse(data);

        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<PicturePage>(context));
        pages.push_back(std::make_unique<FontPage>(context));
        pages.push_back(std::make_unique<TextPage>(context));
        pages.push_back(std::make_unique<SettingsPage>(context));
        pages.push_back(std::make_unique<ModelPage>(context));
        pages.push_back(std::make_unique<WorldPage>(context));
        pages.push_back(std::make_unique<SoundPage>(context));
        pages.push_back(std::make_unique<MoviePage>(context));
        pages.push_back(std::make_unique<FilePage>(context));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("Nothing to show in " + _target.path.string());

        auto show = [&] {
            pages[page]->draw(screen);
            _sys.setWindowTitle("Reluva - " + _target.description + " - " + pages[page]->name() + ": " +
                                pages[page]->current());
            _sys.updateScreen(screen);
        };
        show();
        while (true) {
            Event ev;
            bool changed = false;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                if (ev.key == key::kTab) {
                    // The next page with something to show, or the one before with Shift.
                    pages[page]->deactivate();
                    const size_t step = ev.mods & keymod::kShift ? pages.size() - 1 : 1;
                    do
                        page = (page + step) % pages.size();
                    while (pages[page]->count() == 0);
                } else {
                    pages[page]->handleKey(ev.key);
                }
                changed = true;
            }
            if (pages[page]->tick(_sys.getMillis()))
                changed = true;
            if (changed)
                show();
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
};

class TpwMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "tpw"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<TpwViewer>(system, target);
    }
};

} // namespace

const MetaViewer &tpwMetaViewer() {
    static const TpwMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
