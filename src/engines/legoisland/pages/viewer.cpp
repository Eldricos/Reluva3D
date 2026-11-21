// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's data viewer, in reluva-viewer: the SI files' objects and
// media (see viewers.h for the page's keys). Esc leaves.

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

using legoisland::SiViewer;

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
        if (files.empty())
            return Error::fail("No SI files in " + _target.path.string() +
                               ": import the game from its CD for them (its LEGO\\SCRIPTS folder)");
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

        SiViewer si(_sys, _screen, std::move(files), audioOut ? &mixer : nullptr);
        si.activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape) {
                    si.deactivate();
                    return Error::success();
                }
                si.handleKey(ev.key);
            }
            si.update(_sys.getMillis());
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
