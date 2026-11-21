// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's data viewer, in reluva-viewer: the SI files' objects and
// media, and WORLD.WDB's textures and models (see viewers.h for each
// page's keys). Tab goes to the next page; Esc leaves.

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"
#include "engines/legoisland/pages/viewers.h"
#include "engines/viewer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using legoisland::SiScenePage;
using legoisland::SiViewer;
using legoisland::ViewerPage;
using legoisland::WdbFile;
using legoisland::WdbModelPage;
using legoisland::WdbTexturePage;

// Every .si file in the game's folder, its scripts folder first as the
// game's own order would have it.
std::vector<std::filesystem::path> findSiFiles(const std::filesystem::path &dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && fs::toLower(it->path().extension().string()) == ".si")
            out.push_back(it->path());
    std::sort(out.begin(), out.end(), [&](const auto &a, const auto &b) {
        return fs::windowsNameLess(a.lexically_relative(dir).generic_string(),
                                   b.lexically_relative(dir).generic_string());
    });
    return out;
}

class LegoislandViewer : public Engine {
public:
    LegoislandViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        std::vector<std::filesystem::path> files = findSiFiles(_target.path);
        WdbFile wdb;
        Error wdbError = Error::fail("No lego/data/WORLD.WDB");
        if (const auto path = fs::findFile(_target.path, "lego/data/WORLD.WDB"))
            wdbError = wdb.open(*path);
        if (files.empty() && wdbError)
            return Error::fail("No SI files in " + _target.path.string() +
                               ": import the game from its CD for them (its LEGO\\SCRIPTS folder); " +
                               wdbError.message);
        if (Error err = _sys.initGraphics(legoisland::kViewerW, legoisland::kViewerH, PixelFormat::kRGBA32))
            return err;
        _screen.create(legoisland::kViewerW, legoisland::kViewerH, PixelFormat::kRGBA32);

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

        std::vector<std::unique_ptr<ViewerPage>> pages;
        pages.push_back(std::make_unique<SiViewer>(_sys, _screen, files, audioOut ? &mixer : nullptr));
        if (!wdbError) {
            pages.push_back(std::make_unique<WdbTexturePage>(_screen, wdb));
            pages.push_back(std::make_unique<WdbModelPage>(_sys, _screen, wdb));
            // Its models and animations take parts and models from WORLD.WDB.
            pages.push_back(std::make_unique<SiScenePage>(_sys, _screen, wdb, std::move(files)));
        }
        size_t current = 0;
        while (pages[current]->count() == 0)
            current++;
        pages[current]->activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                    pages[current]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                    pages[current]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                    pages[current]->deactivate();
                    do
                        current = (current + 1) % pages.size();
                    while (pages[current]->count() == 0);
                    if (!pages[current]->uses3D())
                        _sys.initGraphics(legoisland::kViewerW, legoisland::kViewerH, PixelFormat::kRGBA32);
                    pages[current]->activate();
                    continue;
                }
                pages[current]->handleEvent(ev);
            }
            pages[current]->update(_sys.getMillis());
            if (!pages[current]->uses3D())
                _sys.updateScreen(_screen);
            _sys.delayMillis(5);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
};

class LegoislandMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "legoisland"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegoislandViewer>(system, target);
    }
};

} // namespace

const MetaViewer &legoislandMetaViewer() {
    static const LegoislandMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
